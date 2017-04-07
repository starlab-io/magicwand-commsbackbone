/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

/**
 * @file    mwcomms-xen-init.c
 * @author  Mark Mason, Matt Leinhos
 * @date    2 March 2017
 * @version 0.2
 * @brief   Linux kernel module to support MagicWand comms to/from INS.
 *
 * This is the Magic Wand driver (Linux kernel module, or LKM) for the
 * protected virtual machine (PVM). It supports
 * multithreading/multiprocessing - e.g a multi-threaded application
 * above it can read/write to its device, and each request (write) can
 * expect to receive the corresponding response (from read).
 *
 * The LKM supports a handshake with another Xen virtual machine, the
 * unikernel agent (implemented on the Rump unikernel). The handshake
 * involves discovering each others' domain IDs, event channels, and
 * sharing memory via grant references.
 *
 * The LKM supports the usage of an underlying Xen ring buffer. The
 * LKM writes requests on to the ring buffer, and reads responses off
 * the ring buffer. The LKM does not assume that a response following
 * a request will correspond to that last request.
 *
 * The multithreading support works as follows:
 *
 * (1) A user-mode program writes a request to the LKM's
 *     device. The LKM assigns a driver-wide unique ID to that
 *     request and associates the caller's PID with the request ID via
 *     a connection mapping.
 *
 * (2) The user-mode program reads a response from the LKM. The LKM
 *     will cause that read() to block until it receives the response
 *     with an ID that matches the ID of the request sent by that
 *     program. The LKM provides a kernel thread that reads responses
 *     off the ring buffer and notifies blocked threads that a
 *     response has arrived. A user-mode program is not required to
 *     wait for a response; in that case the LKM will receive the
 *     response and destroy it.
 *
 * This model implies some strict standards:
 *
 * - The remote side *must* send a response for every request it
 *   receives during normal (vs. rundown) operation
 * 
 *  - The programs that use this LKM must be well-written: upon
 *    writing a request, they must indicate to the LKM whether or
 *    not they will read a response. Failure to do this correctly can
 *    result in kernel memory leaks until the LKM is unloaded.
 *
 * This LKM backs each Magic Wand socket (mwsocket) with a kernel file
 * object to enable integration with the kernel's VFS system. This
 * provide important functionality that programs make use of; in
 * particular:
 *
 * - release() allows for clean destruction of an mwsocket upon
 *   program termination,
 *
 * - poll() allows for seemless usage of select(), poll() and epoll.
 *
 * Moreover, mwsockets implement these callbacks, which are used by
 * the user-mode shim:
 *
 * - write() for putting a request on the ring buffer
 *
 * - read() for getting a response off the ring buffer
 *
 * - ioctl() for modifying the bahavior of an mwsocket, i.e. mimicking
 *   the functionality of setsockopt() and fcntl()
 *
 * This LKM is structured such that this object, mwcomms-base, servers
 * as the main entry point. It initializes its own kernel device and
 * then asks the Xen interface (see mwcomms-xen-iface) to initialize,
 * which in turn waits for an INS client to appear and completes a
 * handshake with it. Once the handshake is done, a shared memory
 * buffer will have been established and the mwsocket subsystem
 * (mwcomms-socket) is notified. In turn, it initializes the Xen ring
 * buffer.
 *
 * When a process wants to create a new mwsocket, it sends an IOCTL to
 * the main LKM device. That causes mwcomms-socket to create a new
 * mwsocket, backed by a file object, in the calling process, and
 * return the mwsocket's new file descriptor. Thereafter the process
 * interacts with that file descriptor to perform IO on the
 * mwsocket. Closing the file descriptor will cause the release()
 * callback in mwcomms-socket to be invoked, thus destroying the
 * mwsocket.
 *
 * The core functionality of this LKM is found in mwcomms-socket. See
 * mwcomms-socket.c for more info.
 *
 *
 * Here's a visualization of the LKM:
 *
 *                              Application-initiated request
 *                              -----------------------------
 *  /dev/mwcomms
 *  +----------+
 *  | open     | <--------------- Init: open /dev/mwcomms
 *  +----------+
 *  | release  | <--------------- Shutdown: close /dev/mwcomms
 *  +----------+
 *  | ioctl    | <--------------- Check whether FD is for mwsocket, or
 *  +----------+                   create new mwsocket
 *       |
 *       +----------------------------+
 *                 (new mwsocket)     |
 *                                    |
 *                                    |
 * Application-initiated              |         Application/Kernel-initiated
 * ---------------------              v         ----------------------------
 *                                 mwsocket
 *                                +---------+
 * send request --------------->  | write   |
 *                                +---------+
 * get response for request --->  | read    |
 *                                +---------+
 * modify mwsocket behavior --->  | ioctl   |
 *                                +---------+
 *                                | poll    | <------ select/poll/epoll
 *                                +---------+
 *                                | release | <------ close last ref to underlying 
 *                                +---------+          file object
 *
 * The significant advantages to this design can be seen in the
 * diagram: the kernel facilities are leveraged to support (1) polling
 * and (2) mwsocket destruction, even in the case of process
 * termination. This alleviates the LKM from those burdens.
 */

// The device will appear under /dev using this value
#define DEVICE_NAME "mwcomms"

// The device class -- this is a character device driver
#define  CLASS_NAME  "mw"        

#include "mwcomms-common.h"

#include <linux/init.h>           
#include <linux/module.h>         
#include <linux/device.h>         
#include <linux/kernel.h>         
#include <linux/err.h>
#include <linux/fs.h>             
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/kthread.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <asm/uaccess.h>          
#include <linux/time.h>

#include <linux/list.h>

#include <linux/file.h>

#include <xen/grant_table.h>
#include <xen/page.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/interface/callback.h>
#include <xen/interface/io/ring.h>

#include <message_types.h>
#include <xen_keystore_defs.h>

#include "mwcomms-xen-iface.h"
#include "mwcomms-socket.h"

#include "mwcomms-ioctls.h"

// In case of rundown, how many times will we await a response across
// interrupts?
#define MW_INTERNAL_MAX_WAIT_ATTEMPTS 2

// Is the remote side checking for Xen events?
#define MW_DO_SEND_RING_EVENTS 0

#define MW_RUNDOWN_DELAY_SECS 1

/******************************************************************************
 * Globals scoped to this C module - put in struct for flexibility
 *****************************************************************************/

typedef struct _mwcomms_base_globals {
    int             dev_major_num;
    struct class  * dev_class;
    struct device * dev_device;

    // Shared memory
    mw_region_t     xen_shmem;
    // Indicates that the memory has been shared
    struct completion ring_shared;

    // XXXX: list later (?)
    domid_t         client_domid;
    domid_t         my_domid;

    atomic_t        user_ct;
} mwcomms_base_globals_t;

static mwcomms_base_globals_t g_mwcomms_state;


/******************************************************************************
 * Function prototypes and kernel registration points.
 *****************************************************************************/

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark Mason, Matt Leinhos");
MODULE_DESCRIPTION("A driver to support MagicWand's INS");
MODULE_VERSION("0.2");


// File operations supported by the driver

static int
mwbase_dev_open(struct inode *Inode,
                struct file * File);

static int
mwbase_dev_release(struct inode *Inode,
                   struct file * File);

static long
mwbase_dev_ioctl( struct file  * File,
                  unsigned int   Cmd,
                  unsigned long  Arg );

static struct file_operations
mwcomms_fops =
{
    owner           : THIS_MODULE,
    open            : mwbase_dev_open,
    unlocked_ioctl  : mwbase_dev_ioctl,
    release         : mwbase_dev_release
};


static int
mwbase_dev_init( void );

static void
mwbase_dev_fini( void );

module_init(mwbase_dev_init);
module_exit(mwbase_dev_fini);


//static int
//mwbase_dev_client_ready_cb( domid_t ClientId );


/******************************************************************************
 * Function definitions
 *****************************************************************************/

// Callback for the completion of a handshake with a client
static mw_xen_init_complete_cb_t mwbase_client_ready_cb;

static int
mwbase_client_ready_cb( domid_t ClientId )
{
    mwsocket_notify_ring_ready();
    complete( &g_mwcomms_state.ring_shared );
    return 0;
}


static int
mwbase_dev_init( void )
{
    int rc = 0;

    pr_debug("Initializing\n");

    bzero( &g_mwcomms_state, sizeof(g_mwcomms_state) );

    struct module * mod = (struct module *) THIS_MODULE;
    // gdb> add-symbol-file char_driver.ko $eax/$rax
    pr_info( "\n################################\n"
             "%s.ko @ 0x%p\n"
             "################################\n",
             DRIVER_NAME, mod->core_layout.base );
             //DRIVER_NAME, mod->module_core );

#ifdef 0 // MYTRAP // GDB helper - emits a breakpoint!
   asm( "int $3" // module base in *ax
        //:: "a" ((THIS_MODULE)->module_core));
        :: "a" ((THIS_MODULE)->core_layout.base)
        , "b" ((THIS_MODULE)->init_layout.base)
        , "c" (mod) );
#endif

   //
   // Dynamically allocate a major number for the device
   //

   g_mwcomms_state.dev_major_num =
       register_chrdev( 0, DEVICE_NAME, &mwcomms_fops );

   if (  g_mwcomms_state.dev_major_num < 0)
   {
       rc =  g_mwcomms_state.dev_major_num;
       pr_err( "register_chrdev failed: %d\n", rc );
       goto ErrorExit;
   }

   // Register the device class
   g_mwcomms_state.dev_class = class_create( THIS_MODULE, CLASS_NAME );
   if (IS_ERR( g_mwcomms_state.dev_class ) )
   {
       rc = PTR_ERR( g_mwcomms_state.dev_class );
       pr_err( "class_create failed: %d\n", rc );
       goto ErrorExit;
   }

   // Register the device driver
   g_mwcomms_state.dev_device =
       device_create( g_mwcomms_state.dev_class,
                      NULL,
                      MKDEV( g_mwcomms_state.dev_major_num, 0 ),
                      NULL,
                      DEVICE_NAME);
   if ( IS_ERR( g_mwcomms_state.dev_device ) )
   {
       rc = PTR_ERR( g_mwcomms_state.dev_device );
       pr_err( "device_create failed: %d\n", rc );
       goto ErrorExit;
   }

   // Get shared memory in an entire, zeroed block.
   // XXXX: break apart shmem later for multiple clients
   g_mwcomms_state.xen_shmem.ptr = (void *)
       __get_free_pages( GFP_KERNEL | __GFP_ZERO,
                         XENEVENT_GRANT_REF_ORDER );
   if ( NULL == g_mwcomms_state.xen_shmem.ptr )
   {
       pr_err( "Failed to allocate 0x%x pages\n", XENEVENT_GRANT_REF_COUNT );
       rc = -ENOMEM;
       goto ErrorExit;
   }

   g_mwcomms_state.xen_shmem.pagect = XENEVENT_GRANT_REF_COUNT;

   pr_info( "Ring buffer has 0x%x pages. Total size is 0x%lx bytes.\n",
            XENEVENT_GRANT_REF_COUNT, XENEVENT_GRANT_REF_COUNT * PAGE_SIZE );
   
   // The socket iface is invoked when the ring sharing is
   // complete. Therefore, init it before the Xen iface.
   rc = mwsocket_init( &g_mwcomms_state.xen_shmem );
   if ( rc )
   {
       goto ErrorExit;
   }
   
   // Init the Xen interface. The callback will be invoked when client
   // handshake is complete.

   init_completion( &g_mwcomms_state.ring_shared );
   rc = mw_xen_init( &g_mwcomms_state.xen_shmem,
                     mwbase_client_ready_cb,
                     mwsocket_event_cb );
   if ( rc )
   {
       goto ErrorExit;
   }

ErrorExit:
   if ( rc )
   {
       mwbase_dev_fini();
   }

   return rc;
}

static void
mwbase_dev_fini( void )
{
    pr_debug( "Unloading...\n" );

    // Tear down open mwsockets and mwsocket subsystem
    mwsocket_fini();

    // Destroy state related to xen, including grant refs
    mw_xen_fini();

    if ( NULL != g_mwcomms_state.xen_shmem.ptr )
    {
        free_pages( (unsigned long) g_mwcomms_state.xen_shmem.ptr,
                    XENEVENT_GRANT_REF_ORDER );
    }
    
    // Tear down device
    if ( g_mwcomms_state.dev_major_num >= 0 )
    {
        device_destroy( g_mwcomms_state.dev_class,
                        MKDEV( g_mwcomms_state.dev_major_num, 0 ) );
    }

    if ( NULL != g_mwcomms_state.dev_class )
    {
        class_destroy( g_mwcomms_state.dev_class );
        class_unregister( g_mwcomms_state.dev_class );
    }

    if ( NULL != g_mwcomms_state.dev_device )
    {
        unregister_chrdev( g_mwcomms_state.dev_major_num, DEVICE_NAME );
    }

    pr_debug("cleanup is complete\n");
}


static int
mwbase_dev_open(struct inode *Inode,
                struct file * File)
{
    pr_debug( "Processing open()\n" );
    atomic_inc( &g_mwcomms_state.user_ct );
    return 0;
}


static int
mwbase_dev_release(struct inode *Inode,
                   struct file * File)
{
    pr_debug( "Processing release()\n" );
    atomic_dec( &g_mwcomms_state.user_ct );
    return 0;
}


/**
 * @brief Main interface to the mwsocket system, used for mwsocket and
 * checking.
 */
static long
mwbase_dev_ioctl( struct file  * File,
                  unsigned int   Cmd,
                  unsigned long  Arg )
{
    int rc = 0;

    switch( Cmd )
    {
    case MW_IOCTL_CREATE_SOCKET:
    {
        mwsocket_create_args_t create;
        rc = copy_from_user( &create, (void *)Arg, sizeof(create) );
        if ( rc )
        {
            MYASSERT( !"Invalid memory provided\n" );
            rc = -EFAULT;
            goto ErrorExit;
        }

        rc = mwsocket_create( &create.outfd,
                               create.domain,
                               create.type,
                               create.protocol );
        if ( rc ) goto ErrorExit;

        // Copy the resulting FD back to the user
        rc = copy_to_user( &((mwsocket_create_args_t *)Arg)->outfd,
                           &create.outfd,
                           sizeof(create.outfd) );
        if ( rc )
        {
            MYASSERT( !"Invalid memory provided\n" );
            rc = -EFAULT;
            goto ErrorExit;
        }
        break;
    }
    case MW_IOCTL_IS_MWSOCKET:
    {
        mwsocket_verify_args_t verify;
        struct file * file = NULL;
        rc = copy_from_user( &verify, (void *)Arg, sizeof(verify) );
        if ( rc )
        {
            MYASSERT( !"Invalid memory provided\n" );
            rc = -EFAULT;
            goto ErrorExit;
        }

        file = fget( verify.fd );
        if ( NULL == file )
        {
            pr_err( "User passed bad file descriptor %d\n", verify.fd );
            rc = -EBADFD;
            MYASSERT( !"Bad FD" );
            goto ErrorExit;
        }

        verify.is_mwsocket = mwsocket_verify( file );

        fput( file );

        // Copy the answer to the user
        rc = copy_to_user( &((mwsocket_verify_args_t *)Arg)->is_mwsocket,
                           &verify.is_mwsocket,
                           sizeof(verify.is_mwsocket) );
        if ( rc )
        {
            MYASSERT( !"Invalid memory provided\n" );
            rc = -EFAULT;
            goto ErrorExit;
        }

        break;
    }
    default:
        pr_err( "Received invalid IOCTL code\n" );
        rc = -EINVAL;
        goto ErrorExit;
    }

ErrorExit:
    return rc;
}
