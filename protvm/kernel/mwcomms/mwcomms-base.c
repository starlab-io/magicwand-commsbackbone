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
 */


/**
 * This is the Magic Wand driver for the protected virtual machine
 * (PVM). It supports multithreading/multiprocessing - e.g a
 * multi-threaded application above it can read/write to its device,
 * and each request (write) can expect to receive the corresponding
 * response (from read).
 *
 * The driver supports a handshake with another Xen virtual machine,
 * the unikernel agent (implemented on the Rump unikernel). The
 * handshake involves discovering each others' domain IDs, event
 * channels, and sharing memory via grant references.
 *
 * The driver supports the usage of an underlying Xen ring buffer. The
 * driver writes requests on to the ring buffer, and reads responses
 * off the ring buffer. The driver does not assume that a response
 * following a request will correspond to that last request.
 *
 * The multithreading support works as follows:
 *
 * (1) A user-mode program writes a request to the driver's
 *     device. The driver assigns a system-wide unique ID to that
 *     request and associates the caller's PID with the request ID via
 *     a connection mapping.
 *
 * (2) The user-mode program reads a response from the driver. The
 *     driver will cause that read() to block until it receives the
 *     response with an ID that matches the ID of the request sent by
 *     that program. This is implemented by a kernel thread that runs
 *     the consume_response_worker() function.
 *
 * This model implies some strict standards:
 *
 * - The remote side *must* send a response for every request it
 *   receives during normal (vs. rundown) operation
 * 
 *  - The programs that use this driver must be well-written: they
 *    must always read a response for every request they write. This
 *    driver attempts to enforce that behavior.
 *
 * This LKM tracks MW sockets that user applications have opened. If
 * an application doesn't close the socket correctly, this LKM will do
 * so on the application's behalf. This introduces a number of
 * complexities which are addressed, at least, in part. The resources
 * that track open sockets and mappings between apps and requests
 * could be accessed in two ways: (1) the app sends a request via
 * write() and reads a response via read(); (2) the app suddenly calls
 * dev_release() or dies (and the kernel calls dev_release()), in
 * which case the LKM puts the resources associated with the app into
 * "rundown" mode, sends "close" requests over the ring buffer and
 * waits for the responses on behalf of the app.
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

// Record performance metrics?
#define MW_DO_PERFORMANCE_METRICS 0

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
    
#if MW_DO_PERFORMANCE_METRICS

    // Vars for performance tests
    ktime_t        time_start;
    ktime_t        time_end;
    s64            actual_time;

#endif // MW_DO_PERFORMANCE_METRICS
    
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

static mw_xen_init_complete_cb_t mwbase_client_ready_cb;

static int
mwbase_client_ready_cb( domid_t ClientId )
{
    complete( &g_mwcomms_state.ring_shared );
    return 0;
}


static int
mwbase_dev_init( void )
{
    int rc = 0;

    pr_debug("Initializing\n");

    bzero( &g_mwcomms_state, sizeof(g_mwcomms_state) );

#if 0
   struct module * mod = (struct module *) THIS_MODULE;
   // gdb> add-symbol-file char_driver.ko $eax/$rax
   asm( "int $3" // module base in *ax
        //:: "a" ((THIS_MODULE)->module_core));
        :: "a" ((THIS_MODULE)->init_layout.base)
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

   // Init the completion variable that indicates that the ring is
   // shared. Our callback will signal it; the socket subsystem will
   // wait for it.
   init_completion( &g_mwcomms_state.ring_shared );
   
   // Init the Xen interface. The callback will be invoked when client
   // handshake is complete.
   rc = mw_xen_init( &g_mwcomms_state.xen_shmem,
                     mwbase_client_ready_cb,
                     mw_socket_event_cb );
   if ( rc )
   {
       goto ErrorExit;
   }

   rc = mw_socket_init( &g_mwcomms_state.xen_shmem,
                        &g_mwcomms_state.ring_shared );
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
    // Tear down open mwsockets and mwsocket subsystem
    mw_socket_fini();

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
    atomic_inc( &g_mwcomms_state.user_ct );
    return 0;
}


static int
mwbase_dev_release(struct inode *Inode,
                   struct file * File)
{
    atomic_dec( &g_mwcomms_state.user_ct );
    return 0;
}

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

        rc = mw_socket_create( &create.outfd,
                               create.domain,
                               create.type,
                               create.protocol );
        if ( rc )
        {
            goto ErrorExit;
        }

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
            goto ErrorExit;
        }
        
        verify.is_mwsocket = mw_socket_verify( file );

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






#if 0

//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////




//
// Used to track currently open sockets. List anchor is in
// thread_conn_map_t (i.e. sockets are tracked per thread group).
//
typedef struct _open_mw_socket {
    mw_socket_fd_t   mwsockfd;
    struct list_head list;
} open_mw_socket_t;


//
// Used to map a thread to the (single) currently-outstanding
// request. Assumption: no async IO!!
//
typedef struct _thread_response_map {
    mt_id_t message_id;
    
    pid_t pid;
    pid_t tgid; // thread group
    struct task_struct * task;

    bool awaiting_read;

    // Worker thread writes the response here upone receipt, then
    // releases response_pending
    mt_response_generic_t response;

    // Track the open sockets
    struct list_head open_socket_list;
    struct rw_semaphore sock_list_sem;

    // Is the mapping in the rundown state? If true, sock_list_sem is
    // acquired.
    bool                rundown;

    // Lock held between the time a request is issued and the time a
    // response is accepted. First it is down-ed upon a write(), then
    // up-ed upon a read(). It must be held to destroy a thread's open
    // sockets forceably.
    struct semaphore in_flight_lock;

    // Semaphore provides communication between the worker thread and
    // the in-process thread that waits on data.
    struct semaphore response_pending;

    // List link for this mapping. It is either in the active list or
    // the rundown list; never in both.
    struct list_head list;
} thread_response_map_t;

// Slabs for fast allocation 
static struct kmem_cache * open_socket_slab = NULL;
static struct kmem_cache * threadresp_slab  = NULL;


static int    majorNumber = -1;             //  Device number -- determined automatically
static struct class*  mwcharClass  = NULL;  //  The device-driver class struct pointer
static struct device* mwcharDevice = NULL;  //  The device-driver device struct pointer

static bool  client_id_watch_active;
static bool  evtchn_bound_watch_active;

static domid_t       client_dom_id;
static domid_t       srvr_dom_id;
static int           common_event_channel;

// For allocating, tracking, and sharing XENEVENT_GRANT_REF_COUNT
// pages.
static uint8_t      *server_region;
static size_t        server_region_size;
static grant_ref_t   grant_refs[ XENEVENT_GRANT_REF_COUNT ];

static int          irqs_handled = 0;
static int          irq = 0;


#if MW_DO_PERFORMANCE_METRICS

// Vars for performance tests
ktime_t start, end;
s64 actual_time;

#endif // MW_DO_PERFORMANCE_METRICS

static struct semaphore event_channel_sem;

// Enforce only one writer to ring buffer at a time
static struct mutex request_mutex;

//static struct mutex mw_res_mutex;

//
// We maintain two lists of thread_response_map_t's. One is for the
// active processes (threads); the other is for processes that have
// closed their handle to our device - either on their own, or by the
// kernel because the kernel is killing them.
//
// We do this to reduce contention on the active list. It may be
// possible to maintain all mappings in one list; further research
// could be done on this.
//
static struct list_head    active_mapping_head;
static struct rw_semaphore active_mapping_lock;

static struct list_head    rundown_mapping_head;
static struct rw_semaphore rundown_mapping_lock;

// For enforcement: only one rundown (via dev_release()) allowed at
// one time. This means that if the rundown list is populated, then
// the rundown mutex has been acquired.
static struct mutex rundown_mutex;

static struct completion   ring_ready;
static bool                ring_prepared;

// Only this single thread reads from the ring buffer
static struct task_struct * worker_thread;
static bool pending_exit = false;
static struct completion   worker_thread_done;

// Defines:
// mwevent_sring_t
// mwevent_front_ring_t
// mwevent_back_ring_t
DEFINE_RING_TYPES( mwevent, mt_request_generic_t, mt_response_generic_t );

struct mwevent_sring *shared_ring = NULL;
struct mwevent_front_ring front_ring;
size_t shared_mem_size = PAGE_SIZE * XENEVENT_GRANT_REF_COUNT;


// The prototype functions for the character driver
static int     dev_open(struct inode *, struct file *);
static int     dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);

static int
internal_close_remote_fd( thread_response_map_t * ThreadResp,
                       mw_socket_fd_t          SockFd );

static int
internal_await_response( thread_response_map_t * ThreadResp );
                                      

static void mwchar_exit(void);

// Tracks the current request ID. Corresponds to mt_request_base_t.id
static mt_id_t
get_next_id( void )
{
    static atomic64_t counter = ATOMIC64_INIT( 0 );

    return (mt_id_t) atomic64_inc_return( &counter );
}


/**
 * @brief Helper function to find or create socket/pid mapping.
 *
 * Mappings are keyed by PID, not by thread group. This way there can
 * be one mapping per thread. Mappings start off in the active list and
 * move to the rundown list when they close their handle to the driver.
 */
static thread_response_map_t *
get_thread_response_map_by_task( struct task_struct * Task,
                                 bool                 Create )
{
    thread_response_map_t * curr = NULL;

    pr_debug( "Getting Thread Response mapping for PID %d\n",
              Task->pid );
    
    down_read( &active_mapping_lock );
    
    list_for_each_entry( curr, &active_mapping_head, list )
    {
        if ( Task->pid == curr->pid )
        {
            up_read( &active_mapping_lock );
            goto ErrorExit;
        }
    } // for

    // Not found; create a new one
    up_read( &active_mapping_lock );

    if ( !Create )
    {
        curr = NULL;
        goto ErrorExit;
    }
    
    curr = (thread_response_map_t *)
        kmem_cache_alloc( threadresp_slab, GFP_KERNEL );
    if ( NULL == curr )
    {
        pr_err( "kmem_cache_alloc failed\n" );
        goto ErrorExit;
    }

    curr->pid  = Task->pid;
    curr->tgid = Task->tgid;
    curr->task = Task;

    curr->rundown       = false;
    curr->awaiting_read = false;

    init_rwsem( &curr->sock_list_sem );
    INIT_LIST_HEAD( &curr->open_socket_list );

    sema_init( &curr->response_pending,   0 );

    // Allow 1 request/response pair per thread. 
    sema_init( &curr->in_flight_lock, 1 );

    // The new entry starts in the active list
    down_write( &active_mapping_lock );
    list_add( &curr->list, &active_mapping_head );
    up_write( &active_mapping_lock );

ErrorExit:
    return curr;
}

///
/// @brief Gets the thread_response_map_t associated with the given ID.
///
/// Looks first in the active list, then in the rundown list.
static thread_response_map_t *
get_thread_response_map_by_id( mt_id_t id )
{
    thread_response_map_t * curr = NULL;
    bool found = false;

    // Look in the active list first
    down_read( &active_mapping_lock );
    list_for_each_entry( curr, &active_mapping_head, list )
    {
        if ( id == curr->message_id )
        {
            pr_debug( "ID %lx found in active list\n", (unsigned long) id );
            found = true;
            break;
        }
    }
    up_read( &active_mapping_lock );

    if ( found )
    {
        goto ErrorExit;
    }


    // Next, look in the rundown list
    down_read( &rundown_mapping_lock );
    list_for_each_entry( curr, &rundown_mapping_head, list )
    {
        if ( id == curr->message_id )
        {
            pr_debug( "ID %lx found in rundown list\n", (unsigned long) id );
            found = true;
            break;
        }
    }
    up_read( &rundown_mapping_lock );

    if ( !found )
    {
        curr = NULL;
    }

ErrorExit:
    return curr;
}


///
/// @brief Destoys the given socket, possibly closing it if requested.
///
/// Untracks the socket and may issue a close request. In that case,
/// the function will block until the close is complete.
///
static int
destroy_socket( thread_response_map_t * ThreadResp,
                mw_socket_fd_t          MwSock,
                bool                    SendClose )
{
    open_mw_socket_t * curr = NULL;
    open_mw_socket_t * next = NULL;
    bool found = false;
    int rc = 0;

    pr_debug( "Destroying socket %x (PID %d), SendClose=%d\n",
              MwSock, ThreadResp->pid, SendClose );

    // Rundown code path acquires the lock for us
    if ( !ThreadResp->rundown )
    {
        down_write( &ThreadResp->sock_list_sem );
    }

    list_for_each_entry_safe( curr, next, &ThreadResp->open_socket_list, list )
    {
        if ( curr->mwsockfd == MwSock )
        {
            found = true;
            list_del( &curr->list );            
            break;
        }
    }

    if ( !ThreadResp->rundown )
    {
        up_write( &ThreadResp->sock_list_sem );
    }
    
    if ( !found )
    {
        pr_err( "Socket %x not found!\n", MwSock );
        rc = -ENOENT;
        goto ErrorExit;
    }

    kmem_cache_free( open_socket_slab, curr );

    if ( SendClose )
    {
        rc = internal_close_remote_fd( ThreadResp, MwSock );
        if ( rc )
        {
            goto ErrorExit;
        }
    }

ErrorExit:
    return rc;
}


static int
track_socket( thread_response_map_t * ThreadResp,
              mw_socket_fd_t          SockFd )
{
    int rc = 0;
    open_mw_socket_t * curr = NULL;

    curr = kmem_cache_alloc( open_socket_slab, GFP_KERNEL ); 
    if ( NULL == curr )
    {
        pr_err( "kmem_cache_alloc failed\n" );
        rc = -ENOMEM;
        goto ErrorExit;
    }

    curr->mwsockfd = SockFd;

    down_write( &ThreadResp->sock_list_sem );
    pr_debug( "Tracking socket %x with PID %d\n", SockFd, ThreadResp->pid );
    list_add( &curr->list, &ThreadResp->open_socket_list );
    up_write( &ThreadResp->sock_list_sem );

ErrorExit:
    return rc;
}


/// @brief Destroys the thread response map. Blocks until complete.
///
/// Caller must hold rundown_mutex
static void
destroy_thread_response_map( thread_response_map_t ** ThreadRespMap )
{
    // Convenience variable
    thread_response_map_t * threadresp = *ThreadRespMap;

    open_mw_socket_t * curr = NULL;
    open_mw_socket_t * next = NULL;

    int rc = 0;

    // Move the mapping to the rundown list to reduce contention on it.
    down_write( &active_mapping_lock );
    down_write( &rundown_mapping_lock );

    list_move( &threadresp->list, &rundown_mapping_head );

    up_write( &rundown_mapping_lock );
    up_write( &active_mapping_lock );

    // Make an effort to wait for in-flight I/O. However, it is
    // impossible to wait for all of it, e.g. an accept() might never
    // return.
    (void) down_interruptible( &threadresp->in_flight_lock );

    // Close all the open sockets for this mapping. A true rundown
    // field means that sock_list_sem is held.
    down_write( &threadresp->sock_list_sem );
    threadresp->rundown = true;

    list_for_each_entry_safe( curr, next, &threadresp->open_socket_list, list )
    {
        rc = destroy_socket( threadresp, curr->mwsockfd, true );
        if ( rc && rc != -EINTR )
        {
            // -EINTR means we sent the close() request but were
            // interrupted while waiting for the response.
            pr_err( "Unexpected rc = %d\n", rc );
            break;
        }
    }

    up_write( &threadresp->sock_list_sem );

    // Remove from the rundown list
    pr_debug( "Removing struct for PID %d from rundown list\n",
              threadresp->pid );

    //
    // Destroy the mapping for good
    //
    down_write( &rundown_mapping_lock );
    list_del( &threadresp->list );
    up_write( &rundown_mapping_lock );
    
    kmem_cache_free( threadresp_slab, threadresp );
    *ThreadRespMap = NULL;
}


/**
 * @brief Set the callback functions for the file_operations struct
 */
static struct file_operations fops =
{
   .open    = dev_open,
   .read    = dev_read,
   .write   = dev_write,
   .release = dev_release,
};


static int
consume_response_worker( void * Data )
{
    int rc = 0;
    bool available = false;
    mt_response_generic_t * response = NULL;
    thread_response_map_t * conn = NULL;

    pr_info( "Process %d created to consume messages on the ring buffer\n",
             current->pid );

    // Wait for the ring buffer's initialization to complete
    rc = wait_for_completion_interruptible( &ring_ready );
    if ( rc < 0 )
    {
        pr_info( "Received interrupt before ring ready\n" );
        goto ErrorExit;
    }

    // Completion succeeded
    if ( pending_exit )
    {
        pr_info( "Detecting pending exit. Worker thread exiting.\n" );
        goto ErrorExit;
    }

    pr_info( "Proc %d awaiting responses from ring buffer (0x%x slots).\n",
             current->pid, RING_SIZE( &front_ring ) );

    // Consume responses until the module is unloaded. When it is
    // unloaded, consume whatever is still on the ring, then
    // quit. Only leave this loop on a requested exit or a fatal
    // error.
    while( true )
    {
        do
        {
            available = RING_HAS_UNCONSUMED_RESPONSES( &front_ring );
            if ( !available )
            {
                if ( pending_exit )
                {
                    // Nothing available and waiting on exit
                    goto ErrorExit;
                }

                if ( down_interruptible( &event_channel_sem) )
                {
                    pr_info( "Received interrupt in worker thread\n" );
                    goto ErrorExit;
                }
            }
        } while (!available);

        // An item is available. Consume it.
        // Policy: continue upon error.
        // Policy: in case of pending exit, keep consuming the requests until
        //         there are no more
        response = (mt_response_generic_t *)
            RING_GET_RESPONSE(&front_ring, front_ring.rsp_cons);

        pr_debug( "Response ID %lx size %x type %x on ring at idx %x\n",
                  (unsigned long)response->base.id,
                  response->base.size, response->base.type,
                  front_ring.rsp_cons );

        if ( !MT_IS_RESPONSE( response ) )
        {
            // Fatal: The ring is corrupted.
            pr_crit( "Received data that is not a response at idx %d\n",
                     front_ring.rsp_cons );
            rc = -EIO;
            goto ErrorExit;
        }

        // Hereafter: Advance index as soon as we're done with the
        // item in the ring buffer.

        // Look for the ID in the both active and rundown lists.
        conn = get_thread_response_map_by_id( response->base.id );
        if ( NULL == conn )
        {
            // This can happen if the process dies between issuing a
            // request and receiving the response. If the type is
            // MtResponseSocketCreate, we're OK because dev_release()
            // will acquire in_flight_lock and then destroy any open
            // sockets.
            pr_err( "Received response %lx for unknown request. "
                    "Did the process die? (sock %x, type %x)\n",
                    (unsigned long)response->base.id,
                    response->base.sockfd,
                    response->base.type );
            ++front_ring.rsp_cons;
            continue;
        }

        // Compare size of response to permitted size.
        if ( response->base.size > sizeof( conn->response ) )
        {
            pr_err( "Received response that is too big\n" );
            // Reduce the size that we send to user and fall through
            response->base.size = sizeof( conn->response );
        }

        // If this response involves thread assignment on the Rump side, 
        // we need to take certain actions. If destruction has been
        // started, then don't re-attempt it! Whether or not the
        // destruction succeeds, fall-through so the waiter is
        // notified.
        if ( MT_DEALLOCATES_FD( response->base.type ) )
        {
            (void) destroy_socket( conn, response->base.sockfd, false );
        }
        else if ( MT_ALLOCATES_FD( response->base.type )
                  && response->base.sockfd > 0 )
        {
            // The resource allocation succeeded: track the FD
            rc = track_socket( conn, response->base.sockfd );
            if ( rc )
            {
                goto ErrorExit;
            }
        }

        // Put the response into the connection map struct
        memcpy( &conn->response, response, response->base.size );

        // Update index and share it
        ++front_ring.rsp_cons;
        RING_FINAL_CHECK_FOR_RESPONSES( &front_ring, available );

        // The thread no longer has in-flight I/O.
        up( &conn->in_flight_lock );

        // Signal the (blocked) thread that a response is available
        up( &conn->response_pending );
    } // while

ErrorExit:
    if ( ring_prepared )
    {
        // Push our front_ring data to shared ring
        RING_FINAL_CHECK_FOR_RESPONSES( &front_ring, available );
    }
    complete( &worker_thread_done );
    do_exit( rc );
}

static int
send_request( mt_request_generic_t *  Request,
              mt_size_t               RequestSize,
              thread_response_map_t * ThreadResp )
{
    int rc = 0;
    void * dest = NULL;
    mt_id_t id = 0;
    unsigned long flags;
    thread_response_map_t * threadresp = ThreadResp;
    
    local_irq_save( flags ); // XXXX: check ThreadResp->rundown?

    if ( !ring_prepared )
    {
        pr_err( "Ring has not been initialized\n" );
        rc = -ENODEV;
        goto ErrorExit;
    }

    // Hold the lock for the duration of this function. All of these
    // must be done with no interleaving:
    //
    // * Write the request to the ring buffer
    // * Map the requesting process to the request, to match with the response
    // * Flush our private indices to the shared ring

    mutex_lock(&request_mutex);

    if ( NULL == threadresp )
    {
        threadresp = get_thread_response_map_by_task( current, true );
        if ( NULL == threadresp )
        {
            rc = -ENOMEM;
            goto ErrorExit;
        }
    }

    // This thread is running down and the request is from user mode
    if ( NULL == ThreadResp
         && threadresp->rundown )
    {
        pr_warning( "Cannot process user request during rundown\n" );
        rc = -EIO;
        goto ErrorExit;
    }
    
    // The semaphore 'response_pending' starts off locked. read_request()
    // attempts to acquire it and blocks until the worker thread
    // releases it upon receipt of a response.

    // Map this process' connection map to this message ID
    id = get_next_id();
    threadresp->message_id = id;

    pr_debug( "PID %d associated with message ID %lx\n",
              threadresp->pid, (unsigned long)id );
    
    // If we are awaiting a read, then we cannot write! However, if
    // we're in rundown then we allow the write.
    if ( threadresp->awaiting_read && !threadresp->rundown )
    {
        pr_err( "Caller already called write() and must call read() next\n" );
        rc = -EPERM;
        goto ErrorExit;
    }

    if ( RING_FULL( &front_ring ) )
    {
        pr_alert("Front ring is full. Is remote side up?\n");
        rc = -EAGAIN;
p        goto ErrorExit;
    }

    dest = RING_GET_REQUEST(&front_ring, front_ring.req_prod_pvt);
    if ( !dest ) 
    {
        pr_err("destination buffer is NULL\n");
        rc = -EIO;
        goto ErrorExit;
    }

    if ( (void *)Request > (void *)TASK_SIZE )
    {
        // Kernel memory
        memcpy( dest, Request, RequestSize );
    }
    else
    {
        // Copy the request's claimed size into the ring buffer.
        rc = copy_from_user( dest, Request, RequestSize );
        if ( rc )
        {
            pr_err( "copy_from_user failed: %d\n", rc );
            rc = -EFAULT;
            goto ErrorExit;
        }
    }

    // Hereafter work with copy in ring buffer.
    ((mt_request_generic_t *)dest)->base.id = id;

    if ( !MT_IS_REQUEST( (mt_request_generic_t *)dest ) )
    {
        pr_err( "Invalid request given\n" );
        rc = -EINVAL;
        goto ErrorExit;
    }

    // There's an in-flight request initiated from user mode. Acquire
    // the lock here. So if the request is from kernel mode, the lock
    // has already been acquired.
    if ( NULL == ThreadResp )
    {
        down( &threadresp->in_flight_lock );
    }

    // We've written to the ring; now we are awaiting a response
    threadresp->awaiting_read = true;

    // Update the ring buffer only *after* the mapping is in place!
    ++front_ring.req_prod_pvt;
    RING_PUSH_REQUESTS( &front_ring );

    pr_debug( "Request %lx size %d was sent to idx %x\n",
              (long int) id, RequestSize, front_ring.req_prod_pvt-1 );
    
ErrorExit:
#if MW_DO_SEND_RING_EVENTS
    if ( 0 == rc )
    {
        // Inform remote side, only if everything went right
        send_evt( common_event_channel );
    }
#endif // MW_DO_SEND_RING_EVENTS
    mutex_unlock(&request_mutex);
    local_irq_restore( flags );
    return rc;
}


static irqreturn_t irq_event_handler( int port, void * data )
{
    //unsigned long flags;
    //local_irq_save( flags );
   
    ++irqs_handled;

    //xen_clear_irq_pending(irq);
    //local_irq_restore( flags );

    up(&event_channel_sem);

    return IRQ_HANDLED;
}


/** @brief The LKM initialization function
 *  @return returns 0 if successful
 */
static int __init
mwchar_init(void)
{
   int   rc;

   server_region = NULL;
   client_dom_id = 0;
   common_event_channel = 0;
   
   client_id_watch_active = false;
   evtchn_bound_watch_active = false;

#if MW_DO_PERFORMANCE_METRICS
   start = ktime_set(0,0);;
   end = ktime_set(0,0);;
#endif // MW_DO_PERFORMANCE_METRICS
   pr_debug("Initializing\n");

#if 0
   struct module * mod = (struct module *) THIS_MODULE;
   // gdb> add-symbol-file char_driver.ko $eax/$rax
   asm( "int $3" // module base in *ax
        //:: "a" ((THIS_MODULE)->module_core));
        :: "a" ((THIS_MODULE)->init_layout.base)
        , "c" (mod) );
#endif
   // Set up the structs for the thread-connection mapping
   INIT_LIST_HEAD( &active_mapping_head );
   init_rwsem( &active_mapping_lock );

   INIT_LIST_HEAD( &rundown_mapping_head );
   init_rwsem( &rundown_mapping_lock );

   mutex_init( &rundown_mutex );
   
   init_completion( &ring_ready );
   init_completion( &worker_thread_done );
   
   sema_init( &event_channel_sem, 0 );
   mutex_init( &request_mutex );

   open_socket_slab =
       kmem_cache_create( "MW sockets", sizeof(open_mw_socket_t),
                          0, 0, NULL );
   if ( NULL == open_socket_slab )
   {
       pr_err( "kmem_cache_create() failed\n" );
       rc = -ENOMEM;
       goto ErrorExit;
   }

   threadresp_slab =
       kmem_cache_create( "MW connection maps", sizeof(thread_response_map_t),
                          0, 0, NULL );
   if ( NULL == threadresp_slab )
   {
       pr_err( "kmem_cache_create() failed\n" );
       rc = -ENOMEM;
       goto ErrorExit;
   }
   
   // Create worker thread that reads items from the ring buffer
   worker_thread = kthread_run( &consume_response_worker,
                                NULL,
                                "MwMsgConsumer" );
   if ( NULL == worker_thread )
   {
       pr_err( "kthread_run() failed\n" );
       rc = -ESRCH;
       goto ErrorExit;
   }

   // Try to dynamically allocate a major number for the device --
   // more difficult but worth it
   majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
   if (majorNumber < 0)
   {
       rc = majorNumber;
       pr_err( "register_chrdev failed: %d\n", rc );
       goto ErrorExit;
   }

   // Register the device class
   mwcharClass = class_create(THIS_MODULE, CLASS_NAME);
   if (IS_ERR(mwcharClass))
   {
       rc = PTR_ERR(mwcharClass);
       pr_err( "class_create failed: %d\n", rc );
       goto ErrorExit;
   }

   // Register the device driver
   mwcharDevice = device_create(mwcharClass,
                                NULL,
                                MKDEV(majorNumber, 0),
                                NULL,
                                DEVICE_NAME);
   if (IS_ERR(mwcharDevice))
   {
      rc = PTR_ERR(mwcharDevice);
      pr_err( "device_create failed: %d\n", rc );
      goto ErrorExit;
   }


ErrorExit:
   if ( rc )
   {
       mwchar_exit();
   }
   return rc;
}

/**
 * @brief The driver cleanup function
 */
static void mwchar_exit(void)
{
    thread_response_map_t * curr = NULL;
    thread_response_map_t * next = NULL;

    pending_exit = true;

    // Kick the worker thread. It might be waiting for the ring to
    // become ready, or it might be waiting for responses to arrive on
    // the ring. Wait for it to complete so shared resources can be
    // destroyed.
    complete( &ring_ready );
    up( &event_channel_sem );

    if ( NULL != worker_thread )
    {
        pr_info( "Waiting for worker thread (PID %d) to complete\n",
                 worker_thread->pid );
        wait_for_completion( &worker_thread_done );
        //kthread_stop( worker_thread );
        //worker_thread = NULL;
    }

    if ( majorNumber >= 0 )
    {
        device_destroy(mwcharClass, MKDEV(majorNumber, 0)); // remove the device
    }

    if ( NULL != mwcharClass )
    {
        class_destroy(mwcharClass); // remove the device class
        class_unregister(mwcharClass); // unregister the device class
    }
    
    if ( NULL != mwcharDevice )
    {
        unregister_chrdev(majorNumber, DEVICE_NAME); // unregister the major number
    }

   for ( int i = 0; i < XENEVENT_GRANT_REF_COUNT; i++ )
   {
       if ( 0 != grant_refs[ i ] )
       {
           pr_debug("Ending access to grant ref 0x%x\n", grant_refs[i]);
           gnttab_end_foreign_access_ref( grant_refs[i], 0 );
       }
   }

   if ( NULL != server_region )
   {
       free_pages( (unsigned long) server_region, XENEVENT_GRANT_REF_ORDER );
   }

   if (evtchn_bound_watch_active)
   {
      unregister_xenbus_watch(&evtchn_bound_watch);
   }

   if (client_id_watch_active)
   {
      unregister_xenbus_watch(&client_id_watch);
   }

   initialize_keys();

   if (irq)
   {
      unbind_from_irqhandler(irq, NULL);
   }

   if (!is_evt_chn_closed())
   {
      free_unbound_evt_chn();
   }

   mutex_destroy( &request_mutex );

   // Destroy all the remaining mappings.
   mutex_lock( &rundown_mutex );
   list_for_each_entry_safe( curr, next, &active_mapping_head, list )
   {
       destroy_thread_response_map( &curr );
   }
   mutex_unlock( &rundown_mutex );
   mutex_destroy( &rundown_mutex );

   if ( open_socket_slab )
   {
       kmem_cache_destroy( open_socket_slab );
   }
   if ( threadresp_slab )
   {
       kmem_cache_destroy( threadresp_slab );
   }

   pr_debug("cleanup is complete\n");
}

/** @brief Open a handle to the driver's device.
 *
 * A process can only open one handle to this device. dev_release()
 * destroys all the resources held by that the single handle.
 *
 * @param inodep A pointer to an inode object (defined in linux/fs.h)
 * @param filep A pointer to a file object (defined in linux/fs.h)
 */
static int
dev_open(struct inode *inodep, struct file *filep)
{
    int rc = 0;

    thread_response_map_t * tmap =
        get_thread_response_map_by_task( current, false );

    if ( NULL != tmap )
    {
        pr_err( "Calling process %d already has a handle to this device\n",
                current->pid );
        rc = -EBUSY;
        goto ErrorExit;
    }

ErrorExit:
    return rc;
}

/** @brief This function is called whenever device is being read from user space i.e. data is
 *  being sent from the device to the user. In this case it uses the copy_to_user() function to
 *  send the buffer to the user and captures any errors.
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 *  @param buffer The pointer to the buffer to which this function writes the data
 *  @param len The length of the buffer
 *  @param offset The offset if required
 */
static ssize_t
dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset)
{
   int rc = 0; 
   thread_response_map_t * threadresp = NULL;
   mt_size_t resp_size = 0;

   threadresp = get_thread_response_map_by_task( current, false );
   if ( NULL == threadresp )
   {
       pr_err( "read() called from thread that hasn't issued write()\n" );
       rc = -EPERM;
       goto ErrorExit;
   }

   rc = internal_await_response( threadresp );
   if ( rc )
   {
       goto ErrorExit;
   }

   resp_size = threadresp->response.base.size;

   // An item is available and is in threadresp. Process it.
   if ( len < resp_size )
   {
       pr_err( "User buffer too small for response.\n" );
       rc = -EINVAL;
       goto ErrorExit;
   }

   // Data has been received. Write it to the user buffer.
   rc = copy_to_user( buffer,
                      &threadresp->response,
                      resp_size );
   if ( rc )
   {
       pr_err( "copy_to_user() failed: %d\n", rc );
       rc = -EFAULT;
       goto ErrorExit;
   }

   // Success
   rc = resp_size;

ErrorExit:
   pr_debug( "Returning %d\n", rc );
   return rc;
}


///
/// @brief Awaits a response from the ring buffer.
///
/// Waits for the worker thread to indicate that a reply for this
/// thread-response map has arrived. If this mapping is in rundown
/// mode, we issue an uninterruptible wait; otherwise it is
/// interruptible.
///
static int
internal_await_response( thread_response_map_t * ThreadResp )
{
    int rc = 0;
    
    if ( !ring_prepared )
    {
        pr_err( "Ring has not been initialized\n" );
        rc = -ENODEV;
        goto ErrorExit;
    }

    if ( !ThreadResp->awaiting_read )
    {
        pr_err( "Caller hasn't called write() prior to this read()\n" );
        rc = -EPERM;
        goto ErrorExit;
    }

    // Now we have the mapping for this thread. Wait for data to
    // arrive. This may block the calling process.
    pr_debug( "Waiting for response %lx to arrive (rundown=%d)\n",
              (unsigned long) ThreadResp->message_id,
              ThreadResp->rundown );

    for ( int i = MW_INTERNAL_MAX_WAIT_ATTEMPTS; i > 0; --i )
    {
        if ( down_interruptible( &ThreadResp->response_pending ) )
        {
            // Signal encountered, semaphore not acquired.
            pr_err( "read() was interrupted\n" );

            if ( !ThreadResp->rundown )
            {
                // The non-rundown case: give up immediately. The
                // response will be dropped; the write/read pair must
                // be reissued.
                rc = -EINTR;
                goto ErrorExit;
            }

            // Rundown case
            if ( 0 == i )
            {
                // Our final attempt was interrupted. Give up.
                rc = -EINTR;
                goto ErrorExit;
            }
        }
        else
        {
            break;
        }
    }

    // The semaphore was acquired: the response has arrived, and the
    // user should call a write() next.
    pr_debug( "Response %lx (size %x) has arrived\n",
              (unsigned long) ThreadResp->message_id,
              ThreadResp->response.base.size );

    ThreadResp->awaiting_read = false;

ErrorExit:
    return rc;
}

///
/// @brief Close an MW socket.
///
/// This can be called from kernel context when the driver destroys a
/// socket on behalf of a process that has not closed the socket
/// properly.
///
static int
internal_close_remote_fd( thread_response_map_t * ThreadResp,
                          mw_socket_fd_t          SockFd )
{
    int rc = 0;
    mt_request_socket_close_t req;

    req.base.sig    = MT_SIGNATURE_REQUEST;
    req.base.type   = MtRequestSocketClose;
    req.base.size   = MT_REQUEST_SOCKET_CLOSE_SIZE;
    req.base.id     = 0; // assigned by send_request()
    req.base.sockfd = SockFd;

    pr_info( "Sending close(0x%x) on behalf of process %d (group %d)\n",
             SockFd, ThreadResp->pid, ThreadResp->tgid );

    rc = send_request( (mt_request_generic_t *) &req,
                       req.base.size,
                       ThreadResp );
    if ( rc )
    {
        goto ErrorExit;
    }

    // Now await the response in this thread's context
    rc = internal_await_response( ThreadResp );
    if ( rc )
    {
        goto ErrorExit;
    }

    pr_debug( "Successfully closed socket %x\n", SockFd );

ErrorExit:
    return rc;
}



/** @brief Writes a request to the shared ring buffer.
 *
 *  @param filep A pointer to a file object
 *  @param buffer The buffer to that contains the string to write to the device
 *  @param len The length of the array of data that is being passed in the const char buffer
 *  @param offset The offset if required
 */
static ssize_t 
dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset)
{
    int rc = 0;
    mt_request_generic_t *req = (mt_request_generic_t *)buffer;
    mt_size_t req_size = 0;
    
#if MW_DO_PERFORMANCE_METRICS
    int effective_number;
    if (ktime_to_ns(start) == 0)
    {
        start = ktime_get();
    }
    else
    {
        end = ktime_get();
        actual_time = ktime_to_ns(ktime_sub(end, start));
        effective_number = (irqs_handled + 1)/2;
        pr_debug("%d     %lld\n", effective_number, actual_time);
        start = ktime_set(0,0);
    }
#endif // MW_DO_PERFORMANCE_METRICS

    rc = get_user( req_size, &req->base.size );
    if ( rc )
    {
        pr_err( "get_user failed: %d\n", rc );
        rc = -EFAULT;
        goto ErrorExit;
    }
    
    if ( req_size > len || 0 == req_size )
    {
        pr_err( "Received request with invalid size\n" );
        rc = -EINVAL;
        goto ErrorExit;
    }

    rc = send_request( req, req_size, NULL );
    if ( rc )
    {
        goto ErrorExit;
    }

    // Success
    rc = req_size;

#if MW_DO_PERFORMANCE_METRICS
    end = ktime_get();

    actual_time = ktime_to_ns(ktime_sub(end, start));
    pr_info("Time taken for send_request() execution (ns): %lld\n",
            actual_time);

    actual_time = ktime_to_ms(ktime_sub(end, start));

    pr_info("Time taken for send_request() execution (ms): %lld\n",
            actual_time);
#endif // MW_DO_PERFORMANCE_METRICS
    
ErrorExit:
    return rc;
}

/** @brief Handle close() request.
 *
 *  @param inodep A pointer to an inode object (defined in linux/fs.h)
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 */
static int dev_release(struct inode *inodep, struct file *filep)
{
    thread_response_map_t * currtc = NULL;
    thread_response_map_t * nexttc = NULL;

    pr_debug( "Device release initiated\n" );
    mutex_lock( &rundown_mutex );
    
    // Release all the mappings associated with current's thread
    // group. First, move them all to the rundown list. Then, destroy
    // them. Do this so we're not holding the active lock throughout
    // the operation.

    // Give the ring buffer a chance to clear out
    pr_debug( "Sleeping..." );
    set_current_state( TASK_INTERRUPTIBLE );
    schedule_timeout( MW_RUNDOWN_DELAY_SECS * HZ );
    pr_debug( "Done sleeping.\n" );

    list_for_each_entry_safe( currtc, nexttc, &active_mapping_head, list )
    {
        if ( currtc->tgid == current->tgid )
        {
            destroy_thread_response_map( &currtc );
        }
    }

    //rundown_in_progress = false;

    mutex_unlock( &rundown_mutex );
    pr_debug("Device successfully closed\n");
    
    return 0;
}

/** @brief A module must use the module_init() module_exit() macros
 *  from linux/init.h, which identify the initialization function at
 *  insertion time and the cleanup function (as listed above)
 */
module_init(mwchar_init);
module_exit(mwchar_exit);

#endif // 0
