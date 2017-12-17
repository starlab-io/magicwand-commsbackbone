/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

/**
 * @file    mwcomms-socket.c
 * @author  Matt Leinhos
 * @date    23 March 2017
 * @version 0.2
 * @brief   Implementation of MagicWand socket.
 *
 * Overview
 * --------
 * This file provides the core of the Magic Wand PVM driver's
 * functionality. Once an application has created a new Magic Wand
 * socket (mwsocket) via an IOCTL to the main LKM device, its further
 * IO with an mwsocket is routed to callbacks defined in this file.
 *
 * Initialization
 * --------------
 * This C module's init function (mwsocket_init) initializes a variety
 * of bookkeeping structures, including a pseudo filesystem (so it can
 * create file objects which interact with VFS), a couple of caches
 * for fast memory allocation, and two threads: a response consumer
 * that reads responses off the ring buffer, and a poll monitor that
 * periodically sends poll() requests to the INS to support local
 * usage of select()/poll()/epoll.
 *
 * Once the Xen handshake and shared memory arrangement is completed
 * (see mwcomms-base.c), mwsocket_notify_ring_ready() is invoked. In
 * turn the threads are released from a wait and begin working.
 *
 *
 * Socket interactions
 * -------------------
 * When an application creates a new mwsocket, the actual creation is
 * done by mwsocket_create(), which:
 *
 * (1) Creates a socket instance, or "sockinst". This is a structure
 * that describes an mwsocket, to include an inode and file object
 * against the mwsocket pseudo-filesystem. The file object is created
 * using the callbacks below for its operations (f_op). It also
 * registers a file descriptor for its new file object in the current
 * process.
 *
 * (2) Creates a new socket on the INS by sending a creation request
 * to the INS.
 *
 * Thereafter, interaction with that mwsocket goes through these functions:
 *
 * - mwsocket_write() - write a request to the mwsocket (and in turn,
 *   to the ring buffer)
 *
 * - mwsocket_read() - read a request from the mwsocket (and in turn,
 *   from the ring buffer)
 *
 * - mwsocket_ioctl() - changes behavior of an mwsocket by sending a
 *   request to the INS, which triggers a call to setsockopt() or
 *   fcntl().
 *
 * - mwsocket_poll() - interacts with results of the poll monitor
 *   thread to support calls to select()/poll() or epoll.
 *
 * - mwsocket_release() - destroys an mwsocket once it is safe to do
 *   so, given the INS state.
 *
 *
 * Data structures
 * ---------------
 * There are two primary data structures here:
 *
 * (1) The socket instance ("sockinst") as discussed above. A sockinst
 * represents a live socket on the INS.
 *
 * (2) The active request, which represents a request/response pair
 * while it is "in-flight", meaning that the request has been created
 * and sent to the INS, but the response has not been received and
 * processed yet. Each active request is associated with a socket
 * instance. Moreover, each active request holds a reference to its
 * socket instance while it is alive, thus keeping the socket instance
 * from being destroyed.
 *
 *
 * General notes
 * --------------
 * This code is structured to maximize data throughput. When writing a
 * request, an application-level user can specify whether or not it
 * wants to wait for the response. In some cases it may make sense not
 * to wait, e.g. send(). The active request structure tracks whether
 * or not the response is to be delivered to the user. If it is not
 * delivered, then the response consumer thread does not attempt to
 * deliver it; instead it records in its socket instance if the
 * request failed and then destroyed the active request. If it is
 * delivered, it copies the response into the active request and
 * notifies any waiters on the active request's completion variable.
 *
 * The above-described functionality can go bad on a TCP/IP pipeline
 * that's saturated. This driver could report a send() as successful,
 * whereas the TCP/IP send() on the INS returns EAGAIN. The
 * application will find out about the failure only after it thinks it
 * has already sent the bytes that did not go onto the network. A
 * possible partial work-around is to fail a send() request on the PVM
 * side if the socket's poll events indicate that it is not writable.
 *
 * In case a non-delivered response contained an error, that error is
 * reported upon the next read() or write() against that socket
 * instance.
 *
 * Upon release(), the close request is not sent to the INS until
 * certain responses have been received from the INS. Their
 * corresponding request types are marked with
 * _MT_TYPE_MASK_CLOSE_WAITS in message_types.h. This is implemented
 * with a reader-writer lock in the socket instance. To complete a
 * close, the write lock must be held.
 */

#include "mwcomms-common.h"
#include "mwcomms-socket.h"
#include "mwcomms-xen-iface.h"

#include <mwerrno.h>

#include <linux/slab.h>
//#include <linux/slub_def.h> // XXXX: for debugging only

#include <linux/fs.h>
#include <linux/mount.h>

#include <linux/file.h>
#include <linux/poll.h>
#include <linux/delay.h>

#include <linux/kallsyms.h>

#include <linux/kthread.h>

#include <asm/byteorder.h>

#include <asm/atomic.h>
#include <asm/cmpxchg.h>

#include <message_types.h>
#include <translate.h>
#include <xen_keystore_defs.h>

#include "mwcomms-netflow.h"
#include <mw_netflow_iface.h>

struct thread_info ti; // DEBUG ONLY

// Magic for the mwsocket filesystem
#define MWSOCKET_FS_MAGIC  0x4d77536f // MwSo

// Object names
#define MWSOCKET_MONITOR_THREAD          "mw_monitor"
#define MWSOCKET_MESSAGE_CONSUMER_THREAD "mw_consumer"
#define MWSOCKET_ACTIVE_REQUEST_CACHE    "mw_active_requests"
#define MWSOCKET_SOCKET_INSTANCE_CACHE   "mw_socket_instances"
#define MWSOCKET_FILESYSTEM_NAME         "mwsocketfs"
#define MWSOCKET_FILESYSTEM_PREFIX       "mwsocket: "

//
// Timeouts for responses
//

// General-purpose response timeout for user-initiated requests. If
// debug output is enabled on the INS, this needs to be larger.
#define GENERAL_RESPONSE_TIMEOUT      ( HZ * 30)

// Response timeout for kernel-initiated poll requests
#define MONITOR_RESPONSE_TIMEOUT ( HZ * 2 )

//
// How frequently should the poll monitor thread send a request?
//

//#define MONITOR_QUERY_INTERVAL   ( HZ * 2 ) // >= 1 sec

//#define MONITOR_QUERY_INTERVAL   ( HZ >> 2 ) // 4x/sec
//#define MONITOR_QUERY_INTERVAL   ( HZ >> 3 ) // 8x/sec
//#define MONITOR_QUERY_INTERVAL   ( HZ >> 4 ) // 16x/sec
#define MONITOR_QUERY_INTERVAL   ( HZ >> 5 ) // 32x/sec
//#define MONITOR_QUERY_INTERVAL   ( HZ >> 6 ) // 64x/sec

#if (!PVM_USES_EVENT_CHANNEL)
#  define RING_BUFFER_POLL_INTERVAL (HZ >> 6) // 64x / sec
#endif

// How long to wait if we want to write a request but the ring is full
#define RING_FULL_TIMEOUT (HZ >> 6)

// Poll-related messages are annoying, usually....
#define POLL_DEBUG_MESSAGES 0

#if POLL_DEBUG_MESSAGES
#  define pr_verbose_poll(...) pr_debug(__VA_ARGS__)
#else
#  define pr_verbose_poll(...) ((void)0)
#endif


// Even with verbose debugging, don't show these request/response types
#define DEBUG_SHOW_TYPE( _t )                                   \
    ( MtRequestPollsetQuery != MT_GET_REQUEST_TYPE(_t) )



// Flags for tracking socket state. We need these so we can duplicate
// listening/bound sockets onto new INSs as they appear.

// This is a primary socket, meaning the user program uses it
#define MWSOCKET_FLAG_USER           0x01

// Used by poll monitor. By convention, any socket with this flag
// cleared is considered "pollable".
#define MWSOCKET_FLAG_POLLABLE       0x02
#define MWSOCKET_FLAG_BOUND          0x10 // has been bound
#define MWSOCKET_FLAG_LISTENING      0x20 // in listen state
#define MWSOCKET_FLAG_ACCEPT         0x40 // currently accept()ing
#define MWSOCKET_FLAG_CONNECTED     0x100 // connect() succeeded FROM PVM
#define MWSOCKET_FLAG_CLOSED   0x40000000 // remote close
#define MWSOCKET_FLAG_RELEASED 0x80000000 // visited by mwsocket_release


#define MWSOCKET_FLAG_READY (MWSOCKET_FLAG_BOUND | MWSOCKET_FLAG_LISTENING)


/******************************************************************************
 * Interface to MW socket files.
 ******************************************************************************/

static ssize_t
mwsocket_read( struct file * File,
               char        * Bytes,
               size_t        Len,
               loff_t      * Offset );

static ssize_t
mwsocket_write( struct file * File,
                const char  * Bytes,
                size_t        Len,
                loff_t      * Offset );

static long
mwsocket_ioctl( struct file * File,
                unsigned int   Cmd,
                unsigned long  Arg );

static unsigned int
mwsocket_poll( struct file * File,
               struct poll_table_struct * PollTbl );

static int
mwsocket_release( struct inode *Inode,
                  struct file * File );

// N.B. An open() will increment this module's usage count, and
// release() will decrement it. Since we don't have a traditional
// open() we compensate for this behavior in release().
static struct file_operations
mwsocket_fops =
{
    owner          : THIS_MODULE,
    read           : mwsocket_read,
    write          : mwsocket_write,
    unlocked_ioctl : mwsocket_ioctl,
    poll           : mwsocket_poll,
    release        : mwsocket_release
};


/******************************************************************************
 * Types and globals
 ******************************************************************************/

struct _mwsocket_active_request;


/**
 * @brief For tracking per-mwsocket data.
 *
 * An mwsocket is built on top of the mwsocket filesystem. A good
 * example of this model is found in the Linux kernel: fs/pipe.c
 *
 * XXXX: This structure should contain a lock so that only one thread
 * can manipulate it at a time. There could be a race condition
 * between the mwsocket_response_consumer() thread and
 * mwsocket_release().
 */
typedef struct _mwsocket_instance
{
    //
    // Local kernel registration info
    //

    // The process in which this socket was created
    struct task_struct * proc;
    struct inode       * inode;
    struct file        * file;

    // For general access when concurrency is concern
    struct mutex         access_lock;

    // File descriptor info: one for PVM, one for INS
    int                  local_fd; 
    mw_socket_fd_t       remote_fd;

    // Socket state, etc for multi-INS support
    int                   mwflags; // MWSOCKET_FLAG_*

    // socket()
    mt_protocol_family_t  sock_fam;
    mt_sock_type_t        sock_type;
    uint32_t              sock_protocol;

    // bind()
    mt_sockaddr_in_t      bind_sockaddr; // 1.2.3.4:xxxxx

    // listen()
    int                   backlog;

    // accept()
    int                   accept_flags; // XXXX: unused

    // For management of queue of inbound connections
    struct list_head      inbound_list; // anchor for list of active requests
    struct mutex          inbound_lock; // to protect inbound_list
    struct semaphore      inbound_sem;  // to notify of pending inbound

     // Is there an accept() outstanding to the user? Used only in
     // mwsocket_{read,write} for the MWSOCKET_FLAG_USER sockinst.
    bool                  user_outstanding_accept;

    // Every time the process creates an mwsocket, that results in a
    // "primary", or "user" mwsocket. There are secondary ones which
    // this driver has registered on behalf of the process, although
    // the process doesn't know about them.

    // Pointer to the primary mwsocket. If this is the primary this value is NULL.
    struct _mwsocket_instance * usersock;

    // During polling, the primary mwsocket points to its sibling on
    // which there's an event. This only makes sense when polling for
    // an inbound connection (accept).
    struct _mwsocket_instance * poll_active;

    // List of the fellow listening mwsockets; needed for destruction.
    struct list_head            sibling_listener_list;

    // Who is on the other end of the INS?
    struct sockaddr      peer;

    // Is this socket listening?
    uint16_t             listen_port;
    struct sockaddr      local_bind;

    // poll() support: the latest events on this socket
    unsigned long        poll_events;

    // Our latest known value for the file->f_flags, used to track
    // changes. We don't monitor all the ways it could be changed.
    int                 f_flags;

    // User-provided flags that we track across socket usages
    int                 u_flags;

    // Support for scatter-gather sending. We track the total number
    // of bytes sent on the INS and synchronize (as requested by the
    // user) on the final request/response.
    ssize_t             send_tally;

    // Error encountered on INS that has not (yet) been delivered to
    // caller. Supports fire-and-forget model.
    int                 pending_errno;

    // Session stats for netflow
    struct timespec     est_time; // time of socket creation
    uint64_t            tot_sent;
    uint64_t            tot_recv;

    // How many active requests are using this mwsocket? N.B. due to
    // the way release() is implemented on the INS, it is not valid to
    // destroy an mwsocket instance immediately upon release. Destroy
    // instead when this count reaches 0. We don't rely on the file's
    // refct because we might have to close while there are
    // outstanding (and blocking) requests.
    atomic_t            refct;

    // The ID of the blocking request. Only one at a time!
    mt_id_t             blockid;

    // Did user indicate a read() is next?
    bool                read_expected; 
    // Have we started remote close?
    bool                remote_close_requested; 
    // Did the remote side close unexpectedly?
    bool                remote_surprise_close; 
    // Is the backing INS alive?
    bool                ins_alive;

    // Do not allow close() while certain operations are
    // in-flight. Implement this behavior with a read-write lock,
    // wherein close takes a write lock and all the other operations a
    // read lock. Don't block the close indefinitely -- the INS might
    // have died.
    struct rw_semaphore close_lock;

    // Has the close lock been acquired via down_write()?
    bool   close_lock_write;

    // All the instances. Must hold global sockinst_lock to access.
    struct list_head    list_all;

    // Latest ID of request that blocks this socket from closing
    atomic64_t          close_blockid; // holds mt_id_t

} mwsocket_instance_t;


/**
 * @brief For tracking state on requests whose responses have not yet
 * arrived.
 *
 * XXXX: we could add a notion of a "transfer set" of active requests,
 * so that when we're requested to transfer a lot of bytes we can do
 * so asynchronously but with a barrier on the final request/response,
 * so we report to the user the right number of bytes sent. Better
 * yet, add flags to the request to indicate a first or final in
 * sequence.
 */
typedef struct _mwsocket_active_request
{
    mt_id_t id;

    // The process that is issuing IO against the sockinst
    struct task_struct * issuer;

    // Will the requestor wait? If so, we deliver the response by
    // copying it to the response field here and completing 'arrived'
    // variable. Requestor can be user or kernel.
    bool   deliver_response;

    // Has the response been copied into this structure (rr field)?
    bool   response_populated;

    bool   from_user;

    // Signaled when the response arrives and is available in the
    // response field. Only signaled if deliver_response is true.
    struct completion arrived;

    // Either the request or the response. We don't need both
    // here. The entire request is stored here until the response is
    // processed.
    mt_request_response_union_t rr;

    // The backing socket instance
    mwsocket_instance_t * sockinst;

    // Multi-INS support. If list is non-empty then this request has
    // been added to an inbound queue associated with an mwsocket. If
    // it's invalid then it has been consumed.
    struct list_head list_inbound;

    struct list_head list_all;

} mwsocket_active_request_t;


typedef struct _mwsocket_globals
{

    // Used to signal socket subsystem threads
    struct completion xen_iface_ready;
    bool              is_xen_iface_ready;

    // Lock on ring access
    struct mutex request_lock;

    // Active requests
    struct kmem_cache * active_request_cache;
    struct list_head    active_request_list;
    struct mutex        active_request_lock;
    atomic_t            active_request_count;
    
    // Socket instances
    struct kmem_cache * sockinst_cache;
    struct list_head    sockinst_list;
    struct mutex        sockinst_lock;
    atomic_t            sockinst_count;
    atomic_t            poll_sock_count;

    // Indicates response(s) available
    struct semaphore    event_channel_sem;

    // Filesystem data
    struct vfsmount * fs_mount;
    bool              fs_registered;

    // Kernel thread info: response consumer
    struct task_struct * response_reader_thread;
    struct completion    response_reader_done;

    // Kernel thread info: poll notifier
    struct task_struct * monitor_thread;
    struct completion    monitor_done;

    // Support for poll(): to inform waiters of data
    wait_queue_head_t    waitq;

    struct sockaddr    * local_ip;
    
    bool   pending_exit;
} mwsocket_globals_t;

mwsocket_globals_t g_mwsocket_state;

// Data/forward decls needed for socket replication work upon new INS discovery

typedef struct _mwsocket_replication_work
{
    struct work_struct    base;
    domid_t               domid;
    mwsocket_instance_t * usersock;
} mwsocket_replication_work_t;


typedef struct _mwsocket_sock_replicate_args
{
    mwsocket_instance_t * sockinst;
    mt_request_generic_t * reqbase; // only the request_base can be assumed valid
} mwsocket_sock_replicate_args_t;

//DECLARE_WORK( mwsocket_replication_workq, mwsocket_replicate_listening_port_worker );


//
// Kernel symbols we have to find
//
typedef struct inode * pfn_new_inode_pseudo_t(struct super_block *);
static pfn_new_inode_pseudo_t * gfn_new_inode_pseudo = NULL;

typedef char *
pfn_dynamic_dname_t(struct dentry *, char *, int, const char *, ...);

static pfn_dynamic_dname_t * gfn_dynamic_dname = NULL;


/******************************************************************************
 * Forward declarations
 ******************************************************************************/

static void
mwsocket_destroy_active_request( mwsocket_active_request_t * Request );

static int 
mwsocket_create_active_request( IN  mwsocket_instance_t        * SockInst,
                                OUT mwsocket_active_request_t ** ActReq );

static int
mwsocket_send_request( IN mwsocket_active_request_t * ActiveReq,
                       IN bool                        WaitForRing );

static int
mwsocket_send_message( IN mwsocket_instance_t       * SockInst,
                       IN mt_request_generic_t      * Request,
                       IN bool                        AwaitResponse );

static int
mwsocket_close_remote( IN mwsocket_instance_t * SockInst,
                       IN bool                  WaitForResponse );

static int
mwsocket_new_ins( domid_t Domid );

static int
mwsocket_propogate_listeners( struct work_struct * Work );

static mw_xen_per_ins_cb_t mwsocket_ins_sock_replicator;

/******************************************************************************
 * Primitive Functions
 ******************************************************************************/

/**
 * @brief Gets the next unused, valid ID for use in requests.
 *
 * Caller must hold the global active_request_lock.
 */
static mt_id_t
MWSOCKET_DEBUG_ATTRIB
mwsocket_get_next_id( void )
{
    static atomic64_t counter = ATOMIC64_INIT( 0 );
    mt_id_t id = MT_ID_UNSET_VALUE;

    do
    {
        // Get the next candidate ID
        id = (mt_id_t) atomic64_inc_return( &counter );

        if( MT_ID_UNSET_VALUE == id )
        {
            id = (mt_id_t) atomic64_inc_return( &counter );
        }

        // Is this ID currently in use? The caller already holds the lock for us.
        bool found = false;
        mwsocket_active_request_t * curr = NULL;

        list_for_each_entry( curr, &g_mwsocket_state.active_request_list, list_all )
        {
            MYASSERT( curr->id );
            if( curr->id == id )
            {
                pr_debug( "Not using ID %lx because it is currently in use\n",
                          (unsigned long) id );
                found = true;
                break;
            }
        }

        if( !found )
        {
            break;
        }
    } while( true );

    return id;
}


void
mwsocket_notify_ring_ready( domid_t Domid )
{
    MYASSERT( Domid );

    // Triggered when new Ins is added.
    // only call if this is the first Ins that has been added
    if( !g_mwsocket_state.is_xen_iface_ready )
    {
        g_mwsocket_state.is_xen_iface_ready = true;
        complete_all( &g_mwsocket_state.xen_iface_ready );
    }
    else
    {
        // This is a new INS.
        mwsocket_new_ins( Domid );
    }
}

static void
mwsocket_wait( long TimeoutJiffies )
{
    set_current_state( TASK_INTERRUPTIBLE );
    schedule_timeout( TimeoutJiffies );
}


void
mwsocket_event_cb( void )
{
    up( &g_mwsocket_state.event_channel_sem );
}

/******************************************************************************
 * Support for the MW socket pseudo "filesystem", needed for file
 * object generation
 ******************************************************************************/
static const struct super_operations mwsocket_fs_ops =
{
    .destroy_inode = free_inode_nonrcu,
    .statfs        = simple_statfs,
};

static char *
mwsocket_fs_dname( struct dentry * Dentry, char * Buffer, int Len )
{
    return gfn_dynamic_dname( Dentry, Buffer, Len,
                              "mwsocket:[%lu]", d_inode(Dentry)->i_ino );
}

static const struct dentry_operations mwsocket_fs_dentry_ops =
{
    .d_dname = mwsocket_fs_dname,
};

static struct dentry *
fs_mount( struct file_system_type * FsType,
          int                       Flags,
          const char              * DevName,
          void                    * Data )
{
    return mount_pseudo( FsType,
                         MWSOCKET_FILESYSTEM_PREFIX,
                         &mwsocket_fs_ops,
                         &mwsocket_fs_dentry_ops,
                         MWSOCKET_FS_MAGIC );
}


static struct file_system_type mwsocket_fs_type =
{
    .name    = MWSOCKET_FILESYSTEM_NAME,
    .mount   = fs_mount,
    .kill_sb = kill_anon_super,
};


int
MWSOCKET_DEBUG_ATTRIB
mwsocket_fs_init( void )
{
    int rc = 0;

    pr_debug( "Initializing mwsocket pseudo-filesystem\n" );
    
    rc = register_filesystem( &mwsocket_fs_type );
    if( rc )
    {
        pr_err( "register_filesystem failed: %d\n", rc );
        goto ErrorExit;
    }

    g_mwsocket_state.fs_registered = true;

    g_mwsocket_state.fs_mount = kern_mount( &mwsocket_fs_type );

    if( IS_ERR( g_mwsocket_state.fs_mount ) )
    {
        rc = PTR_ERR( g_mwsocket_state.fs_mount );
        pr_err( "kern_mount() failed: %d\n", rc );
        goto ErrorExit;
    }

ErrorExit:
    return rc;
}


/******************************************************************************
 * Support functions for interactions between file objects and Xen ring buffer.
 ******************************************************************************/

/**
 * @todo: Can we remove this and point file->private to the sockinst ?
 */
static int
MWSOCKET_DEBUG_ATTRIB
mwsocket_find_sockinst( OUT mwsocket_instance_t ** SockInst,
                        IN  struct file          * File )
{
    int rc = 0;

    MYASSERT( SockInst );
    MYASSERT( File );

    *SockInst = (mwsocket_instance_t *) File->private_data;

    if( NULL == (*SockInst) ||
        File != (*SockInst)->file )
    {
        rc = -EBADFD;
        pr_err( "Invalid file object %p given\n", File );
        MYASSERT( !"Bad FD given" );
        *SockInst = NULL;
    }

    return rc;
}


static int
MWSOCKET_DEBUG_ATTRIB
mwsocket_find_sockinst_by_remote_fd( OUT mwsocket_instance_t ** SockInst,
                                     IN  mw_socket_fd_t         RemoteFd )
{
    int rc = -EBADF;

    MYASSERT( SockInst );

    *SockInst = NULL;

    mutex_lock( &g_mwsocket_state.sockinst_lock );

    mwsocket_instance_t * curr = NULL;
    list_for_each_entry( curr, &g_mwsocket_state.sockinst_list, list_all )
    {
        if( curr->remote_fd  == RemoteFd )
        {
            *SockInst = curr;
            rc = 0;
            break;
        }
    }

    mutex_unlock( &g_mwsocket_state.sockinst_lock );

    if( rc )
    {
        pr_info( "Failed to find requested remote FD=%lx\n",
                 (unsigned long) RemoteFd );
    }

    return rc;
}


/**
 * @brief Supports mitigation by forcing a remote close of the given socket.
 */
int
MWSOCKET_DEBUG_ATTRIB
mwsocket_close_by_remote_fd( IN mw_socket_fd_t RemoteFd,
                             IN bool           Wait )
{
    int rc = 0;
    mwsocket_instance_t * sockinst = NULL;

    rc = mwsocket_find_sockinst_by_remote_fd( &sockinst, RemoteFd );
    if( rc ) { goto ErrorExit; }

    rc = mwsocket_close_remote( sockinst, Wait );
    if( rc ) { goto ErrorExit; }

    sockinst->remote_surprise_close = true;

ErrorExit:
    return rc;
}


/**
 * @brief Supports mitigation by signalling the owner of the given
 *        remote socket FD.
 */
int
MWSOCKET_DEBUG_ATTRIB
mwsocket_signal_owner_by_remote_fd( IN mw_socket_fd_t RemoteFd,
                                    IN int            SignalNum )
{
    int rc = 0;
    mwsocket_instance_t * sockinst = NULL;

    rc = mwsocket_find_sockinst_by_remote_fd( &sockinst, RemoteFd );
    if( rc ) { goto ErrorExit; }

    pr_warn( "Delivering signal %d to process %d (%s) for socket %d\n",
             SignalNum,
             sockinst->proc->pid, sockinst->proc->comm,
             sockinst->local_fd );

    send_sig( SignalNum, sockinst->proc, 0 );

ErrorExit:
    return rc;
}


void
MWSOCKET_DEBUG_ATTRIB
mwsocket_debug_dump_sockinst( void )
{
    mwsocket_instance_t * curr = NULL;
    list_for_each_entry( curr, &g_mwsocket_state.sockinst_list, list_all )
    {
        if( !strcmp( MWSOCKET_MONITOR_THREAD, curr->proc->comm ) ) { continue; }

        pr_info( "%p refct %d file %p proc %d[%s]\n",
                 curr, atomic_read( &curr->refct ), curr->file,
                 curr->proc->pid, curr->proc->comm );
    }
}


// @brief Reference the socket instance
static void
MWSOCKET_DEBUG_ATTRIB
mwsocket_get_sockinst(  mwsocket_instance_t * SockInst )
{
    int val = 0;

    MYASSERT( SockInst );

    val = atomic_inc_return( &SockInst->refct );

#if defined( DEBUG ) || defined( VERBOSE )

    if( SockInst->local_fd < 0 )
    {
        //Print poll references
        pr_verbose( "Referenced socket instance %p fd=%d, refct=%d\n",
                  SockInst, SockInst->local_fd, val );
    }
    else
    {
        pr_debug( "Referenced socket instance %p fd=%d, refct=%d\n",
                    SockInst, SockInst->local_fd, val );
    }

#endif

    // XXXX should this check be performed somewhere else?
    // removed for multiple INS change
    // What's the maximum refct that should be against a sockinst?
    // Every slot in the ring buffer could be in use by an active
    // request against it, plus there could be one active request being
    // delivered to the user, plus another that has been consumed off
    // the ring but not destroyed yet (see mwsocket_response_consumer).
    //MYASSERT( val <= RING_SIZE( &g_mwsocket_state.front_ring ) + 2 );
}


/**
 * @brief Sets or clears pollable bit from flags and updates the
 * pollable socket count.
 *
 */
static void
mwsocket_set_pollable( IN mwsocket_instance_t * SockInst,
                       IN bool                  Pollable )
{
    MYASSERT( SockInst );

    mutex_lock( &SockInst->access_lock );

    if( (bool) (SockInst->mwflags | MWSOCKET_FLAG_POLLABLE) == Pollable )
    {
        // The state of the socket matches the requested state
        goto ErrorExit;
    }

    if( Pollable )
    {
        atomic_inc( &g_mwsocket_state.poll_sock_count );
        SockInst->mwflags |= MWSOCKET_FLAG_POLLABLE;
    }
    else
    {
        atomic_dec( &g_mwsocket_state.poll_sock_count );
        SockInst->mwflags &= ~MWSOCKET_FLAG_POLLABLE;
    }

ErrorExit:
    mutex_unlock( &SockInst->access_lock );
}


/**
 * @brief Dereferences the socket instance, destroying upon 0 reference count
 */
static void
MWSOCKET_DEBUG_ATTRIB
mwsocket_put_sockinst( mwsocket_instance_t * SockInst )
{
    MYASSERT( SockInst );

    int val = 0;
    
    if( NULL == SockInst )
    {
        goto ErrorExit;
    }

    val = atomic_dec_return( &SockInst->refct );
    if( val )
    {
#if defined( DEBUG ) || defined( VERBOSE )

        if( SockInst->local_fd < 0 )
        {
            pr_verbose( "Dereferenced socket instance %p fd=%d, refct=%d\n",
                        SockInst, SockInst->local_fd, val );
        }
        else
        {
            pr_debug( "Dereferenced socket instance %p fd=%d, refct=%d\n",
                      SockInst, SockInst->local_fd, val );
        }

#endif // defined( DEBUG ) || defined( VERBOSE )
        goto ErrorExit;
    }

    // Destroy this socket instance!

    // MYASSERT( list_empty( &SockInst->inbound_list ) );

    // Remove from global sockinst_list
    mutex_lock( &g_mwsocket_state.sockinst_lock );
    atomic_dec( &g_mwsocket_state.sockinst_count );
    list_del( &SockInst->list_all );
    mutex_unlock( &g_mwsocket_state.sockinst_lock );

    list_del( &SockInst->sibling_listener_list );

    // If this sockinst was pollable (i.e. able to receive events)
    // update the global count of pollable sockets.
    mwsocket_set_pollable( SockInst, false );

    pr_debug( "Destroyed socket instance %p fd=%d\n",
              SockInst, SockInst->local_fd );

    bzero( SockInst, sizeof(*SockInst) );
    kmem_cache_free( g_mwsocket_state.sockinst_cache, SockInst );

ErrorExit:
    return;
}


// XXXX: update to accept optional INS (domid) as parameter
static int
MWSOCKET_DEBUG_ATTRIB
mwsocket_create_sockinst( OUT mwsocket_instance_t ** SockInst,
                          IN  int                    Flags,
                          IN  bool                   Pollable,
                          IN  bool                   CreateBackingFile )
{
    int rc = 0;
    mwsocket_instance_t * sockinst = NULL;
    struct inode * inode = NULL;
    struct file  * file  = NULL;
    struct path path;
    static struct qstr name = { .name = "" };
    unsigned int flags = O_RDWR | Flags;
    int fd = -1;

    MYASSERT( SockInst );
    *SockInst = NULL;

    sockinst = (mwsocket_instance_t *)
        kmem_cache_alloc( g_mwsocket_state.sockinst_cache, GFP_KERNEL );
    if( NULL == sockinst )
    {
        rc = -ENOMEM;
        MYASSERT( !"kmem_cache_alloc failed\n" );
        goto ErrorExit;
    }

    bzero( sockinst, sizeof( *sockinst ) );

    // Must be in the list whether or not we're creating the backing file
    mutex_lock( &g_mwsocket_state.sockinst_lock );
    list_add( &sockinst->list_all, &g_mwsocket_state.sockinst_list );
    atomic_inc( &g_mwsocket_state.sockinst_count );
    mutex_unlock( &g_mwsocket_state.sockinst_lock );

    *SockInst = sockinst;
    mutex_init( &sockinst->access_lock );
    sockinst->local_fd = -1;
    sockinst->remote_fd = MT_INVALID_SOCKET_FD;
    // For pretty printing prior to binding, etc, esp in netflow
    sockinst->local_bind.sa_family = AF_INET;
    sockinst->peer.sa_family       = AF_INET;

    // By default the backing INS is assumed "alive"
    sockinst->ins_alive = true;

    sockinst->usersock = sockinst;
    INIT_LIST_HEAD( &sockinst->sibling_listener_list );
    INIT_LIST_HEAD( &sockinst->inbound_list );

    mutex_init( &sockinst->inbound_lock );
    sema_init( &sockinst->inbound_sem, 0 );

    init_rwsem( &sockinst->close_lock );

    sockinst->proc = current;
    // refct starts at 1; an extra put() is done upon close()
    atomic_set( &sockinst->refct, 1 );

    mwsocket_set_pollable( sockinst, Pollable );

    if( !CreateBackingFile ) { goto ErrorExit; }
    
    inode = gfn_new_inode_pseudo( g_mwsocket_state.fs_mount->mnt_sb );
    if( NULL == inode )
    {
        MYASSERT( !"new_inode_pseudo() failed\n" );
        rc = -ENOMEM;
        goto ErrorExit;
    }
    sockinst->inode = inode;

    inode->i_ino = get_next_ino();
    inode->i_fop = &mwsocket_fops;
    // prevent inode from moving to the dirty list
    inode->i_state = I_DIRTY;
    inode->i_mode  = S_IRUSR | S_IWUSR;
    inode->i_uid   = current_fsuid();
    inode->i_gid   = current_fsgid();

    path.dentry = d_alloc_pseudo( g_mwsocket_state.fs_mount->mnt_sb, &name );
    if( NULL == path.dentry )
    {
        MYASSERT( !"d_alloc_pseudo() failed\n" );
        rc = -ENOMEM;
        goto ErrorExit;
    }
    path.mnt = mntget( g_mwsocket_state.fs_mount );
    d_instantiate( path.dentry, inode );
    
    file = alloc_file( &path, FMODE_WRITE | FMODE_READ, &mwsocket_fops );
    if( IS_ERR( file ) )
    {
        rc = PTR_ERR( file );
        MYASSERT( !"alloc_file() failed" );
        goto ErrorExit;
    }
    sockinst->file = file;

    // For fast lookup
    MYASSERT( NULL == file->private_data );
    file->private_data = (void *) sockinst;

    file->f_flags |= flags;

    fd = get_unused_fd_flags( flags );
    if( fd < 0 )
    {
        MYASSERT( !"get_unused_fd_flags() failed\n" );
        rc = -EMFILE;
        goto ErrorExit;
    }

    // Success
    sockinst->local_fd = fd;
    sockinst->f_flags = flags;
    getnstimeofday( &sockinst->est_time );

    fd_install( fd, sockinst->file );

ErrorExit:
    pr_debug( "Created socket instance %p, file=%p inode=%p fd=%d\n",
              sockinst, file, inode, fd );

    if( rc )
    {
        // Cleanup partially-created file
        if( file )
        {
            put_filp( file );
            path_put( &path );
        }
        if( inode ) { iput( inode ); }
        if( sockinst )
        {
            mutex_lock( &g_mwsocket_state.sockinst_lock );

            list_del( &sockinst->list_all );
            atomic_dec( &g_mwsocket_state.sockinst_count );

            mutex_unlock( &g_mwsocket_state.sockinst_lock );

            kmem_cache_free( g_mwsocket_state.sockinst_cache, sockinst );
        }
        *SockInst = NULL;
    }

    return rc;
}


/**
 * @brief Close the remote socket.
 *
 * @return 0 on success, or error, which could be either local or remote
 */
static int
MWSOCKET_DEBUG_ATTRIB
mwsocket_close_remote( IN mwsocket_instance_t * SockInst,
                       IN bool                  WaitForResponse )
{
    int rc = 0;
    mwsocket_active_request_t * actreq = NULL;
    mt_request_socket_close_t * close = NULL;

    if( !MW_SOCKET_IS_FD( SockInst->remote_fd ) )
    {
        MYASSERT( !"Not an MW socket" );
        rc = -EINVAL;
        goto ErrorExit;
    }

    if( SockInst->remote_close_requested )
    {
        pr_info( "Socket %llx/%d was already closed on the INS. "
                  "Not requesting a remote close.\n",
                  SockInst->remote_fd, SockInst->local_fd );
        goto ErrorExit;
    }

    // Record that close() requested, regardless of results
    SockInst->remote_close_requested = true;
    SockInst->mwflags |= MWSOCKET_FLAG_CLOSED;

    rc = mwsocket_create_active_request( SockInst, &actreq );
    if( rc ) { goto ErrorExit; }

    pr_debug( "Request %lx is closing socket %llx/%d and %s wait\n",
              (unsigned long)actreq->id, SockInst->remote_fd, SockInst->local_fd,
              WaitForResponse ? "will" : "won't" );

    actreq->deliver_response = WaitForResponse;
    close = &actreq->rr.request.socket_close;
    close->base.type   = MtRequestSocketClose;
    close->base.size   = MT_REQUEST_SOCKET_CLOSE_SIZE;

    rc = mwsocket_send_request( actreq, true );
    if( rc ) { goto ErrorExit; }

    if( !WaitForResponse ) { goto ErrorExit; } // no more work

    rc = wait_for_completion_timeout( &actreq->arrived, GENERAL_RESPONSE_TIMEOUT );
    if( 0 == rc )
    {
        rc = -ETIME;
        pr_warn( "Timed out while waiting for response to close\n" );
    }
    else
    {
        pr_debug( "Successfully waited for close to complete\n" );
        rc = 0;
    }

    if( 0 == rc ) { rc = actreq->rr.response.base.status; }

ErrorExit:
    mwsocket_destroy_active_request( actreq );
    return rc;
}


static void
MWSOCKET_DEBUG_ATTRIB
mwsocket_destroy_active_request( mwsocket_active_request_t * Request )
{
    if( NULL == Request )
    {
        return;
    }

    pr_verbose( "Destroyed active request %p id=%lx\n",
                Request, (unsigned long) Request->id );

    // Clear the ID of the blocker to close, iff it is this request's
    atomic64_cmpxchg( &Request->sockinst->close_blockid,
                      Request->id, MT_ID_UNSET_VALUE );

    // Request->list_inbound should be empty or invalidated. How to
    // check for either of these?

    mwsocket_put_sockinst( Request->sockinst );

    // Remove from global list
    mutex_lock( &g_mwsocket_state.active_request_lock );
    list_del( &Request->list_all );
    mutex_unlock( &g_mwsocket_state.active_request_lock );

    kmem_cache_free( g_mwsocket_state.active_request_cache, Request );
}


/**
 * @brief Gets a new active request struct from the cache and does basic init.
 */
static int 
MWSOCKET_DEBUG_ATTRIB
mwsocket_create_active_request( IN mwsocket_instance_t * SockInst,
                                OUT mwsocket_active_request_t ** ActReq )
{
    mwsocket_active_request_t * actreq = NULL;
    int                          rc = 0;

    MYASSERT( SockInst );
    MYASSERT( ActReq );

    if( !SockInst->ins_alive )
    {
        pr_debug( "Not creating active request for dead INS" );
        rc = -EINVAL;
        goto ErrorExit;
    }

    actreq = (mwsocket_active_request_t * )
        kmem_cache_alloc( g_mwsocket_state.active_request_cache,
                          GFP_KERNEL | __GFP_ZERO );
    if( NULL == actreq )
    {
        MYASSERT( !"kmem_cache_alloc failed" );
        rc = -ENOMEM;
        goto ErrorExit;
    }

    // Success
    bzero( actreq, sizeof( *actreq ) );

    actreq->issuer = current;

    init_completion( &actreq->arrived );

    mwsocket_get_sockinst( SockInst );
    actreq->sockinst = SockInst;

    // Add the new active request to the global list
    mutex_lock( &g_mwsocket_state.active_request_lock );

    actreq->id = mwsocket_get_next_id();

    list_add( &actreq->list_all,
              &g_mwsocket_state.active_request_list );

    mutex_unlock( &g_mwsocket_state.active_request_lock );

    INIT_LIST_HEAD( &actreq->list_inbound );

    pr_verbose( "Created active request %p id=%lx\n",
                actreq, (unsigned long)actreq->id );

    *ActReq = actreq;

ErrorExit:
    return rc;
}


static int
MWSOCKET_DEBUG_ATTRIB
mwsocket_find_active_request_by_id( OUT mwsocket_active_request_t ** Request,
                                    IN  mt_id_t                      Id )
{
    int rc = -ENOENT;
    mwsocket_active_request_t * curr = NULL;

    MYASSERT( Request );
    *Request = NULL;

    mutex_lock( &g_mwsocket_state.active_request_lock );
    list_for_each_entry( curr, &g_mwsocket_state.active_request_list, list_all )
    {
        if( curr->id == Id )
        {
            // XXXX: Ideally we could lock the socket here to
            // guarantee per-socket concurrency
            *Request = curr;
            rc = 0;
            break;
        }
    }
    mutex_unlock( &g_mwsocket_state.active_request_lock );

    if( rc ) { goto ErrorExit; }

    MYASSERT( *Request );

    // We found the active request but its socket is dead. This
    // happens when the host process dies unexpectedly. Fail the
    // request.
    // XXXX: A mutex could provide a cleaner way to avoid working
    // on a dead socket.
    if( (*Request)->sockinst->mwflags & MWSOCKET_FLAG_RELEASED )
    {
        pr_debug( "Found request ID=%lx for dead socket %lx/%d.\n",
                  (unsigned long) Id,
                  (unsigned long) (*Request)->sockinst->remote_fd,
                  (*Request)->sockinst->local_fd );
//        MYASSERT( !"Response for dead socket" );
//        rc = -ECANCELED;
    }

ErrorExit:
    return rc;
}


void
MWSOCKET_DEBUG_ATTRIB
mwsocket_debug_dump_actreq( void )
{
    mwsocket_active_request_t * curr = NULL;

    mutex_lock( &g_mwsocket_state.active_request_lock );
    list_for_each_entry( curr, &g_mwsocket_state.active_request_list, list_all )
    {
        mt_request_generic_t * request = &curr->rr.request;

        pr_info( "%lx: %p sockinst %p fd %d type %x\n",
                 (unsigned long)curr->id, curr,
                 curr->sockinst, curr->sockinst->local_fd,
                 request->base.type );
    }
    mutex_unlock( &g_mwsocket_state.active_request_lock );
}


/**
 * @brief Delivers signal to current process
 *
 * @return Returns pending error, or 0 if none
 */
static int
MWSOCKET_DEBUG_ATTRIB
mwsocket_pending_error( mwsocket_instance_t * SockInst,
                        mt_request_type_t     RequestType )
{
    int rc = 0;

    MYASSERT( SockInst );

    // These messages are exempt from pending errors, since they must
    // reach the INS.
    if( MtRequestSocketShutdown == RequestType 
        || MtRequestSocketClose == RequestType )
    {
        goto ErrorExit;
    }

    if( SockInst->remote_surprise_close )
    {
        // Once set, remote_surprise_close is never cleared in a sockinst.
        rc = -MW_ERROR_REMOTE_SURPRISE_CLOSE;

        // Operations behave differently when the remote side goes down...
        if( MtRequestSocketSend == RequestType )
        {
            pr_debug( "Delivering SIGPIPE to process %d (%s) for socket %d\n",
                      SockInst->proc->pid, SockInst->proc->comm,
                      SockInst->local_fd );
            send_sig( SIGPIPE, current, 0 );
        }
    }
    else if( SockInst->pending_errno )
    {
        rc = SockInst->pending_errno;

        pr_debug( "Delivering pending error %d to process %d (%s) for socket %d\n",
                  rc,
                  SockInst->proc->pid, SockInst->proc->comm,
                  SockInst->local_fd );
        // Clear the pending errno. The remote_surprise_close doesn't
        // get reset, but this does.
        SockInst->pending_errno = 0;
    }

ErrorExit:
    return rc;
}


static void
MWSOCKET_DEBUG_ATTRIB
mwsocket_populate_netflow_ip( IN struct sockaddr * SockAddr,
                              OUT mw_endpoint_t *  Endpoint )
{
    MYASSERT( SockAddr );
    MYASSERT( Endpoint );

    struct sockaddr_in  * sa4 = (struct sockaddr_in *)  SockAddr;
    struct sockaddr_in6 * sa6 = (struct sockaddr_in6 *) SockAddr;

    switch( SockAddr->sa_family )
    {
        // N.B. items in sockaddr struct are already in network byte order
    case AF_INET:
        Endpoint->addr.af = __cpu_to_be32( 4 );
        Endpoint->port = sa4->sin_port;
        memcpy( &Endpoint->addr.a, &sa4->sin_addr, sizeof( sa4->sin_addr ) );
        break;
    case AF_INET6:
        Endpoint->addr.af = __cpu_to_be32( 6 );
        Endpoint->port = sa6->sin6_port;
        // Addr in struct is network-ordered
        memcpy( &Endpoint->addr.a, &sa6->sin6_addr, sizeof( sa6->sin6_addr ) );
        break;
    default:
        MYASSERT( !"Invalid address family" );
        bzero( Endpoint, sizeof( *Endpoint ) );
    }
}


static void
MWSOCKET_DEBUG_ATTRIB
mwsocket_populate_netflow( IN mwsocket_active_request_t * ActiveRequest,
                           OUT mw_netflow_info_t        * Netflow )
{
    MYASSERT( Netflow );
    MYASSERT( ActiveRequest );

    mwsocket_instance_t * sockinst = ActiveRequest->sockinst;
    struct timespec ts;
    getnstimeofday( &ts );

    Netflow->base.sig = __cpu_to_be16( MW_MESSAGE_SIG_NETFLOW_INFO );

    Netflow->ts_session_start.sec  = __cpu_to_be64( sockinst->est_time.tv_sec );
    Netflow->ts_session_start.ns   = __cpu_to_be64( sockinst->est_time.tv_nsec );

    Netflow->ts_curr.sec  = __cpu_to_be64( ts.tv_sec );
    Netflow->ts_curr.ns   = __cpu_to_be64( ts.tv_nsec );

    Netflow->sockfd   = __cpu_to_be32( sockinst->remote_fd );

    mwsocket_populate_netflow_ip( &sockinst->local_bind, &Netflow->pvm );
    mwsocket_populate_netflow_ip( &sockinst->peer,       &Netflow->remote );

    Netflow->bytes_in  = __cpu_to_be64( sockinst->tot_recv );
    Netflow->bytes_out = __cpu_to_be64( sockinst->tot_sent );
}


static void
MWSOCKET_DEBUG_ATTRIB
mwsocket_postproc_emit_netflow( mwsocket_active_request_t * ActiveRequest,
                                mt_response_generic_t     * Response )
{
    MYASSERT( ActiveRequest );
    MYASSERT( ActiveRequest->sockinst );
    MYASSERT( Response );

    bool drop = false;
    mw_netflow_info_t nf = {0};
    int obs = MwObservationNone;

    if( !mw_netflow_consumer_exists() )
    {
        goto ErrorExit;
    }

    switch( Response->base.type )
    {
    case MtResponseSocketCreate:
        obs = MwObservationCreate;
        break;
    case MtResponseSocketClose:
        obs = MwObservationClose;
        break;
    case MtResponseSocketConnect:
        obs = MwObservationConnect;
        break;
    case MtResponseSocketBind:
        obs = MwObservationBind;
        break;
    case MtResponseSocketAccept: // "extra" field holds new sockfd
        obs = MwObservationAccept;
        nf.extra = __cpu_to_be64( Response->socket_accept.base.sockfd );
        break;
    case MtResponseSocketSend:
        obs = MwObservationSend;
        break;
    case MtResponseSocketRecv:
    case MtResponseSocketRecvFrom:
        obs = MwObservationRecv;
        break;
    case MtResponseSocketShutdown:

    case MtResponseSocketListen:
    case MtResponseSocketGetName:
    case MtResponseSocketGetPeer:
    case MtResponseSocketAttrib:
    case MtResponsePollsetQuery:
        // Ignored cases
        drop = true;
        break;
    default:
        drop = true;
        MYASSERT( !"Unhandled response" );
        break;
    }

    if( drop ) { goto ErrorExit; }

    MYASSERT( sizeof(mw_obs_space_t) == sizeof(uint16_t) );
    nf.obs = __cpu_to_be16( (mw_obs_space_t) obs );

    mwsocket_populate_netflow( ActiveRequest, &nf );

    mw_netflow_write_all( &nf, sizeof(nf) );
    pr_debug( "Wrote netflow info about request %lx\n",
              (unsigned long) ActiveRequest->id );

ErrorExit:
    return;
}


static void
MWSOCKET_DEBUG_ATTRIB
mwsocket_postproc_no_context( mwsocket_active_request_t * ActiveRequest,
                              mt_response_generic_t     * Response )
{
    MYASSERT( ActiveRequest );
    MYASSERT( ActiveRequest->sockinst );
    MYASSERT( Response );

    int status = Response->base.status;

    if( MT_CLOSE_WAITS( Response ) )
    {
        if( DEBUG_SHOW_TYPE( Response->base.type ) )
        {
            pr_verbose( "close() against fd %d no longer awaiting %lx completion\n",
                        ActiveRequest->sockinst->local_fd,
                        (unsigned long)ActiveRequest->id );
        }

        // Clear the ID of the blocker to close, iff it is this request's
        atomic64_cmpxchg( &ActiveRequest->sockinst->close_blockid,
                          (unsigned long) ActiveRequest->id,
                          (unsigned long) MT_ID_UNSET_VALUE );
        pr_debug( "up_read(close_lock), sockfd=%d\n", ActiveRequest->sockinst->local_fd );
        MYASSERT( rwsem_is_locked( &ActiveRequest->sockinst->close_lock ) );

        up_read( &ActiveRequest->sockinst->close_lock );
    }
    else if( MtResponseSocketClose == Response->base.type
              && ActiveRequest->sockinst->close_lock_write )
    {
        pr_debug( "up_write(close_lock), sockfd=%d\n", ActiveRequest->sockinst->local_fd );
        MYASSERT( rwsem_is_locked( &ActiveRequest->sockinst->close_lock ) );

        up_write( &ActiveRequest->sockinst->close_lock );
        ActiveRequest->sockinst->close_lock_write = false;
    }

    if( MtResponseSocketSend == Response->base.type )
    {
        // If we're on the final response, clear out the state and
        // return the total sent, whether or not this succeeded.
        if( (Response->base.flags & _MT_FLAGS_BATCH_SEND_FINI) )
        {
            ActiveRequest->sockinst->u_flags &= ~(_MT_FLAGS_BATCH_SEND_INIT
                                                  |_MT_FLAGS_BATCH_SEND
                                                  |_MT_FLAGS_BATCH_SEND_FINI);
            if( Response->socket_send.count >= 0 )
            {
                Response->socket_send.count += ActiveRequest->sockinst->send_tally;
            }
            pr_debug( "Final batch send: total sent is %d bytes\n",
                      Response->socket_send.count );
        }
        else if( Response->base.flags & _MT_FLAGS_BATCH_SEND )
        {
            MYASSERT( ActiveRequest->sockinst->u_flags & _MT_FLAGS_BATCH_SEND );
            if( Response->socket_send.count > 0 )
            {
                ActiveRequest->sockinst->send_tally += Response->socket_send.count;
            }
            pr_debug( "Batch send: total %d this %d\n",
                      (int)ActiveRequest->sockinst->send_tally,
                      (int)Response->socket_send.count );
        }
    }
    
    // In case the request failed and the requestor will not process
    // the response, we have to inform the user of the error in some
    // other way. We will deliver an error or a SIGPIPE the next time
    // the user interacts with the subject mwsocket, depending on the
    // type of interaction. Rely on the INS to populate the flags
    // correctly in case of surprise remote close. This approach might
    // report an errno that is not expected for the call the user is
    // making.

    if( Response->base.flags & _MT_FLAGS_REMOTE_CLOSED
        && !ActiveRequest->sockinst->remote_surprise_close )
    {
        ActiveRequest->sockinst->remote_surprise_close = true;
        pr_debug( "Remote side of fd %d (pid %d [%s]) closed. "
                  "Next recv/send call will indicate this.\n",
                  ActiveRequest->sockinst->local_fd,
                  ActiveRequest->sockinst->proc->pid,
                  ActiveRequest->sockinst->proc->comm );
    }

    if( MtResponseSocketAccept == Response->base.type )
    {
        // No longer accepting on this sockinst
        ActiveRequest->sockinst->mwflags &= ~MWSOCKET_FLAG_ACCEPT;

        MYASSERT( ActiveRequest->deliver_response );

        // Put this AR in the inbound queue. Copy in the response now.
        memcpy( &ActiveRequest->rr.response, Response, Response->base.size );
        ActiveRequest->response_populated = true;

        mwsocket_instance_t * primary = ActiveRequest->sockinst->usersock;
        MYASSERT( primary->mwflags & MWSOCKET_FLAG_USER );

        mutex_lock( &primary->inbound_lock );
        list_add_tail( &ActiveRequest->list_inbound, &primary->inbound_list );
        mutex_unlock( &primary->inbound_lock );

        // XXXX: could re-use ActiveRequest->arrived, but it would
        // have to be reset after each usage.
        up( &primary->inbound_sem );
    }

    // N.B. Do not use ActiveRequest->response -- it might not be valid yet.
    if( status < 0 )
    {
        goto ErrorExit;
    }

    // Success case
    switch( Response->base.type )
    {
    // case MtResponseSocketAccept: //-- handled in mwsocket_postproc_in_task()
    case MtResponseSocketCreate:
        pr_debug( "Create in %d [%s]: fd %d ==> %llx\n",
                  ActiveRequest->sockinst->proc->pid,
                  ActiveRequest->sockinst->proc->comm,
                  ActiveRequest->sockinst->local_fd,
                  Response->base.sockfd );

        ActiveRequest->sockinst->remote_fd     = Response->base.sockfd;
        break;
    case MtResponseSocketListen:
        ActiveRequest->sockinst->mwflags |= MWSOCKET_FLAG_LISTENING;
        break;
    case MtResponseSocketBind:
        ActiveRequest->sockinst->mwflags |= MWSOCKET_FLAG_BOUND;
        break;
    case MtResponseSocketConnect:
        ActiveRequest->sockinst->mwflags |= MWSOCKET_FLAG_CONNECTED;
        break;
    case MtResponseSocketSend:
        ActiveRequest->sockinst->tot_sent += Response->socket_send.count;
        break;
    case MtResponseSocketRecv:
        ActiveRequest->sockinst->tot_recv += Response->socket_recv.count;
        break;
    case MtResponseSocketRecvFrom:
        ActiveRequest->sockinst->tot_recv += Response->socket_recvfrom.count;
        break;
    default:
        break;
    }

ErrorExit:
    return;
}


/**
 * @brief Post-process the response in the context of the process using the response
 *
 * Useful especially for dealing with successful call to accept(),
 * which creates a new socket.
 */
static int
MWSOCKET_DEBUG_ATTRIB
mwsocket_postproc_in_task( IN mwsocket_active_request_t * ActiveRequest,
                           IN mt_response_generic_t     * Response )
{
    int rc = 0;
    mwsocket_instance_t * acceptinst = NULL;

    MYASSERT( ActiveRequest );
    MYASSERT( ActiveRequest->sockinst );
    MYASSERT( Response );

    if( MtResponseSocketAccept != Response->base.type
        || Response->base.status < 0 )
    {
        goto ErrorExit;
    }

    // accept() has suuccessfully returned. Populate the new
    // sockinst's flags from the original request, which the INS
    // carried over for us
    rc = mwsocket_create_sockinst( &acceptinst,
                                   Response->socket_accept.flags,
                                   true,
                                   true );
    if( rc )
    {
        // The socket was created remotely but the local creation
        // failed. We cannot clean up the remote side because we don't
        // have the backing sockinst to send the close request.
        pr_err( "Failed to create local socket instance. "
                "Leaking remote socket %llx; local error %d\n",
                Response->base.sockfd, rc );
        goto ErrorExit;
    }

    // The local creation succeeded. Update Response to reflect the
    // local socket.
    acceptinst->remote_fd = Response->base.sockfd;

    // It's being given to the user
    acceptinst->mwflags |= MWSOCKET_FLAG_USER;

    // Copy the local bind info, so we know the bind point of this
    // accepted socket. Needed for netflow.
    acceptinst->local_bind = ActiveRequest->sockinst->local_bind;

    // Populate peer info here
    acceptinst->peer.sa_family =
        xe_net_get_native_protocol_family( Response->socket_accept.sockaddr.sin_family );
    // XXXX: support IPv6
    struct sockaddr_in * sa4 =
        (struct sockaddr_in *)(&acceptinst->peer);
    sa4->sin_addr = *(struct in_addr *) &Response->socket_accept.sockaddr.sin_addr;
    sa4->sin_port = Response->socket_accept.sockaddr.sin_port;

    // Update Response to reflect the local socket.
    acceptinst->remote_fd = Response->base.sockfd;

    Response->base.sockfd
        = Response->base.status
        = acceptinst->local_fd;

    pr_debug( "Accept in %d [%s]: fd %d ==> %llx\n",
              acceptinst->proc->pid, acceptinst->proc->comm,
              acceptinst->local_fd, acceptinst->remote_fd );
ErrorExit:
    return rc;
}


/**
 * @brief Prepares request and socket instance state prior to putting
 * request on ring buffer.
 *
 * Enforces close lock: some operations cannot be started while a close
 * is in progress.
 */
static int
MWSOCKET_DEBUG_ATTRIB
mwsocket_pre_process_request( mwsocket_active_request_t * ActiveRequest )
{
    MYASSERT( ActiveRequest );
    MYASSERT( ActiveRequest->sockinst );

    int rc = 0;
    mt_request_generic_t * request = &ActiveRequest->rr.request;

    MYASSERT( MT_IS_REQUEST( request ) );

    // Will the user wait for the response to this request? If so, update state
    if( MT_REQUEST_CALLER_WAITS( request ) )
    {
        // This write() will be followed immediately by a
        // read(). Indicate the ID that blocks the calling thread.
        ActiveRequest->sockinst->read_expected = true;
        ActiveRequest->sockinst->blockid = ActiveRequest->id;
        ActiveRequest->deliver_response = true;
    }

    if( MT_CLOSE_WAITS( request ) )
    {
        down_read( &ActiveRequest->sockinst->close_lock );

        atomic64_set( &ActiveRequest->sockinst->close_blockid,
                      (unsigned long) ActiveRequest->id );
    }
    else if( MtRequestSocketClose == request->base.type )
    {
        bool waiting = false;
        mt_id_t id = (mt_id_t)
            atomic64_read( &ActiveRequest->sockinst->close_blockid );
        if( MT_ID_UNSET_VALUE != id )
        {
            pr_info( "close() against fd %d [pid %d] waiting for in-flight ID %lx\n",
                     ActiveRequest->sockinst->local_fd,
                     ActiveRequest->sockinst->proc->pid,
                     (unsigned long)id );
            waiting = true;
        }

        // Wait for completion of all in-flight IO that close must
        // wait on. This sockinst is being destroyed, so we won't
        // release this lock. Once it is acquired, regard the socket
        // as dead. Do not wait forever.

#if 0 // XXXX: update to this when we're on a more recent kernel ( > 4.4.0-87 )
        rc = down_write_killable( &ActiveRequest->sockinst->close_lock );
        if( -EINTR == rc )
        {
            pr_info( "Wait for in-flight ID %lx was killed\n", (unsigned long)id );
        }
#else
        int ct = 0;
        bool acquired = false;
        while( ct++ < 2 )
        {
            if( down_write_trylock( &ActiveRequest->sockinst->close_lock ) )
            {
                acquired = true;
                break;
            }
            mwsocket_wait( GENERAL_RESPONSE_TIMEOUT );
        }

        if( !acquired )
        {
            pr_warning( "Closing socket %d [pid %d] despite outstanding IO \
                         on it (ID %lx).\n",
                        ActiveRequest->sockinst->local_fd,
                        ActiveRequest->sockinst->proc->pid,
                        (unsigned long)id );
        }
#endif
        atomic64_set( &ActiveRequest->sockinst->close_blockid,
                      MT_ID_UNSET_VALUE );
    }

    switch( request->base.type )
    {
    case MtRequestSocketCreate:
        ActiveRequest->sockinst->sock_fam      = request->socket_create.sock_fam;
        ActiveRequest->sockinst->sock_type     = request->socket_create.sock_type;
        ActiveRequest->sockinst->sock_protocol = request->socket_create.sock_protocol;
        break;

    case MtRequestSocketListen:
        ActiveRequest->sockinst->backlog = request->socket_listen.backlog;
        break;

    case MtRequestSocketBind:
        ActiveRequest->sockinst->bind_sockaddr = request->socket_bind.sockaddr;
        break;

    case MtRequestSocketAccept:
        ActiveRequest->sockinst->mwflags     |= MWSOCKET_FLAG_ACCEPT;
        ActiveRequest->sockinst->accept_flags = request->socket_accept.flags;
        // If we're accepting and a sibling socket is active, then
        // route the accept() to the INS that's waiting
        if( ActiveRequest->sockinst->poll_active &&
            (ActiveRequest->sockinst->poll_events & POLLIN) )
        {
            request->base.sockfd =
                ActiveRequest->sockinst->poll_active->remote_fd;
        }
        break;

    case MtRequestSocketSend:
        // Handle incoming scatter-gather send requests

        pr_debug( "send flags: %x\n", request->base.flags );
        // Is this the first scatter-gather request in a possible
        // series? We could also check _MT_FLAGS_BATCH_SEND_INIT.
        if( !( ActiveRequest->sockinst->u_flags & _MT_FLAGS_BATCH_SEND )
             && (request->base.flags & _MT_FLAGS_BATCH_SEND) )
        {
            pr_debug( "Starting batch send\n" );
            ActiveRequest->sockinst->u_flags |= _MT_FLAGS_BATCH_SEND;
            ActiveRequest->sockinst->send_tally = 0;
        }

        // Check: is this the final request but without synchronization?
        if( (request->base.flags & _MT_FLAGS_BATCH_SEND_FINI)
             && !MT_REQUEST_CALLER_WAITS( request ) )
        {
            pr_warning( "Request concludes send scatter-gather, but caller "
                        "isn't waiting for response. fd=%d, pid=%d\n",
                        ActiveRequest->sockinst->local_fd,
                        ActiveRequest->sockinst->proc->pid );
        }
        break;
    default:
        break;
    }

    return rc;
}


static int
MWSOCKET_DEBUG_ATTRIB
mwsocket_await_inbound_connection( IN mwsocket_active_request_t   * ActiveRequest,
                                   OUT mwsocket_active_request_t ** InboundRequest )
{
    MYASSERT( ActiveRequest );
    MYASSERT( InboundRequest );

    int rc = 0;
    mwsocket_instance_t * sockinst = ActiveRequest->sockinst;

    MYASSERT( sockinst );
    MYASSERT( sockinst->mwflags & MWSOCKET_FLAG_USER );

    if( down_interruptible( &sockinst->inbound_sem ) )
    {
        rc = -EINTR;
        pr_info( "Received interrupt while awaiting inbound connection\n" );
        goto ErrorExit;
    }

    // Get the first inbound connection.
    mutex_lock( &sockinst->inbound_lock );
    *InboundRequest = list_first_entry( &sockinst->inbound_list,
                                        mwsocket_active_request_t,
                                        list_inbound );
    list_del( &(*InboundRequest)->list_inbound );
    mutex_unlock( &sockinst->inbound_lock );

ErrorExit:
    return rc;
}


static int
MWSOCKET_DEBUG_ATTRIB
mwsocket_send_request( IN mwsocket_active_request_t * ActiveRequest,
                       IN bool                        WaitForRing )
{
    int                    rc   = 0;
    mt_request_generic_t * req  = NULL;
    void                 * h    = NULL;
    mt_request_base_t    * base = NULL;

    MYASSERT( ActiveRequest );

    base = &ActiveRequest->rr.request.base;

    // Perform minimal base field prep. Only clobber an unset sockfd.
    base->sig    = MT_SIGNATURE_REQUEST;
    base->id     = ActiveRequest->id;
    if( !MW_SOCKET_IS_FD( base->sockfd ) )
    {
        pr_debug( "Updating sockfd: %lx => %lx\n",
                  (unsigned long) base->sockfd,
                  (unsigned long) ActiveRequest->sockinst->remote_fd );
        base->sockfd = ActiveRequest->sockinst->remote_fd;
    }

    // Either we don't have a backing remote socket, or else the remote FD is valid
    MYASSERT( MtRequestSocketCreate == base->type ||
              MtRequestPollsetQuery == base->type ||
              MW_SOCKET_IS_FD( base->sockfd ) );

    // Hold this for duration of the operation. 
    mutex_lock( &g_mwsocket_state.request_lock );

    if( !MT_IS_REQUEST( &ActiveRequest->rr.request ) )
    {
        MYASSERT( !"Invalid request given\n" );
        rc = -EINVAL;
        goto ErrorExit;
    }

    if( !ActiveRequest->sockinst->ins_alive )
    {
        rc = -ESTALE;
        pr_debug( "Not sending request %llx to dead INS\n",
                  (unsigned long long) ActiveRequest->id );
        mwsocket_set_pollable( ActiveRequest->sockinst, false );
        goto ErrorExit;
    }

    // Given the client ID, get a request slot: the pointer to the
    // request buffer and an opaque handle
    rc = mw_xen_get_next_request_slot( WaitForRing,
                                       MW_SOCKET_CLIENT_ID( base->sockfd ),
                                       &req,
                                       &h );
    if( rc )
    {
        if( WaitForRing )
        {
            // We were willing to wait for the ring, but we failed
            // anyway. Assume the INS is dead. Propogate this news as
            // needed.
            pr_info( "Marking INS %d as dead\n",
                     MW_SOCKET_CLIENT_ID( base->sockfd ) );
            ActiveRequest->sockinst->ins_alive = false;
            mwsocket_set_pollable( ActiveRequest->sockinst, false );
        }
        goto ErrorExit;
    }

    // Populate the request further as needed. Do this only when we're
    // certain the send will succeed, as it modifies the sockinst
    // state such that the system will fail if a response is not
    // received.
    rc = mwsocket_pre_process_request( ActiveRequest );
    if( rc ) { goto ErrorExit; }

    if( DEBUG_SHOW_TYPE( ActiveRequest->rr.request.base.type ) )
    {
        pr_debug( "Sending request %lx fd %lx/%d type %x\n",
                  (unsigned long)ActiveRequest->rr.request.base.id,
                  (unsigned long)ActiveRequest->rr.request.base.sockfd,
                  ActiveRequest->sockinst->local_fd,
                  ActiveRequest->rr.request.base.type );
    }

    memcpy( (void *) req,
            &ActiveRequest->rr.request,
            ActiveRequest->rr.request.base.size );

    rc = mw_xen_dispatch_request( h );
    // Fall-through

ErrorExit:
    mutex_unlock( &g_mwsocket_state.request_lock );

    return rc;
}


/**
 * @brief Sends the message based on the socket instance.
 *
 * @returns status from sending, or status from response if
 * AwaitResponse is true.
 */
static int
MWSOCKET_DEBUG_ATTRIB
mwsocket_send_message( IN mwsocket_instance_t  * SockInst,
                       IN mt_request_generic_t * Request,
                       IN bool                   AwaitResponse )
{
    int rc = 0;
    int remoterc = 0;
    mwsocket_active_request_t * actreq = NULL;

    MYASSERT( SockInst );
    MYASSERT( Request );

    rc = mwsocket_create_active_request( SockInst, &actreq );
    if( rc ) { goto ErrorExit; }

    actreq->deliver_response = AwaitResponse;

    memcpy( &actreq->rr.request, Request, Request->base.size );

    rc = mwsocket_send_request( actreq, true );
    if( rc ) { goto ErrorExit; }

    if( !AwaitResponse ) { goto ErrorExit; }

    rc = wait_for_completion_interruptible_timeout( &actreq->arrived,
                                                    GENERAL_RESPONSE_TIMEOUT );
    if( 0 == rc )
    {
        rc = -ETIME;
        goto ErrorExit;
    }
    if( rc < 0 )
    {
        MYASSERT( !"Interrupted" );
        goto ErrorExit;
    }

    // Success
    rc = 0;
    remoterc = -actreq->rr.response.base.status;
    pr_debug( "Response arrived: %lx\n", (unsigned long)actreq->id );

ErrorExit:
    if( remoterc )
    {
        rc = remoterc;
    }

    return rc;
}


/**
 * Public function - see mwcomms-socket.h.
 *
 * Uses ring buffer to fulfill given request. Waits for response and
 * popualates it.
 */
int
MWSOCKET_DEBUG_ATTRIB
mwsocket_send_bare_request( IN    mt_request_generic_t  * Request,
                            INOUT mt_response_generic_t * Response )
{
    int rc = 0;
    mwsocket_instance_t      * sockinst = NULL;
    mwsocket_active_request_t  * actreq = NULL;

    if( Request->base.size > sizeof( *Request ) )
    {
        MYASSERT( !"Request size too big" );
        rc = -EINVAL;
        goto ErrorExit;
    }

    // XXXX: wrong???? find the sockinst by the embedded domid?
    rc = mwsocket_find_sockinst_by_remote_fd( &sockinst, Request->base.sockfd );
    if( rc ) { goto ErrorExit; }

    rc = mwsocket_create_active_request( sockinst, &actreq );
    if( rc ) { goto ErrorExit; }

    actreq->deliver_response = true;
    memcpy( &actreq->rr.request, Request, Request->base.size );

    rc = mwsocket_send_request( actreq, true );
    if( rc ) { goto ErrorExit; }

    // Copy the response for the caller, provided we have enough space for it.
    // XXXX: is this check needed?
    if( actreq->rr.response.base.size > Response->base.size )
    {
        rc = -ENOSPC;
        MYASSERT( !"Insufficient space given for response" );
        goto ErrorExit;
    }
    else
    {
        memcpy( (void *) Response,
                &actreq->rr.response,
                actreq->rr.response.base.size );
    }

ErrorExit:
    mwsocket_destroy_active_request( actreq );
    return rc;
}


/******************************************************************************
 * Main functions for worker threads. There are two: the response
 * consumer, and the poll monitor.
 ******************************************************************************/

static int
//MWSOCKET_DEBUG_ATTRIB // yep, enable optimization here: ugh!
mwsocket_response_consumer( void * Arg )
{
    int                         rc = 0;
    void                      * h = NULL;
    mwsocket_active_request_t * actreq = NULL;
    mt_response_generic_t     * response = NULL;
    bool                        available = false;

    // TODO
    // Wait for there to be a ring available for use
    rc = wait_for_completion_interruptible( &g_mwsocket_state.xen_iface_ready );
    if( rc < 0 )
    {
        pr_info( "Received interrupt before ring ready\n" );
        goto ErrorExit;
    }

    // Completion succeeded
    if( g_mwsocket_state.pending_exit )
    {
        pr_info( "Detecting pending exit. Worker thread exiting.\n" );
        goto ErrorExit;
    }

    //
    // Consume responses until the module is unloaded. When it is
    // unloaded, consume whatever is still on the ring, then
    // quit. Only leave this loop upon requested exit or fatal error.
    //
    // Policy: continue upon error.
    // Policy: in case of pending exit, keep consuming the requests
    //         until there are no more
    //

    pr_debug( "Entering response consumer loop\n" );

    while( true )
    {
        do
        {
            available = mw_xen_response_available( &h );
            if( !available )
            {
                if( g_mwsocket_state.pending_exit ) { goto ErrorExit; }

                //
                // pr_verbose( "Waiting for signal on event channel\n" );
                if( down_interruptible( &g_mwsocket_state.event_channel_sem ) )
                {
                    rc = -EINTR;
                    pr_info( "Received interrupt in response consumer thread\n" );
                    goto ErrorExit;
                }
            }
        } while (!available);

        // N.B. only fails if ring is corrupt
        rc = mw_xen_get_next_response( &response, h );
        if( rc ) { goto ErrorExit; }


        MYASSERT( !( response->base.size > MESSAGE_TARGET_MAX_SIZE ) );
        
        if( DEBUG_SHOW_TYPE( response->base.type ) )
        {
            pr_debug( "Response ID %lx size %x type %x status %d\n",
                      (unsigned long)response->base.id,
                      response->base.size, response->base.type,
                      response->base.status );
        }

        if( g_mwsocket_state.pending_exit )
        {
            //NULL response means pending exit was detected
            pr_debug("Pending exit detected, shutting down "
                     "response conusmer thread\n" );
            goto ErrorExit;
        }

        // Hereafter advance index as soon as we're done with the item
        // in the ring buffer.
        rc = mwsocket_find_active_request_by_id( &actreq, response->base.id );
        if( rc )
        {
            // Active requests should *never* just "disappear". They
            // are destroyed in an orderly fashion either here (below)
            // or in mwsocket_read(). This state can be reached if a
            // requestor times out on the response arrival.
            pr_warn( "Couldn't find active request with ID %lx\n",
                     (unsigned long) response->base.id );
            MYASSERT( !"Unknown/expired response received" );
            mw_xen_mark_response_consumed( h );
            continue; // move on
        }

        //
        // The active request has been found.
        //
        mwsocket_postproc_no_context( actreq, response );

        mwsocket_postproc_emit_netflow( actreq, response );

        // Decide the fate of the request. It is either delivered to
        // the caller or destroyed, depending on the caller's request
        // and status.
        //
        // We destroy the response if either (a) the caller doesn't
        // want the response, or (b) the caller is dying and isn't
        // between a fork() and exec(). In the case of (b), if the
        // caller is waiting on the response, its wait will be
        // interrupted so we can just destroy the response and not
        // signal completion. See "Understanding the Linux Kernel",
        // 3e, pages 127 and 831.

        if( !actreq->deliver_response
             || (actreq->from_user
                 && (actreq->sockinst->proc->flags & PF_EXITING)
                 && !(actreq->sockinst->proc->flags & PF_FORKNOEXEC) ) )
        {
            if( actreq->deliver_response )
            {
                pr_info( "Not delivering response %lx for fd=%d pid=%d "
                         "because process is dying\n",
                         (unsigned long) actreq->id,
                         actreq->sockinst->local_fd,
                         actreq->sockinst->proc->pid );
                MYASSERT( !(actreq->sockinst->proc->flags & PF_FORKNOEXEC) );
            }
            mwsocket_destroy_active_request( actreq );
        }
        else 
        {
            // The caller is healthy and wants the response. Copy
            // response if needed then notify any waiter.
            if( !actreq->response_populated )
            {
                memcpy( &actreq->rr.response, response, response->base.size );
                actreq->response_populated = true;
            }
            complete( &actreq->arrived );
        }

        // We're done with this slot of the ring
        mw_xen_mark_response_consumed( h );
    } // while( true )

ErrorExit:
    // Inform the cleanup function that this thread is done.
    pr_debug( "Response consumer thread exiting\n" );
    complete( &g_mwsocket_state.response_reader_done );
    return rc;
}


#ifdef ENABLE_POLLING
static int
MWSOCKET_DEBUG_ATTRIB
mwsocket_poll_handle_notifications( IN mwsocket_instance_t * SockInst )
{
    // The pseudo-socket must not be pollable, and must always
    // indicate a live INS (the pseudo-socket's non-existent INS
    // cannot die by convention.
    MYASSERT( !(SockInst->mwflags & MWSOCKET_FLAG_POLLABLE) );
    MYASSERT( SockInst->ins_alive );
    int rc = 0;

    // For tracking INSs and the pollset requests we send to them
    typedef struct _mwsocket_ins_desc
    {
        domid_t domid; // 0 ==> invalid
        mwsocket_instance_t * sockinst; // useful for debugging
        mwsocket_active_request_t * actreq;
    } mwsocket_ins_desc_t;

    mwsocket_ins_desc_t ins[ MAX_INS_COUNT ] = {0};
    int count = 0;

    mutex_lock( &g_mwsocket_state.sockinst_lock );

    // Look at all socket instances. If one's backing domid is not in
    // our active request array, add it.
    mwsocket_instance_t * curr = NULL;
    list_for_each_entry( curr, &g_mwsocket_state.sockinst_list, list_all )
    {
        if( !curr->ins_alive )
        {
            // If the socket's INS is dead, stop polling on it.
            mwsocket_set_pollable( curr, false );
            continue;
        }

        if( SockInst == curr  || // ignore the PollMonitor sockinst
                                 // the socket has been released
            (curr->mwflags & (MWSOCKET_FLAG_CLOSED | MWSOCKET_FLAG_RELEASED)) )
        {
            continue;
        }

        if( count == MAX_INS_COUNT ) { break; }

        domid_t domid = MW_SOCKET_CLIENT_ID( curr->remote_fd );
        for( int i = 0; i < MAX_INS_COUNT; ++i )
        {
            // If domid exists in array, continue
            if( ins[i].domid == domid )
            {
                // Already accounting for this domid
                break;
            }

            if( ins[i].domid == 0 )
            {
                count++;
                ins[i].domid = domid;
                ins[i].sockinst = curr;
                break;
            }
        }
    }
    mutex_unlock( &g_mwsocket_state.sockinst_lock );

    // Create and send one poll request per INS. Each request is
    // backed by the sockinst that was passed in.
    for( int i = 0; i < MAX_INS_COUNT; ++i )
    {
        if( 0 == ins[i].domid ) { continue; }

        rc = mwsocket_create_active_request( SockInst, &ins[i].actreq );
        if( rc )
        {
            MYASSERT( !"mwsocket_create_active_request" );
            goto ErrorExit;
        }

        mt_request_pollset_query_t * request =
            &ins[i].actreq->rr.request.pollset_query;

        request->base.type   = MtRequestPollsetQuery;
        request->base.size   = MT_REQUEST_POLLSET_QUERY_SIZE;
        request->base.sockfd = MW_SOCKET_CREATE( ins[i].domid, 0 );

        // Sent the request. We will wait for response.
        ins[i].actreq->deliver_response = true;
        rc = mwsocket_send_request( ins[i].actreq, true );
        MYASSERT( 0 == rc );

        if( rc ) { goto ErrorExit; }
    }

    // Wait for each response and process it
    for( int i = 0; i < MAX_INS_COUNT; ++i )
    {
        if( 0 == ins[i].domid ) { continue; }

        rc = wait_for_completion_timeout( &ins[i].actreq->arrived,
                                          MONITOR_RESPONSE_TIMEOUT );
        if( 0 == rc )
        {
            // The response did not arrive, so we cannot process
            // it. WARNING: dropping error.
            MYASSERT( !"wait_for_completion_timeout" );
            pr_warn( "Timed out while waiting for response %lx\n",
                     (unsigned long)ins[i].actreq->id );
            continue;
        }

        rc = 0;
        mt_response_pollset_query_t * response =
               &ins[i].actreq->rr.response.pollset_query;
        if( response->base.status < 0 )
        {
            // WARNING: dropping error
            pr_err( "Remote poll() failed: %d\n", response->base.status );
            continue;
        }

        if( 0 == response->count ) { continue; }

        //
        // Response is good and non-empty: notify registered poll() waiters.
        //

        pr_verbose_poll( "Processing %d FDs with IO events\n", response->count );

        // This lock protects socket instance list access and is used in
        // the poll() callback for accessing the instance itself.
        mutex_lock( &g_mwsocket_state.sockinst_lock );

        // XXXX: Must we track whether events were consumed?
        mwsocket_instance_t * currsi = NULL;
        list_for_each_entry( currsi, &g_mwsocket_state.sockinst_list, list_all )
        {
            // If this instance is referenced in the response, set its
            // events; otherwise clear them
            currsi->usersock->poll_events = 0;

            for( int i = 0; i < response->count; ++i )
            {
                // Find the associated sockinst, matching up by (remote) mwsocket

                if( currsi->remote_fd != response->items[i].sockfd ) { continue; }

                MYASSERT( currsi->usersock );

                // Transfer response's events to sockinst's, notify poll()
                // N.B. MW_POLL* == linux values
                currsi->usersock->poll_events |= response->items[i].events;
                currsi->usersock->poll_active  = currsi;

                pr_verbose_poll( "%d [%s] fd %d shows events %lx\n",
                                 currsi->proc->pid, currsi->proc->comm,
                                 currsi->local_fd, currsi->poll_events );
                MYASSERT( !(currsi->poll_events & (MW_POLLHUP | MW_POLLNVAL) ) );
                break;
            }
        }
        mutex_unlock( &g_mwsocket_state.sockinst_lock );
    } // wait: for each INS

    wake_up_interruptible( &g_mwsocket_state.waitq );

ErrorExit:
    for( int i = 0; i < MAX_INS_COUNT; ++i )
    {
        mwsocket_destroy_active_request( ins[i].actreq );
    }
    return rc;
}
#endif // ENABLE_POLLING


/**
 * @brief Thread function that monitors for local changes in pollset
 * and for remote IO events.
 */
static int
MWSOCKET_DEBUG_ATTRIB
mwsocket_monitor( void * Arg )
{
    int rc = 0;
    mwsocket_instance_t * sockinst = NULL;

    // Create a socket instance without a backing file. This socket is
    // for polling; it's not pollable itself.
    rc = mwsocket_create_sockinst( &sockinst, 0, false, false );
    if( rc ) { goto ErrorExit; }

    // Wait for the ring buffer's initialization to complete
    rc = wait_for_completion_interruptible( &g_mwsocket_state.xen_iface_ready );
    if( rc < 0 )
    {
        pr_info( "Received interrupt before ring ready\n" );
        goto ErrorExit;
    }

    // Main loop
    while( true )
    {
        if( g_mwsocket_state.pending_exit )
        {
            pr_debug( "Detecting pending exit. Leaving.\n" );
            break;
        }

        // Sleep
        mwsocket_wait( MONITOR_QUERY_INTERVAL );

        // Reap dead INS's, using this thread. TODO: track dead INSes
        // and destroy their associated sockets.
        rc = mw_xen_reap_dead_ins();
        MYASSERT( 0 == rc );
        rc = 0;

#ifdef ENABLE_POLLING
        // If there are any "real" open mwsockets at all, complete a
        // poll query exchange. The socket instance from this function
        // doesn't count, since it's only for poll monitoring.

        if( atomic_read( &g_mwsocket_state.poll_sock_count ) < 1 )
        {
            continue;
        }

        rc = mwsocket_poll_handle_notifications( sockinst );
        if( rc )
        {
            pr_err( "Error handling poll notifications: %d\n", rc );
            continue;
        }
#endif // ENABLE_POLLING

    } // while

ErrorExit:
    mwsocket_put_sockinst( sockinst );
    // Inform the cleanup function that this thread is done.
    complete( &g_mwsocket_state.monitor_done );
    return rc;
}


/**
 * @brief Processes an mwsocket creation request. Reachable via IOCTL.
 */
int
MWSOCKET_DEBUG_ATTRIB
mwsocket_create( OUT mwsocket_t * SockFd,
                 IN  int          Domain,
                 IN  int          Type,
                 IN  int          Protocol )
{
    int rc = 0;
    mwsocket_instance_t        * sockinst = NULL;
    mt_request_socket_create_t   create = {0};

    MYASSERT( SockFd );
    *SockFd = (mwsocket_t)-1;
    if( !g_mwsocket_state.is_xen_iface_ready )
    {
        MYASSERT( !"Ring has not been initialized\n" );
        rc = -ENODEV;
        goto ErrorExit;
    }

    // We're creating 2 things here:
    //  (1) a pseudo-socket structure in the local OS, and
    //  (2) the new socket on the client,

    // (1) Local tasks first
    rc = mwsocket_create_sockinst( &sockinst, 0, true, true );
    if( rc ) { goto ErrorExit; }

    // (2) Register the new socket on the client
    create.base.type     = MtRequestSocketCreate;
    create.base.size     = MT_REQUEST_SOCKET_CREATE_SIZE;
    create.sock_fam      = Domain; // family == domain
    create.sock_type     = Type;
    create.sock_protocol = Protocol;

    // Send request, wait for response
    rc = mwsocket_send_message( sockinst,
                                (mt_request_generic_t *)&create,
                                true );
    if( rc ) { goto ErrorExit; }

    // The request is via ioctl(), so we're giving the socket to the user.
    sockinst->mwflags |= MWSOCKET_FLAG_USER;

ErrorExit:
    if( 0 == rc )
    {
        MYASSERT( sockinst->local_fd >= 0 );
        *SockFd = (mwsocket_t) sockinst->local_fd;
    }

    return rc;
}


/**
 * @brief Returns whether the given file descriptor is backed by an MW socket.
 */
bool
MWSOCKET_DEBUG_ATTRIB
mwsocket_verify( const struct file * File )
{
    return (File->f_op == &mwsocket_fops);
}


static int
MWSOCKET_DEBUG_ATTRIB
mwsocket_handle_attrib( IN struct file       * File,
                        IN mwsocket_attrib_t * SetAttribs )
{
    int rc = 0;
    mwsocket_instance_t   * sockinst = NULL;
    mwsocket_active_request_t * actreq = NULL;
    mt_request_socket_attrib_t * request = NULL;
    mt_response_socket_attrib_t * response = NULL;

    rc = mwsocket_find_sockinst( &sockinst, File );
    if( rc ) { goto ErrorExit; }

    // Create a new active request
    rc = mwsocket_create_active_request( sockinst, &actreq );
    if( rc ) { goto ErrorExit; }

    // Populate the request. Do not validate request->attrib here.
    actreq->deliver_response = true; // we'll wait
    request = &actreq->rr.request.socket_attrib;
    request->base.type = MtRequestSocketAttrib;
    request->base.size = MT_REQUEST_SOCKET_ATTRIB_SIZE;

    request->modify = SetAttribs->modify;
    request->name   = SetAttribs->name;
    request->val    = SetAttribs->val;

    rc = mwsocket_send_request( actreq, true );
    if( rc ) { goto ErrorExit; }

    // Wait
    rc = wait_for_completion_timeout( &actreq->arrived, GENERAL_RESPONSE_TIMEOUT );
    if( 0 == rc )
    {
        rc = -ETIME;
        goto ErrorExit;
    }
    rc = 0;
    
    response = &actreq->rr.response.socket_attrib;
    if( response->base.status < 0 )
    {
        rc = response->base.status;
        pr_err( "Operation failed on remote side: %d\n", rc );
        goto ErrorExit;
    }

    if( SetAttribs->modify )
    {
        SetAttribs->val = response->val;
    }

ErrorExit:
    mwsocket_destroy_active_request( actreq );
    return rc;
}


static ssize_t
MWSOCKET_DEBUG_ATTRIB
mwsocket_read( struct file * File,
               char        * Bytes,
               size_t        Len,
               loff_t      * Offset )
{
    ssize_t rc = 0;
    mwsocket_active_request_t   * actreq = NULL;
    mt_response_generic_t     * response = NULL;
    mwsocket_instance_t       * sockinst = NULL;

    pr_debug( "Processing read()\n" );

    rc = mwsocket_find_sockinst( &sockinst, File );
    if( rc ) { goto ErrorExit; }
    MYASSERT( sockinst->mwflags & MWSOCKET_FLAG_USER );

    if( !sockinst->read_expected )
    {
        MYASSERT( !"Calling read() but fire-and-forget was indicated in write()" );
        rc = -EINVAL;
        goto ErrorExit;
    }

    // The read was attempted
    sockinst->read_expected = false;

    // Now find the outstanding request/response
    rc = mwsocket_find_active_request_by_id( &actreq, sockinst->blockid );
    if( rc )
    {
        goto ErrorExit;
    }

    if( !actreq->deliver_response )
    {
        rc = -EINVAL;
        goto ErrorExit;
    }

    if( sockinst->user_outstanding_accept )
    {
        // For accept(), swap out the known active request for the
        // inbound one that arrived first.
        mwsocket_active_request_t * new = NULL;
        rc = mwsocket_await_inbound_connection( actreq, &new );
        if( rc ) { goto ErrorExit; }

        // We're consuming the returning accept() here
        sockinst->user_outstanding_accept = false;

        // Replace the active request with its inbound surrogate
        actreq = new;
        sockinst = actreq->sockinst;

        // Although there may still be an outstanding accept() on the
        // user socket, it should expect a write next since we're
        // returning against a read. Change its state underneath it.
        sockinst->usersock->read_expected = false;
    }
    else if( wait_for_completion_interruptible( &actreq->arrived ) )
    {
        // Keep the request alive and stage for another read attempt.
        pr_warn( "read() was interrupted\n" );
        sockinst->read_expected = true;
        rc = -EINTR;
        goto ErrorExit;
    }

    // Data is ready for us. Validate and copy to user.
    response = (mt_response_generic_t *) &actreq->rr.response;
    if( Len < response->base.size )
    {
        MYASSERT( !"User buffer is too small for response" );
        rc = -EINVAL;
        goto ErrorExit;
    }

    // If this is from accept(), install a new file descriptor in the
    // calling process and report it to the user via the response.
    rc = mwsocket_postproc_in_task( actreq, response );
    if( rc ) { goto ErrorExit; }

    // Is there a pending error on this socket? If so, return it.
    rc = mwsocket_pending_error( sockinst, response->base.type & MT_TYPE_MASK );
    if( rc )
    {
        pr_debug( "Delivering error: %d\n", (int)rc );
        goto ErrorExit;
    }

    rc = copy_to_user( Bytes, (void *)response, response->base.size );
    if( rc )
    {
        MYASSERT( !"copy_to_user() failed" );
        rc = -EFAULT;
        goto ErrorExit;
    }
    
    // Success
    rc = response->base.size;

ErrorExit:
    if( -EINTR != rc ) // XXXX: hacky -- could leak
    {
        // The "active" request is now dead.
        mwsocket_destroy_active_request( actreq );
    }

    return rc;
}


static ssize_t
MWSOCKET_DEBUG_ATTRIB
mwsocket_write( struct file * File,
                const char  * Bytes,
                size_t        Len,
                loff_t      * Offset )
{
    ssize_t rc = 0;
    mt_request_generic_t * request = NULL;
    mwsocket_active_request_t * actreq = NULL;
    mwsocket_instance_t * sockinst = NULL;
    mt_request_base_t base;
    bool sent = false;
    // Do not expect a read() after this if we return -EAGAIN

    if( Len < MT_REQUEST_BASE_SIZE )
    {
        MYASSERT( !"User provided too few bytes." );
        rc = -EINVAL;
        goto ErrorExit;
    }

    // Peek to discover the base
    rc = copy_from_user( &base, Bytes, sizeof(base) );
    if( rc )
    {
        MYASSERT( !"copy_from_user failed." );
        rc = -EFAULT;
        goto ErrorExit;
    }

    rc = mwsocket_find_sockinst( &sockinst, File );
    if( rc ) { goto ErrorExit; }

    MYASSERT( MWSOCKET_FLAG_USER & sockinst->mwflags );

    if( sockinst->read_expected )
    {
        MYASSERT( !"write() called but read() expected" );
        rc = -EIO;
        goto ErrorExit;
    }

    if( MtRequestSocketAccept == base.type )
    {
        // Distribute accept() to all INSs. This is generating a new
        // outstanding accept().
        sockinst->user_outstanding_accept = true;

        mwsocket_sock_replicate_args_t args =
            { .sockinst = sockinst,
              .reqbase = (mt_request_generic_t *) &base };
        rc = mw_xen_for_each_live_ins( mwsocket_ins_sock_replicator, &args );
        MYASSERT( 0 == rc );
        sockinst->read_expected = true;
        goto ErrorExit;
    }

    // Otherwise... Create a new active request
    rc = mwsocket_create_active_request( sockinst, &actreq );
    if( rc ) { goto ErrorExit; }

    actreq->from_user = true;

    // Populate the request: read the entire thing from user
    // memory. We could read just the base here and later copy the
    // body from user memory into the ring buffer, but the extra calls
    // are not worth it performance-wise.
    request = &actreq->rr.request;

    rc = copy_from_user( request, Bytes, Len );
    if( rc )
    {
        MYASSERT( !"copy_from_user failed." );
        rc = -EFAULT;
        goto ErrorExit;
    }

    if( request->base.size > Len )
    {
        MYASSERT( !"Request is longer than provided buffer." );
        rc = -EINVAL;
        goto ErrorExit;
    }

    // Is there a pending error on this socket? If so, deliver it.
    rc = mwsocket_pending_error( sockinst, request->base.type );
    if( rc )
    {
        pr_debug( "Delivering error: %d\n", (int)rc );
        goto ErrorExit;
    }

    // Write to the ring. If the ring is full, we will wait here on
    // behalf of the user. This is noticably faster (~100ms/MB) than
    // having the caller wait in usermode and try again later.
    rc = mwsocket_send_request( actreq, true );
    if( rc ) { goto ErrorExit; }

    sent = true;

ErrorExit:

    // We're returning an error - don't expect a read
    if( rc )
    {
        sockinst->read_expected = false;
    }

    if( rc && !sent )
    {
        mwsocket_destroy_active_request( actreq );
    }

    return rc;
}


/**
 * @brief IOCTL callback for ioctls against mwsocket files themselves
 * (vs. the mwcomms device)
 */
static long
MWSOCKET_DEBUG_ATTRIB
mwsocket_ioctl( struct file * File,
                unsigned int   Cmd,
                unsigned long  Arg )
{
    int rc = 0;
    mwsocket_attrib_t attrib;
    mwsocket_attrib_t * uattrib = (mwsocket_attrib_t *)Arg;
    mwsocket_instance_t * sockinst = NULL;

    rc = mwsocket_find_sockinst( &sockinst, File );
    if( rc ) { goto ErrorExit; }

    switch( Cmd )
    {
    case MW_IOCTL_SOCKET_ATTRIBUTES:
        rc = copy_from_user( &attrib, (void *)Arg, sizeof(attrib) );
        if( rc )
        {
            MYASSERT( !"Invalid memory provided\n" );
            rc = -EFAULT;
            goto ErrorExit;
        }

        rc = mwsocket_handle_attrib( File, &attrib );
        if( rc ) { goto ErrorExit; }

        if( attrib.modify )
        {
            rc = copy_to_user( &uattrib->val,
                               &attrib.val,
                               sizeof(attrib.val) );
            if( rc )
            {
                MYASSERT( !"Invalid memory provided\n" );
                rc = -EFAULT;
                goto ErrorExit;
            }
        }
        break;
    default:
        pr_err( "Called with invalid IOCTL %x\n", Cmd );
        rc = -EINVAL;
        goto ErrorExit;
    }

ErrorExit:
    return rc;
}


static unsigned int
MWSOCKET_DEBUG_ATTRIB
mwsocket_poll( struct file * File,
               struct poll_table_struct * PollTbl )
{
    ssize_t rc = 0;
    mwsocket_instance_t * sockinst = NULL;
    unsigned long events = 0;

    rc = mwsocket_find_sockinst( &sockinst, File );
    MYASSERT( 0 == rc );
    pr_verbose( "Processing poll(), fd %d\n", sockinst->local_fd );

    poll_wait( File, &g_mwsocket_state.waitq, PollTbl );

    // Lock used by poll monitor during socket instance update
    mutex_lock( &g_mwsocket_state.sockinst_lock );

    events = sockinst->poll_events;
    sockinst->poll_events = 0;

    mutex_unlock( &g_mwsocket_state.sockinst_lock );

    if( events )
    {
        pr_debug( "Returning events %lx, fd %d\n", events, sockinst->local_fd );
    }

    return events;
}


static int
MWSOCKET_DEBUG_ATTRIB
mwsocket_release( struct inode * Inode,
                  struct file  * File )
{
    int rc = 0;
    mwsocket_instance_t * sockinst = NULL;

    // Do not incur a decrement against our module for this close(),
    // since the file we're closing was not opened by an open()
    // callback.
    __module_get( THIS_MODULE );

    MYASSERT( File );
    
    rc = mwsocket_find_sockinst( &sockinst, File );
    if( rc ) { goto ErrorExit; }

    MYASSERT( sockinst->mwflags & MWSOCKET_FLAG_USER );

    sockinst->mwflags |= MWSOCKET_FLAG_RELEASED;

    pr_debug( "Processing release() on fd=%d remote=%llx\n",
              sockinst->local_fd, (unsigned long long) sockinst->remote_fd );

    // Close the remote socket only if it exists. It won't exist in
    // the case where accept() was called but hasn't returned yet. In
    // that case, we've created a local sockinst, but the remote
    // socket does not yet exist.

    // First, close sibling listeners. They shouldn't be backed by a file.
    mwsocket_instance_t * curr = NULL;
    mwsocket_instance_t * next = NULL;
    list_for_each_entry_safe( curr, next,
                              &sockinst->sibling_listener_list,
                              sibling_listener_list )
    {
        if( curr->mwflags & MWSOCKET_FLAG_USER ) { continue; }

        // This is a sibling listener
        MYASSERT( NULL == curr->file );

        rc = mwsocket_close_remote( curr, true );
        MYASSERT( 0 == rc );
    }

    // Now close the user socket
    if( MW_SOCKET_IS_FD( sockinst->remote_fd ) )
    {
        rc = mwsocket_close_remote( sockinst, true );
        // fall-through
    }

    // XXXX: The socket must be destroyed. We force its refct down to
    // 1 here. Cases where it isn't 1 should be investigated; one such
    // case is on accept().

    // MYASSERT( 1 == atomic_read( &sockinst->refct ) );
    atomic_set( &sockinst->refct, 1 );

    mwsocket_put_sockinst( sockinst );

ErrorExit:
    return rc;
}


/******************************************************************************
 * Module-level init and fini function
 ******************************************************************************/
int
MWSOCKET_DEBUG_ATTRIB
mwsocket_init( IN struct sockaddr * LocalIp )
{
    // Do everything we can, then wait for the ring to be ready
    int rc = 0;

    bzero( &g_mwsocket_state, sizeof(g_mwsocket_state) );

    g_mwsocket_state.local_ip = LocalIp;

    mutex_init( &g_mwsocket_state.request_lock );
    mutex_init( &g_mwsocket_state.active_request_lock );
    mutex_init( &g_mwsocket_state.sockinst_lock );

    sema_init( &g_mwsocket_state.event_channel_sem, 0 );
    init_completion( &g_mwsocket_state.response_reader_done );
    init_completion( &g_mwsocket_state.monitor_done );
    init_completion( &g_mwsocket_state.xen_iface_ready );

    INIT_LIST_HEAD( &g_mwsocket_state.active_request_list );
    INIT_LIST_HEAD( &g_mwsocket_state.sockinst_list );

    init_waitqueue_head( &g_mwsocket_state.waitq );

    gfn_new_inode_pseudo = (pfn_new_inode_pseudo_t *)
        kallsyms_lookup_name( "new_inode_pseudo" );
    gfn_dynamic_dname = (pfn_dynamic_dname_t *)
        kallsyms_lookup_name( "dynamic_dname" );

    if( NULL == gfn_new_inode_pseudo
        || NULL == gfn_dynamic_dname )
    {
        MYASSERT( !"Couldn't find required kernel function\n" );
        rc = -ENXIO;
        goto ErrorExit;
    }

    rc = mwsocket_fs_init();
    if( rc ) { goto ErrorExit; }

    g_mwsocket_state.active_request_cache =
        kmem_cache_create( MWSOCKET_ACTIVE_REQUEST_CACHE,
                           sizeof( mwsocket_active_request_t ),
                           0, 0, NULL );

    if( NULL == g_mwsocket_state.active_request_cache )
    {
        MYASSERT( !"kmem_cache_create() failed\n" );
        rc = -ENOMEM;
        goto ErrorExit;
    }

    g_mwsocket_state.sockinst_cache =
        kmem_cache_create( MWSOCKET_SOCKET_INSTANCE_CACHE,
                           sizeof( mwsocket_instance_t ),
                           0, 0, NULL );
    if( NULL == g_mwsocket_state.sockinst_cache )
    {
        MYASSERT( !"kmem_cache_create() failed\n" );
        rc = -ENOMEM;
        goto ErrorExit;
    }

    // Response consumer
    g_mwsocket_state.response_reader_thread =
        kthread_run( &mwsocket_response_consumer,
                     NULL,
                     MWSOCKET_MESSAGE_CONSUMER_THREAD );
    if( NULL == g_mwsocket_state.response_reader_thread )
    {
        MYASSERT( !"kthread_run() failed\n" );
        rc = -ESRCH;
        goto ErrorExit;
    }

    // Thread that monitors poll events and reaps dead INSs
    g_mwsocket_state.monitor_thread =
        kthread_run( &mwsocket_monitor,
                     NULL,
                     MWSOCKET_MONITOR_THREAD );
    if( NULL == g_mwsocket_state.monitor_thread )
    {
        MYASSERT( !"kthread_run() failed\n" );
        rc = -ESRCH;
        goto ErrorExit;
    }

ErrorExit:
    return rc;
}


/**
 * @brief Replicate the given exemplar socket onto the given INS.
 *
 * Can be used when the socket on the given INS:
 *
 * #. Has not yet been created on the INS
 *
 * #. Has been created and possibly bind() and/or listen() have been invoked on it.
 *
 * #. Has been used for an accept(), but is not currently in accept() state.
 *
 * This function is useful for two main situations:
 *
 * #. A new INS has appeared and currently-listening sockets need to
 *    be replicated onto it.
 *
 * #. An existing "user" socket is being used in an accept() call. In
 *    this case, we replicate that socket across all known INSs.
 *
 * See the functions mwsocket_propogate_listeners() and
 * mwsocket_write() to understand this function's use cases. It is
 * intended to be called through mw_xen_for_each_live_ins() although
 * that isn't a requirement.
 */
static int
MWSOCKET_DEBUG_ATTRIB
mwsocket_ins_sock_replicator( IN domid_t Domid,
                              IN void *  Arg )
{
    MYASSERT( Arg );

    int rc = 0;
    mwsocket_sock_replicate_args_t * args = (mwsocket_sock_replicate_args_t *) Arg;
    mwsocket_instance_t * usersock = (mwsocket_instance_t *) args->sockinst;
    mwsocket_instance_t * newsock = NULL;
    bool found = false;

    MYASSERT( usersock );

    mutex_lock( &usersock->inbound_lock );

    if( !(usersock->mwflags & MWSOCKET_FLAG_USER) )
    {
        MYASSERT( !"Not a user socket. I shouldn't have been invoked." );
        rc = -EINVAL;
        goto ErrorExit;
    }

    // N.B. This function is called for every user socket and every
    // INS. It has to figure out whether the user socket or one of its
    // siblings is bound/accepting on the given INS. Each user socket
    // is associated with a unique socket type/bind address.

    // First, see if the user socket itself is the reason we were
    // called.
    if( MW_SOCKET_CLIENT_ID( usersock->remote_fd ) == Domid )
    {
        newsock = usersock;
        found = true;
    }
    else
    {
        // Look for a sibling listener that's associated with this
        // INS.
        list_for_each_entry( newsock, &usersock->sibling_listener_list,
                             sibling_listener_list )
        {
            if( MW_SOCKET_CLIENT_ID( newsock->remote_fd ) == Domid )
            {
                found = true;
                break;
            }
        }
    }

    // There is no sockinst for this INS. Create one that's
    // pollable but has no backing file object.
    if( !found )
    {
        rc = mwsocket_create_sockinst( &newsock, 0, true, false );
        if( rc ) { goto ErrorExit; }

        mt_request_socket_create_t create =
            { .base.type     = MtRequestSocketCreate,
              .base.size     = MT_REQUEST_SOCKET_CREATE_SIZE,
              .base.sockfd   = MW_SOCKET_CREATE( Domid, MT_INVALID_SOCKET_FD ),
              .sock_fam      = usersock->sock_fam, // family == domain
              .sock_type     = usersock->sock_type,
              .sock_protocol = usersock->sock_protocol };

        // Send request, wait for response
        rc = mwsocket_send_message( newsock,
                                    (mt_request_generic_t *)&create,
                                    true );
        if( rc )
        {
            pr_err( "Socket replication: creation failed, domid=%d\n", Domid );
            MYASSERT( !"mwsocket_send_message() / create" );
            goto ErrorExit;
        }

        // Remote creation succeeded; add to user's sibling list
        list_add( &newsock->sibling_listener_list,
                  &usersock->sibling_listener_list );

        // Map newsock to its primary socket for polling support
        newsock->usersock = usersock;
    }

    MYASSERT( NULL != newsock );

    // Either we found it, or we created it. Now bind, waiting for completion.
    if( (usersock->mwflags & MWSOCKET_FLAG_BOUND) &&
        !(newsock->mwflags & MWSOCKET_FLAG_BOUND) )
    {
        mt_request_socket_bind_t bind =
            { .base.type      = MtRequestSocketBind,
              .base.size      = MT_REQUEST_SOCKET_BIND_SIZE,
              .base.sockfd    = newsock->remote_fd,
              .sockaddr       = usersock->bind_sockaddr };
        rc = mwsocket_send_message( newsock,
                                    (mt_request_generic_t *)&bind,
                                    true );
        if( rc )
        {
            MYASSERT( !"mwsocket_send_message() / bind" );
            pr_err( "Socket replication: bind failed, domid=%d\n", Domid );
            goto ErrorExit;
        }
    }

    // Listen, wait
    if( (usersock->mwflags & MWSOCKET_FLAG_LISTENING) &&
        !(newsock->mwflags & MWSOCKET_FLAG_LISTENING) )
    {
        mt_request_socket_listen_t listen =
            { .base.type    = MtRequestSocketListen,
              .base.size    = MT_REQUEST_SOCKET_LISTEN_SIZE,
              .base.sockfd  = newsock->remote_fd,
              .backlog      = usersock->backlog };
        rc = mwsocket_send_message( newsock,
                                    (mt_request_generic_t *) &listen,
                                    true );
        if( rc )
        {
            pr_err( "Socket replication: listen failed, domid=%d\n", Domid );
            MYASSERT( !"mwsocket_send_message() / listen" );
            goto ErrorExit;
        }
    }

    // Accept: send request but don't wait for response
    if( !(newsock->mwflags & MWSOCKET_FLAG_ACCEPT) )
    {
        mt_request_socket_accept_t accept =
            { .base.type    = MtRequestSocketAccept,
              .base.size    = MT_REQUEST_SOCKET_ACCEPT_SIZE,
              .base.sockfd  = newsock->remote_fd,
              .flags        = usersock->accept_flags };
        mwsocket_active_request_t * actreq = NULL;

        rc = mwsocket_create_active_request( newsock, &actreq );
        if( rc ) { goto ErrorExit; }

        // If the originator wants to await the response then set the
        // flag accordingly.
        if( newsock == usersock &&
            NULL != args->reqbase &&
            MT_REQUEST_CALLER_WAITS( args->reqbase ) )
        {
            MT_REQUEST_SET_CALLER_WAITS( &accept );
        }

        // Unconditionally deliver the response for correct processing
        // in the response consumer (??)
        actreq->deliver_response = true;
        memcpy( &actreq->rr.request, &accept, accept.base.size );

        rc = mwsocket_send_request( actreq, true );
        if( rc )
        {
            pr_err( "Socket replication: accept send failed, domid=%d\n",
                    Domid );
            MYASSERT( !"mwsocket_send_message() / accept" );
            goto ErrorExit;
        }
    }


ErrorExit:
    if( rc )
    {
        mwsocket_put_sockinst( newsock );
        MYASSERT( !"Something went wrong" );
    }

    mutex_unlock( &usersock->inbound_lock );

    return rc;
} // mwsocket_ins_sock_replicator


/**
 * @brief Replicates all listening sockets onto all live INSs. Called via work item.
 */
static int
mwsocket_propogate_listeners( struct work_struct * Work )
{
    int rc = 0;

    // We need to iterate over socket list, and we may need to add new
    // sockets. However, we cannot lock sockinst_lock here because
    // we're adding to the list too. Since we're adding only at the
    // head, we'll be OK.

    // mutex_lock( &g_mwsocket_state.sockinst_lock );
    mwsocket_instance_t * curr = NULL;
    list_for_each_entry( curr, &g_mwsocket_state.sockinst_list, list_all )
    {
        // Socket must be primary AND ready (bound and listening, at least)
        if( !(MWSOCKET_FLAG_USER      & curr->mwflags) ||
            !(MWSOCKET_FLAG_BOUND     & curr->mwflags) ||
            !(MWSOCKET_FLAG_LISTENING & curr->mwflags)  )
        {
            continue;
        }

        mwsocket_sock_replicate_args_t args =
            { .sockinst = curr,
              .reqbase = NULL };
        rc = mw_xen_for_each_live_ins( mwsocket_ins_sock_replicator, &args );
        if( rc )
        {
            pr_err( "An INS listener propogation failed, rc = %d. Continuing.\n", rc );
        }
    }

    // mutex_unlock( &g_mwsocket_state.sockinst_lock );

    CHECK_FREE( Work );
    return rc;
}


/**
 * @brief Handles introduction of a new INS; can be called by XenWatch thread.
 *
 * Propogates existing listeners via work item -- doesn't block the
 * calling thread.
 */
static int
//MWSOCKET_DEBUG_ATTRIB: incompatible with workq API
mwsocket_new_ins( domid_t Domid )
{
    int rc = 0;

    // Replicate the lister(s), but not in the current thread (xenwatch)
    // since that's responsible for foundational Xen interactions
    // (e.g. event channel).
    struct work_struct * work = NULL;
    work = (struct work_struct *) kmalloc( sizeof( *work ), GFP_KERNEL);
    if( NULL == work )
    {
        rc = -ENOMEM;
        MYASSERT( !"kmalloc" );
        goto ErrorExit;
    }

    // The allocation succeeded, and nothing below can fail. So
    // the target function will release the allocation.
    INIT_WORK( work, (work_func_t) mwsocket_propogate_listeners );
    schedule_work( work );

ErrorExit:
    return rc;
}


void
MWSOCKET_DEBUG_ATTRIB
mwsocket_fini( void )
{
    mwsocket_active_request_t * currar = NULL;
    mwsocket_active_request_t * nextar = NULL;
    
    mwsocket_instance_t * currsi = NULL;
    mwsocket_instance_t * nextsi = NULL;

    // Destroy response consumer -- kick it. It could be waiting for
    // the ring to become ready, or for responses
    // to arrive on the ring. Wait for it to complete so shared
    // resources can be safely destroyed below.
    g_mwsocket_state.pending_exit = true;
    complete_all( &g_mwsocket_state.xen_iface_ready );

    up( &g_mwsocket_state.event_channel_sem );
    
    if( NULL != g_mwsocket_state.response_reader_thread )
    {
        wait_for_completion( &g_mwsocket_state.response_reader_done );
    }

    // Similarly, destroy the poll notification thread. It regularly
    // checks pending_exit, so kicking isn't necessary.
    if( NULL != g_mwsocket_state.monitor_thread )
    {
        wait_for_completion( &g_mwsocket_state.monitor_done );
    }

    // Active requests cleanup
    mutex_lock( &g_mwsocket_state.active_request_lock );
    list_for_each_entry_safe( currar, nextar,
                              &g_mwsocket_state.active_request_list, list_all )
    {
        pr_err( "Harvesting leaked active request id=%lx type=%x\n",
                (unsigned long)currar->id, currar->rr.request.base.type );
        list_del( &currar->list_all );
        kmem_cache_free( g_mwsocket_state.active_request_cache, currar );
    }
    mutex_unlock( &g_mwsocket_state.active_request_lock );

    if( NULL != g_mwsocket_state.active_request_cache )
    {
        kmem_cache_destroy( g_mwsocket_state.active_request_cache );
    }

    // Socket instances cleanup
    mutex_lock( &g_mwsocket_state.sockinst_lock );
    list_for_each_entry_safe( currsi, nextsi,
                              &g_mwsocket_state.sockinst_list, list_all )
    {
        pr_err( "Harvesting leaked socket instance for FD %d\n",
                currsi->local_fd );

        list_del( &currsi->list_all );
        kmem_cache_free( g_mwsocket_state.sockinst_cache, currsi );
    }
    mutex_unlock( &g_mwsocket_state.sockinst_lock );

    if( NULL != g_mwsocket_state.sockinst_cache )
    {
        kmem_cache_destroy( g_mwsocket_state.sockinst_cache );
    }

    // Pseudo-filesystem cleanup
    if( NULL != g_mwsocket_state.fs_mount )
    {
        kern_unmount( g_mwsocket_state.fs_mount );
    }

    if( g_mwsocket_state.fs_registered )
    {
        unregister_filesystem( &mwsocket_fs_type );
    }
}
