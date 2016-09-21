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
#include "xenevent_comms.h"

#include "xenevent_netbsd.h"
#include "xenevent_minios.h"

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

typedef struct _xen_dev_state
{
    //
    // mini-os/semaphore.h
    //
    // This is used to signal between the xen event channel callback
    // the the thread that is waiting on a read() to complete.
    //
    xenevent_semaphore_t messages_available;

    // XCXXXXXXXXXXXXXX move to xenevent_comms.c ????????
    //
    // Only one thread can read from the device 
    //
    xenevent_mutex_t read_lock;

    //
    // Only one thread can write to the device 
    //
    xenevent_mutex_t write_lock;
} xen_dev_state_t;

static xen_dev_state_t g_state;


// TODO: we don't get or establish meaningful major/minor numbers here
// or in the open function. Fix this.
int
xe_dev_init( void )
{
    int rc = 0;
    devmajor_t cmaj = NODEVMAJOR;
    devmajor_t bmaj = NODEVMAJOR;
    devminor_t cmin = 0;
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
//    DEBUG_BREAK();

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

    rc = xenevent_semaphore_init( &g_state.messages_available );
    if ( 0 != rc )
    {
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
    
    rc = xe_comms_init( g_state.messages_available );
ErrorExit:
    return rc;
}

int
xe_dev_fini( void )
{
    int rc = 0;

    xe_comms_fini();
    
    xenevent_semaphore_destroy( &g_state.messages_available );

    xenevent_mutex_destroy( &g_state.read_lock );
    xenevent_mutex_destroy( &g_state.write_lock );
    
    return rc;
}
    

static int
xe_dev_open( dev_t Dev,
               int Flags,
               int Fmt,
               struct lwp * Lwp )
{
    int rc = 0;
    DEBUG_PRINT("Opened device=%p, Flags=%x Fmt=%x Lwp=%p\n",
                (void *)Dev, Flags, Fmt, Lwp);

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

    return rc;
}

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

    // Only one reader at a time
    xenevent_mutex_wait( g_state.read_lock );
    
    for ( int i = 0; i < Uio->uio_iovcnt; i++ )
    {
        DEBUG_PRINT( "Read request: %d bytes at %p\n",
                     (int)Uio->uio_iov[i].iov_len, Uio->uio_iov[i].iov_base );
    }

    rc = xe_comms_read_item( Uio->uio_iov[0].iov_base,
                             Uio->uio_iov[0].iov_len );

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

    // Wait for a command to arrive
    rc = xe_comms_write_item( Uio->uio_iov[0].iov_base,
                              Uio->uio_iov[0].iov_len );
    
    for ( int i = 0; i < Uio->uio_iovcnt; i++ )
    {
        DEBUG_PRINT( "Write request: %d bytes at %p\n",
                     (int)Uio->uio_iov[i].iov_len, Uio->uio_iov[i].iov_base );
        hex_dump( "Write request",
                  Uio->uio_iov[i].iov_base, (int)Uio->uio_iov[i].iov_len );
    }

    xenevent_mutex_release( g_state.write_lock );
    
    return rc;
}
