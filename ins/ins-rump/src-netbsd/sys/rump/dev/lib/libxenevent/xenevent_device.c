/**************************************************************************
 * STAR LAB PROPRIETARY & CONFIDENTIAL
 * Copyright (C) 2018, Star Lab — All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited.
 **************************************************************************/

// Define a character device

// Headers in src-netbsd/sys/sys

#include <sys/cdefs.h>

#if defined(_KERNEL_OPT)
#   include "opt_compat_netbsd.h"
#endif

#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/conf.h>
#include <sys/evcnt.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/poll.h>
#include <sys/pool.h>
#include <sys/rndpool.h>
#include <sys/rndsource.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/systm.h> // printf

#include <net/route.h>
#include <net/if.h>
#include <netinet/in.h>

#include <sys/vfs_syscalls.h>

#include <rump-sys/kern.h>
#include <rump-sys/vfs.h>
#include <sys/time.h>

#include "xenevent_common.h"
#include "xenevent_device.h"
#include "xenevent_comms.h"

#include "xenevent_netbsd.h"
#include "xenevent_minios.h"

#include "xen_keystore_defs.h"
#include "ioconf.h"
#include "ins-ioctls.h"


// Should we emit message contents? Very verbose!!
#define XE_EMIT_MESSAGE_CONTENTS 0

// Function prototypes 

// src-netbsd/sys/sys/conf.h
static dev_type_open ( xe_dev_open  );
static dev_type_close( xe_dev_close );
static dev_type_read ( xe_dev_read  );
static dev_type_write( xe_dev_write );
static dev_type_ioctl( xe_dev_ioctl );


// Character device entry points 
static struct cdevsw
xe_cdevsw = {
    .d_open    = xe_dev_open,
    .d_close   = xe_dev_close,
    .d_read    = xe_dev_read,
    .d_write   = xe_dev_write,
    .d_ioctl   = xe_dev_ioctl,

    .d_stop    = nostop,
    .d_tty     = notty,
    .d_poll    = nopoll,
    .d_mmap    = nommap,
    .d_kqfilter = nokqfilter,
    .d_discard = nodiscard,
    .d_flag    = 0 //D_OTHER | D_MPSAFE
};

typedef struct _xen_dev_state
{
    //
    // Only one thread can read from the device 
    //
    xenevent_mutex_t read_lock;

    //
    // Only one thread can write to the device 
    //
    xenevent_mutex_t write_lock;

    //
    // Only one handle to device allowed
    //
    uint32_t open_count;

    //
    // Have we found the IP address?
    //
    bool ip_found;

} xen_dev_state_t;

static xen_dev_state_t g_state;
#define IP_BUF_SIZE 128


//
// The next 2 functions are used to discover the public IP address of
// the Rump instance. Presently they just publish the first AF_INET
// address found, which works for now.
//
static int
xe_dev_iface_callback( struct rtentry * RtEntry, void * Arg )
{
    int rc = 0;
    char ip[ 4 * 4 ]; // IP4: 4 chars per octet
    struct ifaddr *ifa = NULL;

    char * buf = (char *) Arg;

    IFADDR_READER_FOREACH( ifa, RtEntry->rt_ifp )
    {
        if ( AF_INET == ifa->ifa_addr->sa_family )
        {
            sin_print( ip, sizeof(ip), (const void *) ifa->ifa_addr );

            if ( 0 == strstr( buf, ip ) )
            {
                snprintf( buf, IP_BUF_SIZE, "%s %s", buf, ip );
            }
            break;
        }
    }

    return rc;
}


static int
xe_dev_publish_ip( void )
{
    int rc = 0;
    char * buf = NULL;

    if ( g_state.ip_found )
    {
        goto ErrorExit;
    }

    // Whether or not we find the IP, we are starting a search for it
    // not and won't do it again.
    g_state.ip_found = true;

    buf = (char *) malloc( IP_BUF_SIZE, 0, M_WAITOK | M_ZERO | M_CANFAIL );
    if ( NULL == buf )
    {
        MYASSERT( !"malloc" );
        rc = ENOMEM;
        goto ErrorExit;
    }
    memset( buf, 0, IP_BUF_SIZE );

    // Invoke the callback for each network interface
    rc = rt_walktree( AF_INET, xe_dev_iface_callback, buf );
    if ( rc )
    {
        MYASSERT( !"rt_walktree" );
        goto ErrorExit;
    }

    rc = xe_comms_publish_ip_addr( buf );
    if ( rc ) { goto ErrorExit; }

ErrorExit:
    if ( buf )
    {
        free( buf, 0 );
    }

    return rc;
}



// TODO: we don't get or establish meaningful major/minor numbers here
// or in the open function. Fix this.
int
xe_dev_init( void )
{
    int rc = 0;
    devmajor_t cmaj = NODEVMAJOR;
    devmajor_t bmaj = NODEVMAJOR;
    devminor_t cmin = 0;

    memset( &g_state, 0, sizeof(g_state) );

    rc = xe_comms_init();
    if ( rc )
    {
        goto ErrorExit;
    }

    // Attach driver to device
    // See driver for /dev/random for example
/*
    rc = config_init_component( cfdriver_ioconf_xenevent,
                                 cfattach_ioconf_xenevent,
                                 cfdata_ioconf_xenevent);
    if ( 0 != rc )
    {
        MYASSERT( !"Failed to attach driver" );
        goto ErrorExit;
    }
*/
    
    cmaj = cdevsw_lookup_major(&xe_cdevsw);

    rc = devsw_attach( DEVICE_NAME,
                       NULL, // block device info
                       &bmaj,
                       &xe_cdevsw, // char device info
                       &cmaj );
    if ( 0 != rc )
    {
        MYASSERT( !"Failed to attach driver" );
        goto ErrorExit;
    }

    // Make the character device
    rc = rump_vfs_makeonedevnode( S_IFCHR,
                                  DEVICE_PATH,
                                  cmaj,
                                  cmin );
    if ( 0 != rc )
    {
        MYASSERT( !"Failed to create control device" );
        goto ErrorExit;
    }

ErrorExit:
    return rc;
}


int
xe_dev_fini( void )
{
    int rc = 0;

    rc = xe_comms_fini();

    return rc;
}
    

int
xe_dev_ioctl( dev_t Dev, u_long Cmd, void *Data, int Num, struct lwp *Thing )
{
    int rc = 0;

    switch ( Cmd )
    {

    case INS_GET_SOCK_PARAMS_IOCTL:
        rc = xe_comms_get_sock_params( (char *) Data );
        if ( 0 != rc )
        {
            goto ErrorExit;
        }
        break;
        
    case INS_HEARTBEAT_IOCTL:
        rc = xe_comms_heartbeat( (const char *) Data );
        if ( 0 != rc )
        {
            MYASSERT( !"heartbeat failed" );
            rc = EPASSTHROUGH;
            goto ErrorExit;
        }
        break;

    case INS_DOMID_IOCTL:
        rc = xe_comms_get_domid();
        if ( rc <= 0 )
        {
            MYASSERT( !"get domid failed" );
            rc = EPASSTHROUGH;
            goto ErrorExit;
        }

        *(domid_t *) Data = (domid_t) rc;
        DEBUG_PRINT( "xe_dev_ioctl returning domid: %u\n", rc );
        rc = 0;
        break;

    case INS_PUBLISH_LISTENERS_IOCTL:
        rc = xe_comms_listeners( (const char *) Data );
        if ( 0 != rc )
        {
            goto ErrorExit;
        }
        break;
        
    default:
        rc = EINVAL;
        MYASSERT( !"Invalid IOCTL code" );
    }

ErrorExit:
    return rc;
}


static int
xe_dev_open( dev_t        Dev,
             int          Flags,
             int          Fmt,
             struct lwp * Lwp )
{
    int rc = 0;
    DEBUG_PRINT("Opened device=%p, Flags=%x Fmt=%x Lwp=%p\n",
                (void *)Dev, Flags, Fmt, Lwp);
    DEBUG_BREAK();
    if ( xenevent_atomic_inc( &g_state.open_count ) > 1 )
    {
        rc = BMK_EBUSY;
        MYASSERT( !"device is already open" );
        goto ErrorExit;
    }

    rc = xenevent_mutex_init( &g_state.read_lock );
    if ( 0 != rc )
    {
        goto ErrorExit;
    }

    rc = xenevent_mutex_init( &g_state.write_lock );
    if ( 0 != rc )
    {
        goto ErrorExit;
    }

    rc = xe_comms_register();
    if ( rc )
    {
        goto ErrorExit;
    }

    // Publish the IP only after user-mode component has loaded;
    // otherwise the IP hasn't been assigned yet. Do it only once.
    rc = xe_dev_publish_ip();
    if ( rc )
    {
        goto ErrorExit;
    }

ErrorExit:
    return rc;
}

static int
xe_dev_close( dev_t Dev,
              int Flags,
              int Fmt,
              struct lwp * Lwp )
{
    int rc = 0;
    DEBUG_PRINT("Closed device=%p, Flags=%xx Fmt=%x Lwp=%p\n",
                (void *)Dev, Flags, Fmt, Lwp);

    if ( 0 != xenevent_atomic_dec( &g_state.open_count ) )
    {
        goto ErrorExit;
    }
        
    xenevent_mutex_destroy( &g_state.read_lock );
    xenevent_mutex_destroy( &g_state.write_lock );

    rc = xe_dev_fini();
    
ErrorExit:
    return rc;
}

/*
static struct timespec 
diff_time(struct timespec start, struct timespec end)
{
	struct timespec temp;
	if ( ( end.tv_nsec - start.tv_nsec ) < 0 ) {
		temp.tv_sec = end.tv_sec - start.tv_sec - 1;
		temp.tv_nsec = 1000000000 + end.tv_nsec - start.tv_nsec;
	} else {
		temp.tv_sec = end.tv_sec - start.tv_sec;
		temp.tv_nsec = end.tv_nsec - start.tv_nsec;
	}
	return temp;
}
*/

//
// The read function blocks until a message is read from the ring
// buffer. It then writes that message to the memory specified and
// returns.
//

static int
xe_dev_read( dev_t Dev,
             struct uio * Uio,
             int Flag )
{
    int rc = 0;
    //struct timespec t1,t2,t3;
    
    // Only one reader at a time
    xenevent_mutex_wait( g_state.read_lock );

    // Only process one request (?)
    MYASSERT( 1 == Uio->uio_iovcnt );

    for ( int i = 0; i < Uio->uio_iovcnt; i++ )
    {
        struct iovec * iov = &(Uio->uio_iov[i]);
        size_t bytes_read = 0;

        //clock_gettime1(CLOCK_REALTIME, &t1);
        
        rc = xe_comms_read_item( iov->iov_base,
                                 iov->iov_len,
                                 &bytes_read );

        //clock_gettime1(CLOCK_REALTIME, &t2);

        //t3 = diff_time(t1,t2);

        //DEBUG_PRINT( "Time of Execution for xe_comms_read_item. sec: %ld. nsec:%ld\n",
                     //t3.tv_sec, t3.tv_nsec);
        if ( rc )
        {
            goto ErrorExit;
        }

#if XE_EMIT_MESSAGE_CONTENTS
        DEBUG_PRINT( "Read request: %d bytes at %p\n",
                     (int)Uio->uio_iov[i].iov_len, Uio->uio_iov[i].iov_base );

        hex_dump( "Read request", iov->iov_base, (int) iov->iov_len );
#endif // XE_EMIT_MESSAGE_CONTENTS

        // Inform system of data transfer
        iov->iov_len -= bytes_read;
        iov->iov_base = (uint8_t *)iov->iov_base + bytes_read;
        
        Uio->uio_offset += bytes_read;
        Uio->uio_resid  -= bytes_read;
    }

ErrorExit:
    xenevent_mutex_release( g_state.read_lock );
    return rc;
}


//
// The write function writes the given message to the ring buffer.
//

static int
xe_dev_write( dev_t Dev,
              struct uio * Uio,
              int Flag )
{
    int rc = 0;

    // Only one writer at a time
    xenevent_mutex_wait( g_state.write_lock );
    
    for ( int i = 0; i < Uio->uio_iovcnt; i++ )
    {
        struct iovec * iov = &(Uio->uio_iov[i]);
        size_t bytes_written = 0;
        //struct timespec t1,t2,t3;

        //clock_gettime1(CLOCK_REALTIME, &t1);
        
        // Wait for a command to arrive
        rc = xe_comms_write_item( iov->iov_base,
                                  iov->iov_len,
                                  &bytes_written);

        //clock_gettime1(CLOCK_REALTIME, &t2);

        //t3 = diff_time(t1,t2);

        //DEBUG_PRINT( "Time of Execution for xe_comms_write_item. sec: %ld. nsec:%ld\n",
                     //t3.tv_sec, t3.tv_nsec);

        if ( rc )
        {
            goto ErrorExit;
        }

#if XE_EMIT_MESSAGE_CONTENTS
        DEBUG_PRINT( "Write response: %d bytes at %p\n",
                     (int)iov->iov_len, iov->iov_base );
        hex_dump( "Write response", 
                  iov->iov_base, (int) iov->iov_len );
#endif // XE_EMIT_MESSAGE_CONTENTS

        // Inform system of data transfer
        iov->iov_len -= bytes_written;
        iov->iov_base = (uint8_t *)iov->iov_base + bytes_written;
        
        Uio->uio_offset += bytes_written;
        Uio->uio_resid  -= bytes_written;
    }

ErrorExit:
    xenevent_mutex_release( g_state.write_lock );
    return rc;
}
