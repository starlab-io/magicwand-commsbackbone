// Define a character device

#include <sys/cdefs.h>
//__KERNEL_RCSID(0, "$NetBSD: rndpseudo.c,v 1.35 2015/08/20 14:40:17 christos Exp $");

#if defined(_KERNEL_OPT)
#include "opt_compat_netbsd.h"
#endif

#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/conf.h>
//#include <sys/cprng.h>
#include <sys/cpu.h>
#include <sys/evcnt.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/ioctl.h>
#include <sys/kauth.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/percpu.h>
#include <sys/poll.h>
#include <sys/pool.h>
#include <sys/proc.h>
#include <sys/rnd.h>
#include <sys/rndpool.h>
#include <sys/rndsource.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/systm.h>

#include <sys/vnode.h>

#include <sys/vfs_syscalls.h>

#include <rump-sys/kern.h>
#include <rump-sys/vfs.h>

#include "xenevent_common.h"
#include "xenevent_device.h"
#include "xen_comms.h"

// Function prototypes 


// src-netbsd/sys/sys/conf.h
static dev_type_open ( xenevent_open  );
static dev_type_close( xenevent_close );
static dev_type_read ( xenevent_read  );
static dev_type_write( xenevent_write );



// Character device entry points 
static struct cdevsw xenevent_cdevsw = {
    .d_open    = xenevent_open,
    .d_close   = xenevent_close,
    .d_read    = xenevent_read,
    .d_write   = xenevent_write,
};


//dev_type_open( xenevent_open );

int
xenevent_device_init( void )
{
    int err = 0;
    int  bmaj = -1, cmaj = -1;

    // Attach driver to device
    // See 
    /*error = config_init_component(cfdriver_ioconf_dtv,
		    cfattach_ioconf_dtv, cfdata_ioconf_dtv);
		if (error)
			return error;
		error = devsw_attach("dtv", NULL, &bmaj, &dtv_cdevsw, &cmaj);
		if (error)
			config_fini_component(cfdriver_ioconf_dtv,
			    cfattach_ioconf_dtv, cfdata_ioconf_dtv);
                            return error; */

DEBUG_BREAK();
    err = devsw_attach( DEVICE_NAME,
                        NULL,
                        &bmaj,
                        &xenevent_cdevsw,
                        &cmaj );
    if ( 0 != err )
    {
        MYASSERT( !"Failed to attach driver" );
        goto ErrorExit;
    }

    err = rump_vfs_makedevnodes( S_IFCHR,
                                 DEVICE_PATH,
                                 0,
                                 cmaj,
                                 0,
                                 1 );
    if ( 0 != err )
    {
        MYASSERT( !"Failed to create control device" );
        goto ErrorExit;
    }

ErrorExit:
    return err;
}

int
xenevent_device_fini( void )
{
    int err = 0;

    return err;
}
    

static int
xenevent_open( dev_t Dev,
               int Flags,
               int Fmt,
               struct lwp * Lwp )
//static int
//xenevent_open(struct cdev *Device __unused,
//              int Flags __unused,
//              int Type __unused,
//              struct lwp *Lwp __unused ) // <<< OR...
              //struct thread *Thread __unused)
{
    int error = 0;
    DEBUG_PRINT("Opened device \"xenevent\" successfully.\n");
    return error;
}

static int
xenevent_close( dev_t Dev,
                int Flags,
                int Fmt,
                struct lwp * Lwp )
//static int
//xenevent_close(struct cdev *Device __unused,
//               int Flag __unused,
//               int Devtype __unused,
//               struct lwp *Lwp __unused ) // <<< OR...
//               // struct thread *td __unused)
{
    DEBUG_PRINT("Closing device \"xenevent\".\n");
    return (0);
}

//
// The read function just takes the buf that was saved via
// xenevent_write() and returns it to userland for accessing.
// uio(9)
//

static int
xenevent_read( dev_t Dev,
               struct uio * Uio,
               int Flag )
//static int
//xenevent_read(struct cdev *Device __unused,
//              struct uio *Uio,
//              int Flag __unused)
{
    int error = 0;

    DEBUG_PRINT( "%s\n", __FUNCTION__ );
    DEBUG_PRINT( "here\n" );
    return error;
}

//
// * xenevent_write takes in a character string and saves it
// * to buf for later accessing.
static int
xenevent_write( dev_t Dev,
                struct uio * Uio,
                int Flag )

//static int
//xenevent_write(struct cdev *Device __unused,
//               struct uio *Uio,
//               int Flag __unused)
{
    int error = 0;
DEBUG_PRINT( "%s\n", __FUNCTION__ );
DEBUG_BREAK();
//    DEBUG_PRINT( __FUNCTION__ "\n" );
    return error;
}
