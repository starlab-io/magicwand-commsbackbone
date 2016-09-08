/*
// BEGIN headers: platform/xen/init.c
#include <bmk-rumpuser/core_types.h>

#include <mini-os/types.h>
#include <mini-os/hypervisor.h>
#include <mini-os/kernel.h>
#include <mini-os/xenbus.h>
#include <xen/xen.h>

#include <bmk-core/mainthread.h>
#include <bmk-core/platform.h>
#include <bmk-core/printf.h>
// END headers: platform/xen/init.c
*/

// BEGIN headers netbsd/sys/rump/dev/lib/librnd/rnd_component.c
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: rnd_component.c,v 1.5 2016/05/30 14:52:06 pooka Exp $");

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/rnd.h>
#include <sys/stat.h>

#include <rump-sys/kern.h>
#include <rump-sys/dev.h>
#include <rump-sys/vfs.h>

#include "ioconf.h"

// END headers netbsd/sys/rump/dev/lib/librnd/rnd_component.c


#include "xenevent_common.h"
//#include "xen_iface.h"
#include "xenevent_device.h"

//#include <rump-sys/kern.h>
//#include <rump-sys/vfs.h>


#if 0 // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
  // my try
#include <sys/cdefs.h>
//__KERNEL_RCSID(0, "$Id:$" );

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/stat.h>

#include <rump-sys/kern.h>
#include <rump-sys/vfs.h>

// auto-generated from .ioconf file
#include "ioconf.c"

/*
#include <sys/types.h>
//#include <sys/module.h>
//#include <sys/systm.h>  // uprintf 
//#include <sys/param.h>  // defines used in kernel.h 
//#include <sys/kernel.h> // types used in module initialization 
//#include <sys/conf.h>   // cdevsw struct 
//#include <sys/uio.h>    // uio struct 
//#include <sys/malloc.h>

//#include <mini-os/offer_accept_gnt.h>
*/

// Exclude certain header files which result in macro redefinition

// ???
//#define _BMK_CORE_QUEUE_H_
//#define MINIOS_QUEUE_H__ // xenbus expects minios-specific defns
//#define _QUEUE_H_
#define _SYS_QUEUE_H_ // sys/queue.h
//#define _ARCH_MM_H_ // arch/mini-os/machine/mm.h
// no console - results in compilation error
// (struct xenbus_event_queue not defined)
#define _MINIOS_LIB_CONSOLE_H_


//#include <mini-os/os.h>
//#include <mini-os/mm.h> // requires PAGE_SIZE

//#include <xen/sched.h>
#include <mini-os/xenbus.h> // xenbus_transaction_start
//#include <mini-os/events.h>

#include <mini-os/gnttab.h>
#include <mini-os/gntmap.h>

//#include <bmk-core/string.h>
//#include <bmk-core/printf.h>
//#include <bmk-core/memalloc.h>
//#include <bmk-core/pgalloc.h>
//#include <rump-sys/kern.h>
//#include <rump-sys/vfs.h>
#endif


/////////////////////////////////////////////

//MALLOC_DECLARE(M_XENEVENTBUF);
//MALLOC_DEFINE(M_XENEVENTBUF, "xeneventbuffer",
//              "buffer for xenevent module");


RUMP_COMPONENT(RUMP_COMPONENT_DEV)
{
//    const struct cdevsw xenevent_cdevsw;
//    devmajor_t bmaj, cmaj;
    int error;

//    config_init_component(cfdriver_ioconf_xenevent,
//                          cfattach_ioconf_xenevent, cfdata_ioconf_xenevent);
/*
    bmaj = cmaj = NODEVMAJOR;
    error = devsw_attach( DEVICE_NAME,
                          NULL,
                          &bmaj,
                          &xenevent_cdevsw,
                          &cmaj);
    if ( 0 != error )
    {
        panic("xenevent devsw attach failed: %d", error);
    }

    error = rump_vfs_makedevnodes( S_IFCHR,
                                   DEVICE_PATH,
                                   '0',
                                   cmaj,
                                   XENEVENT_DEVICE,
                                   4);
    if ( 0 != error )
    {
        panic("cannot create xenevent device nodes: %d", error);
    }

    // Needed?
    error = rump_vfs_makesymlink("xenevent0", "/dev/xenevent");
    if ( 0 != error )
    {
        panic("cannot create xenevent symlink: %d", error);
    }

*/
    error = xenevent_device_init();
    if ( 0 != error )
    {
        DEBUG_BREAK();
    }
}

/*
//
// This function is called by the kld[un]load(2) system calls to
// determine what actions to take when a module is loaded or unloaded.
//
static int
xenevent_loader(struct module *Module, int Action, void *Arg)
{
    int error = 0;

    switch (what) {
    case MOD_LOAD:                // kldload 
        error = make_dev_p(MAKEDEV_CHECKNAME | MAKEDEV_WAITOK,
                           &xenevent_dev,
                           &xenevent_cdevsw,
                           0,
                           UID_ROOT,
                           GID_WHEEL,
                           0600,
                           "xenevent");
        if (0 != error )
        {
            printf( "make_dev_p failed: %d\n", error );
            goto ErrorExit;
        }

        // meaningless - just use the symbol
        xenbus_transaction_t txn;
        char                *err;

        err = xenbus_transaction_start(&txn);

        printf("xenevent device loaded.\n");
        break;
    case MOD_UNLOAD:
        destroy_dev(xenevent_dev);
        printf("xenevent device unloaded.\n");
        break;
    default:
        error = EOPNOTSUPP;
        break;
    }

ErrorExit:
    return error;
}
*/

/*
static int
xenevent_open(struct cdev *Device __unused,
              int Flags __unused,
              int Type __unused,
              struct thread *Thread __unused)
{
    int error = 0;

    uprintf("Opened device \"xenevent\" successfully.\n");
    return error;
}

static int
xenevent_close(struct cdev *Device __unused,
               int Flag __unused,
               int Devtype __unused,
               struct thread *td __unused)
{

    uprintf("Closing device \"xenevent\".\n");
    return (0);
}

//
// The read function just takes the buf that was saved via
// xenevent_write() and returns it to userland for accessing.
// uio(9)
//

static int
xenevent_read(struct cdev *Device __unused,
              struct uio *Uio,
              int Flag __unused)
{
    size_t amt;
    int error = 0;

    printf( __FUNCTION__ "\n" );
    printf( "xenevent_read\n" );
    return error;
}

//
// * xenevent_write takes in a character string and saves it
// * to buf for later accessing.

static int
xenevent_write(struct cdev *Device __unused,
               struct uio *Uio,
               int Flag __unused)
{
    size_t amt;
    int error = 0;
    printf( __FUNCTION__ "\n" );
    return error;
}


//DEV_MODULE(xenevent, xenevent_loader, NULL);
*/
