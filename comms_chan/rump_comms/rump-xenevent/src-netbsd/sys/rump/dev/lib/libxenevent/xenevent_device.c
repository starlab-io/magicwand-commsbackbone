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

#include <sys/vfs_syscalls.h>

#include <rump-sys/kern.h>
#include <rump-sys/vfs.h>

#include "xenevent_common.h"
#include "xenevent_device.h"
#include "xen_comms.h"

#include "xenevent_netbsd.h"

#include "ioconf.h"

// Function prototypes 

// src-netbsd/sys/sys/conf.h
static dev_type_open ( xe_dev_open  );
static dev_type_close( xe_dev_close );
static dev_type_read ( xe_dev_read  );
static dev_type_write( xe_dev_write );

// Character device entry points 
static struct cdevsw
xe_cdevsw = {
    .d_open    = xe_dev_open,
    .d_close   = xe_dev_close,
    .d_read    = xe_dev_read,
    .d_write   = xe_dev_write,

    .d_ioctl   = noioctl,
    .d_stop    = nostop,
    .d_tty     = notty,
    .d_poll    = nopoll,
    .d_mmap    = nommap,
    .d_kqfilter = nokqfilter,
    .d_discard = nodiscard,
    .d_flag    = 0 //D_OTHER | D_MPSAFE
};

// TODO: we don't get or establish meaningful major/minor numbers here
// or in the open function. Fix this.
int
xe_dev_init( void )
{
    int err = 0;
    devmajor_t cmaj = NODEVMAJOR;
    devmajor_t bmaj = NODEVMAJOR;
    devminor_t cmin = 0;
    // Attach driver to device
    // See driver for /dev/random for example
/*
    err = config_init_component( cfdriver_ioconf_xenevent,
                                 cfattach_ioconf_xenevent,
                                 cfdata_ioconf_xenevent);
    if ( 0 != err )
    {
        MYASSERT( !"Failed to attach driver" );
        goto ErrorExit;
    }
*/
    
    cmaj = cdevsw_lookup_major(&xe_cdevsw);
//    DEBUG_BREAK();

    err = devsw_attach( DEVICE_NAME,
                        NULL, // block device info
                        &bmaj,
                        &xe_cdevsw, // char device info
                        &cmaj );
    if ( 0 != err )
    {
        MYASSERT( !"Failed to attach driver" );
        goto ErrorExit;
    }

    // Make the character device
    err = rump_vfs_makeonedevnode( S_IFCHR,
                                   DEVICE_PATH,
                                   cmaj,
                                   cmin );
    if ( 0 != err )
    {
        MYASSERT( !"Failed to create control device" );
        goto ErrorExit;
    }

    err = xe_comms_init();
    
//    DEBUG_BREAK();

ErrorExit:
    return err;
}

int
xe_dev_fini( void )
{
    int err = 0;

    return err;
}
    

static int
xe_dev_open( dev_t Dev,
               int Flags,
               int Fmt,
               struct lwp * Lwp )
{
    int error = 0;
    DEBUG_PRINT("Opened device=%p, Flags=%x Fmt=%x Lwp=%p\n",
                (void *)Dev, Flags, Fmt, Lwp);
//    DEBUG_BREAK();
    return error;
}

static int
xe_dev_close( dev_t Dev,
              int Flags,
              int Fmt,
              struct lwp * Lwp )
{
    int error = 0;
    DEBUG_PRINT("Closed device=%p, Flags=%xx Fmt=%x Lwp=%p\n",
                (void *)Dev, Flags, Fmt, Lwp);
//    DEBUG_BREAK();
    return error;
}

//
// The read function blocks until an event for this thread is
// delivered. It then 
//

static int
xe_dev_read( dev_t Dev,
             struct uio * Uio,
             int Flag )
{
    int error = 0;

    for ( int i = 0; i < Uio->uio_iovcnt; i++ )
    {
        DEBUG_PRINT( "Read request: %d bytes at %p\n",
                     (int)Uio->uio_iov[i].iov_len, Uio->uio_iov[i].iov_base );
    }

    DEBUG_BREAK();
    // If we previously had 0 outstanding reads, then enable events
//    if ( atomic_inc_32_nv( &g_outstanding_reads ) > 0 )
//    {
//        xen_comms_enable_events();
//    }
    
    // TODO: use a meaningful ID (arg1)
    error = xe_comms_read_data( (event_id_t) 1,
                                Uio->uio_iov[0].iov_base,
                                Uio->uio_iov[0].iov_len );

    // If we previously had 0 outstanding reads, then enable events
//    if ( atomic_inc_32_nv( &g_outstanding_reads ) > 0 )
//    {
//        xen_comms_enable_events();
//    }

    
    return error;
}


static int
xe_dev_write( dev_t Dev,
              struct uio * Uio,
              int Flag )
{
    int error = 0;

    for ( int i = 0; i < Uio->uio_iovcnt; i++ )
    {
        DEBUG_PRINT( "Write request: %d bytes at %p\n",
                     (int)Uio->uio_iov[i].iov_len, Uio->uio_iov[i].iov_base );
        hex_dump( "Write request",
                  Uio->uio_iov[i].iov_base, (int)Uio->uio_iov[i].iov_len );
    }
    
    return error;
}
