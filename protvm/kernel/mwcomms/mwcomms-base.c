/*************************************************************************
 * STAR LAB PROPRIETARY & CONFIDENTIAL
 * Copyright (C) 2018, Star Lab — All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited.
 ***************************************************************************/

/**
 * @file    mwcomms-base.c
 * @author  Mark Mason, Matt Leinhos
 * @date    16 January 2018
 * @version 0.2
 * @brief   Linux kernel module to support MagicWand comms to/from INS.
 *
 * Introduction
 * -------------
 *
 * This is the MagicWand driver (Linux kernel module, or LKM) for the
 * protected virtual machine (PVM). It facilitates the passing of
 * requests from a protected application to one or more Isolated
 * Network Stacks (INSs, currently backed by Rump unikernels on the
 * same hardware). It also facilitates the delivery of a response,
 * produced by an INS, to the protected application that expects
 * it. This LKM is intended to be exercised by a custom shared object
 * ("shim") that the protected application loads via LD_PRELOAD. The
 * shim is available in this repo.
 *
 * The LKM supports multithreading / multiprocessing - e.g a
 * multi-threaded application above it can read/write to its device,
 * and each request (write) can expect to receive the corresponding
 * response (via read). It is designed to be fast, with expected
 * slowdowns in the 10s of milliseconds relative to native speeds.
 *
 * The LKM supports a handshake with another Xen virtual machine, the
 * INS unikernel agent (implemented on the Rump unikernel). The
 * handshake involves discovering each others' domain IDs, event
 * channels, and sharing memory via grant references.
 *
 * The LKM supports the usage of an underlying Xen ring buffer. The
 * LKM writes requests to the ring buffer, and reads responses from
 * it. The LKM makes no assumptions about the ordering of responses.
 * The code that interacts with the underlying Xen ring buffer and the
 * Xen event channel is the busiest and most performance-critical part
 * of the system, and is found in mwcomms-xen-iface.c. The sending of
 * an event on the event channel wakes up a thread in the INS, which
 * then reads the next request off the ring buffer (the event
 * indicates that the request is available for consumption). Likewise,
 * the LKM has a special thread that reads responses off the ring
 * buffer(s). It blocks on a semaphore which is up()ed by the LKM's
 * event channel handler.
 *
 *
 * Multi-threading
 * ---------------
 *
 * Multithreading support works as follows:
 *
 * (1) A user-mode program writes a request to the LKM's device. The
 *     LKM assigns a driver-wide unique ID to that request and
 *     associates the caller's PID with the request ID via a
 *     connection mapping. Assume that the program indicates it will
 *     wait for a response (it is not required to wait unless it says
 *     it will do so)..
 *
 * (2) The user-mode program reads a response from the LKM. The LKM
 *     will cause that read() to block until it receives the response
 *     with an ID that matches the request's ID. As mentioned earlier,
 *     the LKM provides a kernel thread that reads responses off the
 *     ring buffer and notifies blocked threads when their respective
 *     responses have arrived. If the program had indicated that it
 *     will not wait, then the LKM destroys the response upon receipt
 *     and after some processing.
 *
 * This model implies some strict standards:
 *
 * - The remote side *must* send a response for every request it
 *   receives, although the responses can be in a different order than
 *   the requests.
 * 
 * - The programs that use this LKM must be well-written: upon writing
 *   a request, they must indicate to the LKM whether or not they will
 *   read a response. If the program says it will wait but doesn't,
 *   then the LKM will signal a completion variable and leak the
 *   response until the LKM is unloaded -- the user-provided thread
 *   was supposed to consume the response and destroy it.  The moral
 *   of the story is that the user-mode shim should be correct.
 *
 *
 * Leveraging the Linux kernel's VFS
 * ---------------------------------
 *
 * The first iteration of this driver did not leverage the VFS: it
 * presented the shim with pseudo-file objects. This model was mostly
 * adequate until the system was exercised against a more advanced
 * protected application, Apache. Apache makes extensive use of
 * polling, which in turn meant that the shim and LKM had to include
 * support for MagicWand's version of polling. Moreover, there was a
 * design problem wherein the death of a protected application was not
 * always recognized by the LKM, thereby leaking resources.
 *
 * To alleviate these problems, the LKM was redesigned to leverage VFS
 * and allow the development to focus on MagicWand, rather than
 * re-implementing epoll() and release() support, which had already
 * been done by the Linux community.  With the rewrite, the LKM now
 * backs each Magic Wand socket (mwsocket) with a kernel file object
 * to enable integration with the kernel's VFS. This provides
 * important functionality that programs make use of; in particular:
 *
 * - release() allows for clean destruction of an mwsocket upon
 *   program termination,
 *
 * - poll() allows for seemless usage of select(), poll() and epoll.
 *
 * Moreover, mwsockets implement these operations, which are used by
 * the user-mode shim:
 *
 * - write() for putting a request on the ring buffer
 *
 * - read() for getting a response off the ring buffer
 *
 * - ioctl() for modifying the bahavior of an mwsocket, i.e. mimicking
 *   the functionality of setsockopt() and fcntl()
 *
 * The above benefits provided the primary motivation for creating
 * version 0.2 of the LKM. Without integration into the VFS, the shim
 * has to intercept and translate every poll-related call, including
 * the complex epoll structures.
 *
 * This LKM is structured such that this object, mwcomms-base, serves
 * as the main entry point. It initializes its own kernel device and
 * initializes the Xen subsystem and the netflow channel.
 *
 * When initializing the Xen subsystem, a callback is passed to
 * it. When a new INS is recognized, the subsystem completes a
 * handshake with it to include sharing a block of memory for a ring
 * buffer. Once that is complete, the callback is invoked. That, in
 * turn, notifies the mwsocket subsystem that there is a new INS
 * available for usage. If there are any listening sockets, those will
 * be "replicated" to the new INS to facilitate multi-INS
 * support. N.B. a new INS is recognized on a XenWatch thread, which
 * we cannot cause to block for a long time. Thus the mwsocket
 * subsystem completes socket replication via a work item.
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
 * The core functionality of this LKM is found in mwcomms-socket.
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
 * termination. This alleviates the LKM from those burdens. In a
 * future refactor, this could be simplified so that the mwsocket
 * object uses the main device's functions. In this case, a socket()
 * call would result in a direct opening of /dev/mwcomms for a new
 * file descriptor.
 *
 *
 * Multi-INS support
 * -----------------
 *
 * To facilitate attack mitigation and TCP/IP stack diversification,
 * the driver supports multiple INSs. This feature is considered in
 * beta; there are still some issues to work out.
 *
 * The driver does not create new INSs, nor does it direct that to
 * happen. The INSs publish network traffic statistics to XenStore. An
 * external agent (such as mw_distro_ins.py, in the repo) fulfills the
 * task of creating INSs and deciding which INS to use for inbound
 * connections. In turn, the LKM recognizes the appearance of a new
 * INS and completes a handshake with it. Upon a successful handshake,
 * the INS is registered and is considered available for requests.
 *
 * Once a new INS is registered, any listening sockets must be
 * "replicated" onto it. For instance, say a protected application has
 * requested to listen on 0.0.0.0:80, and INS 1 is doing that on its
 * TCP/IP stack. Then INS 2 appears. To enable multiplexing of the
 * protected application, INS 2 must also listen on that port. The LKM
 * is responsible for telling INS 2 to listen on port 80. It achieves
 * this by creating a work item that crafts the requests (from within
 * the kernel) and sends them to the new INS. It does this via work
 * item because it cannot block the XenStore watching thread.
 *
 * This behavior means that when an inbound connection on port 80
 * could come from any known INS. The LKM must direct that connection
 * (a response to an accept) to the thread that originally wrote the
 * accept request, although it may have sent it to a different INS (or
 * set of INSs). The LKM achieves this capability by maintaining a
 * list of inbound connections for each mwsocket. Upon an inbound
 * connection, the response is put in the list and a special semaphore
 * is up()-ed, which the accept()ing thread is blocking on. Moreover,
 * each "replicated" mwsocket has an associated "primary" mwsocket,
 * which is the one exposed to the user.
 *
 * SDG
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

#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <net/if_inet6.h>

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
#include "mwcomms-netflow.h"

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


typedef struct _mwcomms_base_globals
{
    int             dev_major_num;
    struct class  * dev_class;
    struct device * dev_device;

    struct sockaddr   local_ip;

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


/******************************************************************************
 * Function definitions
 *****************************************************************************/

// Callback for the completion of a handshake with a client
static mw_xen_init_complete_cb_t mwbase_client_ready_cb;

static int
mwbase_client_ready_cb( domid_t Domid )
{
    mwsocket_notify_ring_ready( Domid );
    return 0;
}


/**
 * @brief Find the external-facing IP address. First look for an IPv4
 * address, then look for IPv6.
 */
static int
mwbase_find_extern_ip( void )
{
    int rc = -ENOENT;
    bool found = false;

    bzero( &g_mwcomms_state.local_ip, sizeof( g_mwcomms_state.local_ip ) );

    read_lock(&dev_base_lock);

    struct net_device * dev = first_net_device( &init_net );
    while( NULL != dev )
    {
        if ( 0 != strcmp( dev->name, EXT_IFACE ) ||
             (NULL == dev->ip_ptr ) )
        {
            dev = next_net_device( dev );
            continue;
        }

        struct in_ifaddr * ifa = NULL;
        for ( ifa = dev->ip_ptr->ifa_list; NULL != ifa; ifa = ifa->ifa_next )
        {
            pr_info( "Found address %pI4 on interface %s\n",
                     &ifa->ifa_address, dev->name );
            if ( found )
            {
                MYASSERT( !"External interface has multiple addresses" );
                rc = -EADDRINUSE;
                goto ErrorExit;
            }

            found = true;
            rc = 0;
            //Addr->sin_addr.s_addr = ifa->ifa_address;
            g_mwcomms_state.local_ip.sa_family = AF_INET;
            ((struct sockaddr_in *) &g_mwcomms_state.local_ip)->sin_port = 0;
            ((struct sockaddr_in *) &g_mwcomms_state.local_ip)->sin_addr.s_addr
                = ifa->ifa_address;
        }
        dev = next_net_device( dev );
    } // while

    if ( found ) { goto ErrorExit; }

    // No IPv4 addresses found. Move on to IPv6. UNTESTED!!!
    dev = first_net_device( &init_net );
    while( NULL != dev )
    {
        struct inet6_dev * indev = dev->ip6_ptr;
        if ( 0 != strcmp( dev->name, EXT_IFACE ) ||
             (NULL == indev) )
        {
            dev = next_net_device( dev );
            continue;
        }

        struct inet6_ifaddr * ifa = NULL;
        list_for_each_entry(ifa, &indev->addr_list, if_list)
        {
            pr_info( "Found address %pI6c on interface %s\n",
                     &ifa->addr, dev->name );
            if ( found )
            {
                MYASSERT( !"External interface has multiple addresses" );
                rc = -EADDRINUSE;
                goto ErrorExit;
            }

            found = true;
            rc = 0;
            g_mwcomms_state.local_ip.sa_family = AF_INET6;
            ((struct sockaddr_in6 *) &g_mwcomms_state.local_ip)->sin6_port = 0;
            ((struct sockaddr_in6 *) &g_mwcomms_state.local_ip)->sin6_addr
                = ifa->addr;
        }
        dev = next_net_device( dev );
    } // while

ErrorExit:
    if ( !found )
    {
        MYASSERT( !"Failed to find IP address" );
    }
    read_unlock(&dev_base_lock);
    return rc;
}


static int
MWSOCKET_DEBUG_OPTIMIZE_OFF
mwbase_dev_init( void )
{
    int rc = 0;
    struct module * mod = (struct module *) THIS_MODULE;

    pr_info( "\n################################\n"
             "%s.ko @ 0x%p\n"
             "################################\n",
             DRIVER_NAME, mod->core_layout.base );
             //DRIVER_NAME, mod->module_core );

#ifdef MYTRAP
    // gdb> add-symbol-file mwcomms.ko $eax/$rax

    asm( "int $3" // module base in *ax
         //:: "a" ((THIS_MODULE)->module_core));
         :: "a" ((THIS_MODULE)->core_layout.base)
          , "b" ((THIS_MODULE)->init_layout.base)
          , "c" (mod) );
#endif

    bzero( &g_mwcomms_state, sizeof(g_mwcomms_state) );

    //
    // Dynamically allocate a major number for the device
    //

    g_mwcomms_state.dev_major_num =
        register_chrdev( 0, DEVICE_NAME, &mwcomms_fops );

    if ( g_mwcomms_state.dev_major_num < 0 )
    {
        rc =  g_mwcomms_state.dev_major_num;
        pr_err( "register_chrdev failed: %d\n", rc );
        goto ErrorExit;
    }

    // Register the device class
    g_mwcomms_state.dev_class = class_create( THIS_MODULE, CLASS_NAME );
    if ( IS_ERR( g_mwcomms_state.dev_class ) )
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


   rc = mwbase_find_extern_ip();
   if ( rc )
   {
       pr_warn( "Failed to find external IP address. "
                "Some functionality will be limited\n" );
       rc = 0;
   }

    // The socket iface is invoked when the ring sharing is
    // complete. Therefore, init it before the Xen iface.
    rc = mwsocket_init( &g_mwcomms_state.local_ip );
    if ( rc )
    {
        goto ErrorExit;
    }
   
    // Init the Xen interface. The callback will be invoked when client
    // handshake is complete.

    rc = mw_xen_init( mwbase_client_ready_cb,
                      mwsocket_event_cb );
    if ( rc )
    {
        goto ErrorExit;
    }

   rc = mw_netflow_init( &g_mwcomms_state.local_ip );
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
MWSOCKET_DEBUG_OPTIMIZE_OFF
mwbase_dev_fini( void )
{
    pr_debug( "Unloading...\n" );

    // Tear down open mwsockets and mwsocket subsystem
    mwsocket_fini();

    // Destroy state related to xen, including grant refs
    mw_xen_fini();

    mw_netflow_fini();

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

    pr_info( "Cleanup is complete\n" );
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
MWSOCKET_DEBUG_OPTIMIZE_OFF
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
