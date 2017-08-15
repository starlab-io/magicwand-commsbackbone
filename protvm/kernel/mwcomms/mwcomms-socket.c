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

#include <linux/fs.h>
#include <linux/mount.h>

#include <linux/file.h>
#include <linux/poll.h>
#include <linux/delay.h>

#include <linux/kallsyms.h>

#include <linux/kthread.h>

#include <asm/atomic.h>
#include <asm/cmpxchg.h>

#include <message_types.h>
#include <xen_keystore_defs.h>

#include "mwcomms-backchannel.h"


// Magic for the mwsocket filesystem
#define MWSOCKET_FS_MAGIC  0x4d77536f // MwSo


//
// Timeouts for responses
//

// General-purpose response timeout for user-initiated requests. If
// debug output is enabled on the INS, this needs to be larger.
#define GENERAL_RESPONSE_TIMEOUT      ( HZ * 2)

// Response timeout for kernel-initiated poll requests
#define POLL_MONITOR_RESPONSE_TIMEOUT ( HZ * 2 )

//
// How frequently should the poll monitor thread send a request?
//

//#define POLL_MONITOR_QUERY_INTERVAL   ( HZ * 2 ) // >= 1 sec

//#define POLL_MONITOR_QUERY_INTERVAL   ( HZ >> 2 ) // 4x/sec
//#define POLL_MONITOR_QUERY_INTERVAL   ( HZ >> 3 ) // 8x/sec
//#define POLL_MONITOR_QUERY_INTERVAL   ( HZ >> 4 ) // 16x/sec
#define POLL_MONITOR_QUERY_INTERVAL   ( HZ >> 5 ) // 32x/sec
//#define POLL_MONITOR_QUERY_INTERVAL   ( HZ >> 6 ) // 64x/sec

#if (!PVM_USES_EVENT_CHANNEL)
#  define RING_BUFFER_POLL_INTERVAL (HZ >> 6) // 64x / sec
#endif

// How long to wait if we want to write a request but the ring is full
#define RING_FULL_TIMEOUT (HZ >> 6)

// Poll-related messages are annoying, usually....

#define POLL_DEBUG_MESSAGES 0

#if POLL_DEBUG_MESSAGES
#  define pr_verbose_poll(...) pr_debug(__VA_ARGS)
#else
#  define pr_verbose_poll(...) ((void)0)
#endif


// Even with verbose debugging, don't show these request/response types
#define DEBUG_SHOW_TYPE( _t )                         \
    ( ((_t) & MT_TYPE_MASK) != MtRequestPollsetQuery )

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

    // File descriptor info: one for PVM, one for INS
    int                  local_fd; 
    mw_socket_fd_t       remote_fd;

    // poll() support: the latest events on this socket
    unsigned long       poll_events;

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

    // How many active requests are using this mwsocket? N.B. due to
    // the way release() is implemented on the INS, it is not valid to
    // destroy an mwsocket instance immediately upon release. Destroy
    // instead when this count reaches 0. We don't rely on the file's
    // refct because we might have to close while there are
    // outstanding (and blocking) requests.
    atomic_t            refct;

    // The ID of the blocking request. Only one at a time!
    mt_id_t             blockid;

    // did user indicate a read() is next?
    bool                read_expected; 
    // have we started remote close?
    bool                remote_close_requested; 
    // did the remote side close unexpectedly?
    bool                remote_surprise_close; 

    // Do not allow close() while certain operations are
    // in-flight. Implement this behavior with a read-write lock,
    // wherein close takes a write lock and all the other operations a
    // read lock. Don't block the close indefinitely -- the INS might
    // have died.
    struct rw_semaphore close_lock;

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

    // Indicates response(s) available
    struct semaphore event_channel_sem;

    // Filesystem data
    struct vfsmount * fs_mount;
    bool              fs_registered;

    // Kernel thread info: response consumer
    struct task_struct * response_reader_thread;
    struct completion    response_reader_done;

    // Kernel thread info: poll notifier
    struct task_struct * poll_monitor_thread;
    struct completion    poll_monitor_done;

    // Support for poll(): to inform waiters of data
    wait_queue_head_t   waitq;
    
    bool   pending_exit;
} mwsocket_globals_t;

mwsocket_globals_t g_mwsocket_state;

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
mwsocket_create_active_request( IN mwsocket_instance_t * SockInst,
                                OUT mwsocket_active_request_t ** ActReq );

static int
mwsocket_send_request( IN mwsocket_active_request_t * ActiveReq,
                       IN bool                        WaitForRing );

static int
mwsocket_send_message( IN mwsocket_instance_t  * SockInst,
                       IN mt_request_generic_t * Request,
                       IN bool                   AwaitResponse );

/******************************************************************************
 * Primitive Functions
 ******************************************************************************/

/**
 * @brief Gets the next unused, valid ID for use in requests.
 *
 * Caller must hold the global active_request_lock.
 */
static mt_id_t
mwsocket_get_next_id( void )
{
    static atomic64_t counter = ATOMIC64_INIT( 0 );
    mt_id_t id = MT_ID_UNSET_VALUE;

    do
    {
        // Get the next candidate ID
        id = (mt_id_t) atomic64_inc_return( &counter );

        if ( MT_ID_UNSET_VALUE == id )
        {
            id = (mt_id_t) atomic64_inc_return( &counter );
        }

        // Is this ID currently in use? The caller already holds the lock for us.
        bool found = false;
        mwsocket_active_request_t * curr = NULL;

        list_for_each_entry( curr, &g_mwsocket_state.active_request_list, list_all )
        {
            if ( curr->id == id )
            {
                pr_debug( "Not using ID %lx because it is currently in use\n",
                          (unsigned long) id );
                found = true;
                break;
            }
        }

        if ( !found )
        {
            break;
        }
    } while( true );

    return id;
}


void
mwsocket_notify_ring_ready( void )
{
    // Triggered when new Ins is added.
    // only call if this is the first Ins that has been added
    if( false == g_mwsocket_state.is_xen_iface_ready )
    {
        g_mwsocket_state.is_xen_iface_ready = true;
        complete_all( &g_mwsocket_state.xen_iface_ready );
    }
}

static void
mwsocket_wait( long TimeoutJiffies )
{
    set_current_state( TASK_INTERRUPTIBLE );
    schedule_timeout( TimeoutJiffies );
}


void
mwsocket_event_cb ( void )
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
                         "mwsocket: ",
                         &mwsocket_fs_ops,
                         &mwsocket_fs_dentry_ops,
                         MWSOCKET_FS_MAGIC );
}


static struct file_system_type mwsocket_fs_type =
{
    .name    = "mwsocketfs",
    .mount   = fs_mount,
    .kill_sb = kill_anon_super,
};


int
mwsocket_fs_init( void )
{
    int rc = 0;

    pr_debug( "Initializing mwsocket pseudo-filesystem\n" );
    
    rc = register_filesystem( &mwsocket_fs_type );
    if ( rc )
    {
        pr_err( "register_filesystem failed: %d\n", rc );
        goto ErrorExit;
    }

    g_mwsocket_state.fs_registered = true;

    g_mwsocket_state.fs_mount = kern_mount( &mwsocket_fs_type );

    if ( IS_ERR(g_mwsocket_state.fs_mount) )
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

static int
mwsocket_find_sockinst( OUT mwsocket_instance_t ** SockInst,
                        IN  struct file          * File )
{
    int rc = -ENOENT;
    mwsocket_instance_t * curr = NULL;

    MYASSERT( SockInst );
    MYASSERT( File );

    *SockInst = NULL;

    mutex_lock( &g_mwsocket_state.sockinst_lock );

    list_for_each_entry( curr, &g_mwsocket_state.sockinst_list, list_all )
    {
        if ( curr->file  == File )
        {
            *SockInst = curr;
            rc = 0;
            break;
        }
    }

    mutex_unlock( &g_mwsocket_state.sockinst_lock );

    return rc;
}


void
mwsocket_debug_dump_sockinst( void )
{
   mwsocket_instance_t * curr = NULL;
   list_for_each_entry( curr, &g_mwsocket_state.sockinst_list, list_all )
   {
       pr_info( "%p refct %d file %p proc %d[%s]\n",
                curr, atomic_read( &curr->refct ), curr->file,
                curr->proc->pid, curr->proc->comm );
   }
}


// @brief Reference the socket instance
static void
mwsocket_get_sockinst(  mwsocket_instance_t * SockInst )
{
    int val = 0;
    
    MYASSERT( SockInst );

    val = atomic_inc_return( &SockInst->refct );

    pr_debug( "Referenced socket instance %p fd=%d, refct=%d\n",
              SockInst, SockInst->local_fd, val );

    // XXXX should this check be performed somewhere else?
    // removed for multiple INS change
    // What's the maximum refct that should be against a sockinst?
    // Every slot in the ring buffer could be in use by an active
    // request against it, plus there could be one active request being
    // delivered to the user, plus another that has been consumed off
    // the ring but not destroyed yet (see mwsocket_response_consumer).
    //MYASSERT( val <= RING_SIZE( &g_mwsocket_state.front_ring ) + 2 );
}



// @brief Dereferences the socket instance, destroying upon 0 reference count
static void
MWSOCKET_DEBUG_ATTRIB
mwsocket_put_sockinst( mwsocket_instance_t * SockInst )
{
    int val = 0;
    
    if ( NULL == SockInst )
    {
        goto ErrorExit;
    }

    val = atomic_dec_return( &SockInst->refct );
    if ( val )
    {
        pr_debug( "Dereferenced socket instance %p fd=%d, refct=%d\n",
                  SockInst, SockInst->local_fd, val );
        goto ErrorExit;
    }
    
    // Remove from global sockinst_list
    mutex_lock( &g_mwsocket_state.sockinst_lock );
    atomic_dec( &g_mwsocket_state.sockinst_count );
    list_del( &SockInst->list_all );
    mutex_unlock( &g_mwsocket_state.sockinst_lock );

    pr_debug( "Destroyed socket instance %p fd=%d\n",
              SockInst, SockInst->local_fd );

    bzero( SockInst, sizeof(*SockInst) );
    kmem_cache_free( g_mwsocket_state.sockinst_cache, SockInst );

ErrorExit:
    return;
}


static int
mwsocket_create_sockinst( OUT mwsocket_instance_t ** SockInst,
                          IN  int                    Flags,
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
        kmem_cache_alloc( g_mwsocket_state.sockinst_cache,
                          GFP_KERNEL | __GFP_ZERO );
    if ( NULL == sockinst )
    {
        MYASSERT( !"kmem_cache_alloc failed\n" );
        rc = -ENOMEM;
        goto ErrorExit;
    }

    // Must be in the list whether or not we're creating the backing file
    mutex_lock( &g_mwsocket_state.sockinst_lock );
    list_add( &sockinst->list_all, &g_mwsocket_state.sockinst_list );
    atomic_inc( &g_mwsocket_state.sockinst_count );
    mutex_unlock( &g_mwsocket_state.sockinst_lock );

    *SockInst = sockinst;
    sockinst->local_fd = -1;
    sockinst->remote_fd = MT_INVALID_SOCKET_FD;

    init_rwsem( &sockinst->close_lock );
    
    sockinst->proc = current;
    // refct starts at 1; an extra put() is done upon close()
    atomic_set( &sockinst->refct, 1 );

    if ( !CreateBackingFile )
    {
        goto ErrorExit;
    }
    
    inode = gfn_new_inode_pseudo( g_mwsocket_state.fs_mount->mnt_sb );
    if ( NULL == inode )
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
    if ( NULL == path.dentry )
    {
        MYASSERT( !"d_alloc_pseudo() failed\n" );
        rc = -ENOMEM;
        goto ErrorExit;
    }
    path.mnt = mntget( g_mwsocket_state.fs_mount );
    d_instantiate( path.dentry, inode );
    
    file = alloc_file( &path, FMODE_WRITE | FMODE_READ, &mwsocket_fops );
    if ( IS_ERR( file ) )
    {
        rc = PTR_ERR( file );
        MYASSERT( !"alloc_file() failed" );
        goto ErrorExit;
    }
    sockinst->file = file;

    file->f_flags |= flags;

    fd = get_unused_fd_flags( flags );
    if ( fd < 0 )
    {
        MYASSERT( !"get_unused_fd_flags() failed\n" );
        rc = -EMFILE;
        goto ErrorExit;
    }

    // Success
    sockinst->local_fd = fd;
    sockinst->f_flags = flags;

    fd_install( fd, sockinst->file );

    pr_debug( "Created socket instance %p, file=%p inode=%p fd=%d\n",
              sockinst, file, inode, fd );
ErrorExit:
    if ( rc )
    {
        // Cleanup partially-created file
        if ( file )
        {
            put_filp( file );
            path_put( &path );
        }
        if ( inode ) iput( inode );
        if ( sockinst )
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


// @brief Close the remote socket.
//
// @return 0 on success, or error, which could be either local or remote
static int
mwsocket_close_remote( IN mwsocket_instance_t * SockInst,
                       IN bool                  WaitForResponse )
{
    int rc = 0;
    mwsocket_active_request_t * actreq = NULL;
    mt_request_socket_close_t * close = NULL;

    if ( !MW_SOCKET_IS_FD( SockInst->remote_fd ) )
    {
        MYASSERT( !"Not an MW socket" );
        rc = -EINVAL;
        goto ErrorExit;
    }

    if ( SockInst->remote_close_requested )
    {
        pr_info( "Socket %x/%d was already closed on the INS. "
                  "Not requesting a remote close.\n",
                  SockInst->remote_fd, SockInst->local_fd );
        goto ErrorExit;
    }

    SockInst->remote_close_requested = true;

    rc = mwsocket_create_active_request( SockInst, &actreq );
    if ( rc ) goto ErrorExit;

    pr_debug( "Request %lx is closing socket %x/%d and %s wait\n",
              (unsigned long)actreq->id, SockInst->remote_fd, SockInst->local_fd,
              WaitForResponse ? "will" : "won't" );

    actreq->deliver_response = WaitForResponse;
    close = &actreq->rr.request.socket_close;
    close->base.type   = MtRequestSocketClose;
    close->base.size   = MT_REQUEST_SOCKET_CLOSE_SIZE;

    rc = mwsocket_send_request( actreq, true );
    if ( rc ) goto ErrorExit;

    if ( !WaitForResponse ) goto ErrorExit; // no more work

    rc = wait_for_completion_timeout( &actreq->arrived, GENERAL_RESPONSE_TIMEOUT );
    if ( 0 == rc )
    {
        pr_warn( "Timed out while waiting for response to close\n" );
        rc = -ETIME;
    }
    else
    {
        pr_debug( "Successfully waited for close to complete\n" );
        rc = 0;
    }

    rc = rc ? rc : actreq->rr.response.base.status;

ErrorExit:
    mwsocket_destroy_active_request( actreq );
    return rc;
}


static void
MWSOCKET_DEBUG_ATTRIB
mwsocket_destroy_active_request( mwsocket_active_request_t * Request )
{
    if ( NULL == Request )
    {
        return;
    }

    pr_debug( "Destroyed active request %p id=%lx\n",
              Request, (unsigned long) Request->id );

    // Clear the ID of the blocker to close, iff it is this request's
    atomic64_cmpxchg( &Request->sockinst->close_blockid,
                      Request->id, MT_ID_UNSET_VALUE );

    mwsocket_put_sockinst( Request->sockinst );

    // Remove from list
    mutex_lock( &g_mwsocket_state.active_request_lock );
    list_del( &Request->list_all );
    mutex_unlock( &g_mwsocket_state.active_request_lock );

    kmem_cache_free( g_mwsocket_state.active_request_cache, Request );
}


// @brief Gets a new active request struct from the cache and does basic init.
static int 
MWSOCKET_DEBUG_ATTRIB
mwsocket_create_active_request( IN mwsocket_instance_t * SockInst,
                                OUT mwsocket_active_request_t ** ActReq )
{
    mwsocket_active_request_t * actreq = NULL;
    int                          rc = 0;

    MYASSERT( SockInst );
    MYASSERT( ActReq );

    actreq = (mwsocket_active_request_t *)
        kmem_cache_alloc( g_mwsocket_state.active_request_cache,
                          GFP_KERNEL | __GFP_ZERO );
    if ( NULL == actreq )
    {
        MYASSERT( !"kmem_cache_alloc failed" );
        rc = -ENOMEM;
        goto ErrorExit;
    }

    // Success
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

    pr_verbose( "Created active request %p id=%lx\n",
                actreq, (unsigned long)id );

    *ActReq = actreq;
    
ErrorExit:
    return rc;
}


int
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
        if ( curr->id == Id )
        {
            *Request = curr;
            rc = 0;
            break;
        }
    }
    mutex_unlock( &g_mwsocket_state.active_request_lock );

    return rc;
}


void
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

// @brief Delivers signal to current process
//
// @return Returns pending error, or 0 if none
static int
mwsocket_pending_error( mwsocket_instance_t * SockInst,
                        mt_request_type_t     RequestType )
{
    int rc = 0;

    MYASSERT( SockInst );

    // These messages are exempt from pending errors, since they must
    // reach the INS.
    if ( MtRequestSocketShutdown == RequestType 
         || MtRequestSocketClose == RequestType )
    {
        goto ErrorExit;
    }

    if ( SockInst->remote_surprise_close )
    {
        // Once set, remote_surprise_close is never cleared in a sockinst.
        rc = -MW_ERROR_REMOTE_SURPRISE_CLOSE;

        // Operations behave differently when the remote side goes down...
        if ( MtRequestSocketSend == RequestType )
        {
            pr_debug( "Delivering SIGPIPE to process %d (%s) for socket %d\n",
                      SockInst->proc->pid, SockInst->proc->comm,
                      SockInst->local_fd );
            send_sig( SIGPIPE, current, 0 );
        }
    }
    else if (SockInst->pending_errno )
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
mwsocket_postproc_emit_netflow( mwsocket_active_request_t * ActiveRequest,
                                mt_response_generic_t     * Response )
{
    MYASSERT( ActiveRequest );
    MYASSERT( ActiveRequest->sockinst );
    MYASSERT( Response );

    if ( !mw_backchannel_consumer_exists() )
    {
        goto ErrorExit;
    }

    switch( Response->base.type )
    {
    case MtResponseSocketCreate:
        mw_backchannel_write_msg( "%lx: created\n",
                                  Response->base.sockfd );
        break;
    case MtResponseSocketShutdown:
    case MtResponseSocketClose:
        mw_backchannel_write_msg( "%lx: shutdown/closed\n",
                                  Response->base.sockfd );
        break;
    case MtResponseSocketConnect:
        mw_backchannel_write_msg( "%lx: connected\n",
                                  Response->base.sockfd );
        break;
    case MtResponseSocketBind:
        mw_backchannel_write_msg( "%lx: bound\n",
                                  Response->base.sockfd );
        break;
    case MtResponseSocketAccept:
        mw_backchannel_write_msg( "%lx: accepted from %pI4:%hu\n",
                                  Response->base.sockfd,
                                  &Response->socket_accept.sockaddr.sin_addr.s_addr,
                                  Response->socket_accept.sockaddr.sin_port );
        break;
    case MtResponseSocketSend:
        mw_backchannel_write_msg( "%lx: wrote %d bytes\n",
                                  Response->base.sockfd,
                                  Response->socket_send.count );
        break;
    case MtResponseSocketRecv:
        mw_backchannel_write_msg( "%lx: received %d bytes\n",
                                  Response->base.sockfd,
                                  Response->socket_recv.count );
        break;
    case MtResponseSocketRecvFrom:
        mw_backchannel_write_msg( "%lx: received %d bytes from %pI4:%hu\n",
                                  Response->base.sockfd,
                                  Response->socket_recvfrom.count,
                                  Response->socket_recvfrom.src_addr.sin_addr.s_addr,
                                  Response->socket_recvfrom.src_addr.sin_port );
        break;
    case MtResponseSocketListen:
        mw_backchannel_write_msg( "%lx: listening\n",
                                  Response->base.sockfd );
        break;
    case MtResponseSocketGetName:
    case MtResponseSocketGetPeer:
    case MtResponseSocketAttrib:
    case MtResponsePollsetQuery:
        // Ignored cases
        break;
    default:
        MYASSERT( !"Unhandled response" );
        break;
    }

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

    if ( MT_CLOSE_WAITS( Response ) )
    {
        if ( DEBUG_SHOW_TYPE( Response->base.type ) )
        {
            pr_verbose( "close() against fd %d will no longer wait for %lx \
                        completion\n",
                        ActiveRequest->sockinst->local_fd,
                        (unsigned long)ActiveRequest->id );
        }

        // Clear the ID of the blocker to close, iff it is this request's
        atomic64_cmpxchg( &ActiveRequest->sockinst->close_blockid,
                          (unsigned long) ActiveRequest->id,
                          (unsigned long) MT_ID_UNSET_VALUE );

        up_read( &ActiveRequest->sockinst->close_lock );
    }

    if ( MtResponseSocketSend == Response->base.type )
    {
        // If we're on the final response, clear out the state and
        // return the total sent, whether or not this succeeded.
        if ( (Response->base.flags & _MT_FLAGS_BATCH_SEND_FINI) )
        {
            ActiveRequest->sockinst->u_flags &= ~(_MT_FLAGS_BATCH_SEND_INIT
                                                  |_MT_FLAGS_BATCH_SEND
                                                  |_MT_FLAGS_BATCH_SEND_FINI);
            if ( Response->socket_send.count >= 0 )
            {
                Response->socket_send.count += ActiveRequest->sockinst->send_tally;
            }
            pr_debug( "Final batch send: total sent is %d bytes\n",
                      Response->socket_send.count );
        }
        else if ( Response->base.flags & _MT_FLAGS_BATCH_SEND )
        {
            MYASSERT( ActiveRequest->sockinst->u_flags & _MT_FLAGS_BATCH_SEND );
            if ( Response->socket_send.count > 0 )
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

    if ( Response->base.flags & _MT_FLAGS_REMOTE_CLOSED
        && !ActiveRequest->sockinst->remote_surprise_close )
    {
        ActiveRequest->sockinst->remote_surprise_close = true;
        pr_debug( "Remote side of fd %d (pid %d [%s]) closed. "
                  "Next recv/send call will indicate this.\n",
                  ActiveRequest->sockinst->local_fd,
                  ActiveRequest->sockinst->proc->pid,
                  ActiveRequest->sockinst->proc->comm );
    }

    // N.B. Do not use ActiveRequest->response -- it might not be valid yet.
    if ( status < 0 )
    {
        goto ErrorExit;
    }

    // Success case
    switch( Response->base.type )
    {
    case MtResponseSocketCreate:
        pr_debug( "Create in %d [%s]: fd %d ==> %x\n",
                  ActiveRequest->sockinst->proc->pid,
                  ActiveRequest->sockinst->proc->comm,
                  ActiveRequest->sockinst->local_fd,
                  Response->base.sockfd );
        ActiveRequest->sockinst->remote_fd = Response->base.sockfd;
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
 * Useful especially for dealing with successful call to accept(), which creates a new socket.
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

    if ( MtResponseSocketAccept != Response->base.type
        || Response->base.status < 0 )
    {
        goto ErrorExit;
    }

    // Populate the new sockinst's flags from the original request,
    // which the INS carried over for us
    rc = mwsocket_create_sockinst( &acceptinst,
                                   Response->socket_accept.flags,
                                   true );
    if ( rc )
    {
        // The socket was created remotely but the local creation
        // failed. We cannot clean up the remote side because we don't
        // have the backing sockinst to send the close request.
        pr_err( "Failed to create local socket instance. "
                "Leaking remote socket %x; local error %d\n",
                Response->base.sockfd, rc );
        goto ErrorExit;
    }


    // The local creation succeeded. Update Response to reflect the
    // local socket.
    acceptinst->remote_fd = Response->base.sockfd;

    Response->base.sockfd
        = Response->base.status
        = acceptinst->local_fd;

    pr_debug( "Accept in %d [%s]: fd %d ==> %x\n",
              acceptinst->proc->pid, acceptinst->proc->comm,
              acceptinst->local_fd, acceptinst->remote_fd );
ErrorExit:
    return rc;
}




// @brief Prepares request and socket instance state prior to putting
// request on ring buffer.
static int
mwsocket_pre_process_request( mwsocket_active_request_t * ActiveRequest )
{
    MYASSERT( ActiveRequest );
    MYASSERT( ActiveRequest->sockinst );

    int rc = 0;
    mt_request_generic_t * request = &ActiveRequest->rr.request;

    MYASSERT( MT_IS_REQUEST( request ) );

    // Will the user wait for the response to this request? If so, update state
    if ( MT_REQUEST_CALLER_WAITS( request ) )
    {
        // This write() will be followed immediately by a
        // read(). Indicate the ID that blocks the calling thread.
        ActiveRequest->sockinst->read_expected = true;
        ActiveRequest->sockinst->blockid = ActiveRequest->id;
        ActiveRequest->deliver_response = true;
    }

    if ( MT_CLOSE_WAITS( request ) )
    {
        down_read( &ActiveRequest->sockinst->close_lock );

        atomic64_set( &ActiveRequest->sockinst->close_blockid,
                      (unsigned long) ActiveRequest->id );
    }
    else if ( MtRequestSocketClose == request->base.type )
    {
        bool waiting = false;
        mt_id_t id = (mt_id_t)
            atomic64_read( &ActiveRequest->sockinst->close_blockid );
        if ( MT_ID_UNSET_VALUE != id )
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

#if 0 // XXXX: update to this when we're on a more recent kernel
        rc = down_write_killable( &ActiveRequest->sockinst->close_lock );
        if ( -EINTR == rc )
        {
            pr_info( "Wait for in-flight ID %lx was killed\n", (unsigned long)id );
        }
#endif
        int ct = 0;
        bool acquired = false;
        while( ct++ < 2 )
        {
            if ( down_write_trylock( &ActiveRequest->sockinst->close_lock ) )
            {
                acquired = true;
                break;
            }
            mwsocket_wait( GENERAL_RESPONSE_TIMEOUT );
        }

        if ( !acquired )
        {
            pr_warning( "Closing socket %d [pid %d] despite outstanding IO \
                         on it (ID %lx).\n",
                        ActiveRequest->sockinst->local_fd,
                        ActiveRequest->sockinst->proc->pid,
                        (unsigned long)id );
        }

        atomic64_set( &ActiveRequest->sockinst->close_blockid,
                      MT_ID_UNSET_VALUE );
    }

    switch( request->base.type )
    {
    case MtRequestSocketSend:
        // Handle incoming scatter-gather send requests

        pr_debug( "send flags: %x\n", request->base.flags );
        // Is this the first scatter-gather request in a possible
        // series? We could also check _MT_FLAGS_BATCH_SEND_INIT.
        if ( !( ActiveRequest->sockinst->u_flags & _MT_FLAGS_BATCH_SEND )
             && (request->base.flags & _MT_FLAGS_BATCH_SEND) )
        {
            pr_debug( "Starting batch send\n" );
            ActiveRequest->sockinst->u_flags |= _MT_FLAGS_BATCH_SEND;
            ActiveRequest->sockinst->send_tally = 0;
        }

        // Check: is this the final request but without synchronization?
        if ( (request->base.flags & _MT_FLAGS_BATCH_SEND_FINI)
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
mwsocket_send_request( IN mwsocket_active_request_t * ActiveRequest,
                       IN bool                        WaitForRing )
{
    int rc = 0;
    uint8_t * dest = NULL;
    mt_request_base_t * base = &ActiveRequest->rr.request.base;

    // Perform minimal base field prep
    base->sig    = MT_SIGNATURE_REQUEST;
    base->sockfd = ActiveRequest->sockinst->remote_fd;
    base->id     = ActiveRequest->id;

    // Hold this for duration of the operation. 
    mutex_lock( &g_mwsocket_state.request_lock );

    if ( !MT_IS_REQUEST( &ActiveRequest->rr.request ) )
    {
        MYASSERT( !"Invalid request given\n" );
        rc = -EINVAL;
        goto ErrorExit;
    }

    rc = mw_xen_get_next_request_slot( WaitForRing, &dest);
    if( rc )
    {
        goto ErrorExit;
    }
    
    // Populate the request further as needed. Do this only when we're
    // certain the send will succeed, as it modifies the sockinst
    // state such that the system will fail if a response is not
    // received.
    rc = mwsocket_pre_process_request( ActiveRequest );
    if ( rc ) goto ErrorExit;

    if ( DEBUG_SHOW_TYPE( ActiveRequest->rr.request.base.type ) )
    {
        pr_debug( "Sending request %lx fd %lx/%d type %x\n",
                  (unsigned long)ActiveRequest->rr.request.base.id,
                  (unsigned long)ActiveRequest->rr.request.base.sockfd,
                  ActiveRequest->sockinst->local_fd,
                  ActiveRequest->rr.request.base.type );
    }

    mw_xen_dispatch_request( &ActiveRequest->rr.request, dest );
    
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
mwsocket_send_message( IN mwsocket_instance_t * SockInst,
                       IN mt_request_generic_t * Request,
                       IN bool                   AwaitResponse )
{
    int rc = 0;
    int remoterc = 0;
    mwsocket_active_request_t * actreq = NULL;
    
    rc = mwsocket_create_active_request( SockInst, &actreq );
    if ( rc ) goto ErrorExit;

    actreq->deliver_response = AwaitResponse;

    memcpy( &actreq->rr.request, Request, Request->base.size );

    rc = mwsocket_send_request( actreq, true );
    if ( rc ) goto ErrorExit;

    if ( !AwaitResponse ) goto ErrorExit;

    wait_for_completion_interruptible( &actreq->arrived );
    pr_debug( "Response arrived: %lx\n", (unsigned long)actreq->id );

    remoterc = -actreq->rr.response.base.status;

ErrorExit:
    // If the active request was created, release it and socket instance
    mwsocket_destroy_active_request( actreq );

    if ( remoterc )
    {
        rc = remoterc;
    }
    
    return rc;
}



/******************************************************************************
 * Main functions for worker threads. There are two: the response
 * consumer, and the poll monitor.
 ******************************************************************************/

static int
mwsocket_response_consumer( void * Arg )
{
    int                         rc = 0;
    mwcomms_ins_data_t        * ins = NULL;
    mwsocket_active_request_t * actreq = NULL;
    mt_response_generic_t     * response = NULL;
    bool                        available = false;
    

    // TODO
    // Wait for there to be a ring available for use
    rc = wait_for_completion_interruptible( &g_mwsocket_state.xen_iface_ready );
    if ( rc < 0 )
    {
        pr_info( "Received interrupt before ring ready\n" );
        goto ErrorExit;
    }

    // Completion succeeded
    if ( g_mwsocket_state.pending_exit )
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
    
    pr_debug("Entering response consumer loop\n");

    while( true )
    {

        do
        {

            available = mw_xen_response_available( &ins );
            if( !available )
            {
                if( g_mwsocket_state.pending_exit )
                {
                    goto ErrorExit;
                }
                
                if ( down_interruptible( &g_mwsocket_state.event_channel_sem ) )
                {
                    rc = -EINTR;
                    pr_info( "Received interrupt in response consumer thread\n" );
                    goto ErrorExit;
                }
            }

        } while (!available);
        

        rc = mw_xen_get_next_response( &response,  ins );
         
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
        if ( rc )
        {
            // Active requests should *never* just "disappear". They
            // are destroyed in an orderly fashion either hxere (below)
            // or in mwsocket_read(). This state can be reached if a
            // requestor times out on the response arrival.
            pr_warn( "Couldn't find active request with ID %lx\n",
                     (unsigned long) response->base.id );

            mw_xen_mark_response_consumed( ins );
            continue; // move on
        }

        
        //
        // The active request has been found.
        //
        mwsocket_postproc_emit_netflow( actreq, response );

        mwsocket_postproc_no_context( actreq, response );

        
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

        if ( !actreq->deliver_response
             || (actreq->from_user
                 && (actreq->sockinst->proc->flags & PF_EXITING)
                 && !(actreq->sockinst->proc->flags & PF_FORKNOEXEC) ) )
        {
            if ( actreq->deliver_response )
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
            // The caller is healthy and wants the response.
            memcpy( &actreq->rr.response, response, response->base.size );
            complete_all( &actreq->arrived );
        }

        // We're done with this slot of the ring
        mw_xen_mark_response_consumed( ins );

        
        //Post Processing
        //Mark consumed

    } // while( true )

ErrorExit:
    // Inform the cleanup function that this thread is done.
    pr_debug("Response consumer thread exiting\n");
    complete( &g_mwsocket_state.response_reader_done );
    return rc;
}


static int
MWSOCKET_DEBUG_ATTRIB
mwsocket_poll_handle_notifications( IN mwsocket_instance_t * SockInst )
{
    int rc = 0;
    mwsocket_active_request_t     * actreq = NULL;
    mt_request_pollset_query_t   * request = NULL;
    mt_response_pollset_query_t * response = NULL;

    rc = mwsocket_create_active_request( SockInst, &actreq );
    if ( rc ) goto ErrorExit;

    request = &actreq->rr.request.pollset_query;
    request->base.type = MtRequestPollsetQuery;
    request->base.size = MT_REQUEST_POLLSET_QUERY_SIZE;

    // Sent the request. We will wait for response.
    actreq->deliver_response = true;

    rc = mwsocket_send_request( actreq, true );
    if ( rc ) goto ErrorExit;

    // Wait
    rc = wait_for_completion_timeout( &actreq->arrived,
                                      POLL_MONITOR_RESPONSE_TIMEOUT );
    if ( 0 == rc )
    {
        // The response did not arrive, so we cannot process it
        pr_warn( "Timed out while waiting for response %lx\n",
                 (unsigned long)actreq->id );
        rc = -ETIME;
        goto ErrorExit;
    }

    rc = 0;
    response = &actreq->rr.response.pollset_query;

    if ( response->base.status < 0 )
    {
        pr_err( "Remote poll() failed: %d\n", response->base.status );
        rc = response->base.status;
        goto ErrorExit;; // don't inform anyone
    }

    if ( 0 == response->count ) goto ErrorExit;

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
        currsi->poll_events = 0;    

        for ( int i = 0; i < response->count; ++i )
        {
            // Find the associated sockinst, matching up by (remote) mwsocket
        
            if ( currsi->remote_fd != response->items[i].sockfd ) continue;

            // Transfer response's events to sockinst's, notify poll()
            // N.B. MW_POLL* == linux values
            currsi->poll_events = response->items[i].events;

            pr_verbose_poll( "%d [%s] fd %d shows events %lx\n",
                             currsi->proc->pid, currsi->proc->comm,
                             currsi->local_fd, currsi->poll_events );
            MYASSERT( !(currsi->poll_events & (MW_POLLHUP | MW_POLLNVAL) ) );
            break;
        }
    }
    mutex_unlock( &g_mwsocket_state.sockinst_lock );

    wake_up_interruptible( &g_mwsocket_state.waitq );

ErrorExit:
    mwsocket_destroy_active_request( actreq );
    return rc;
}


/**
 * @brief Thread function that monitors for local changes in pollset
 * and for remote IO events.
 */
static int
MWSOCKET_DEBUG_ATTRIB
mwsocket_poll_monitor( void * Arg )
{
    int rc = 0;
    mwsocket_instance_t * sockinst = NULL;

    // Create a socket instance without a backing file 
    rc = mwsocket_create_sockinst( &sockinst, 0, false );
    if ( rc ) goto ErrorExit;

    // Wait for the ring buffer's initialization to complete
    rc = wait_for_completion_interruptible( &g_mwsocket_state.xen_iface_ready );
    if ( rc < 0 )
    {
        pr_info( "Received interrupt before ring ready\n" );
        goto ErrorExit;
    }

    // Main loop
    while ( true )
    {
        if ( g_mwsocket_state.pending_exit )
        {
            pr_debug( "Detecting pending exit. Leaving.\n" );
            break;
        }

        // Sleep
        mwsocket_wait( POLL_MONITOR_QUERY_INTERVAL );
        
        // If there are any "real" open mwsockets at all, complete a
        // poll query exchange. The socket instance from this function
        // doesn't count, since it's only for poll monitoring.
        if ( atomic_read( &g_mwsocket_state.sockinst_count ) <= 1 )
        {
            continue;
        }

        rc = mwsocket_poll_handle_notifications( sockinst );
        if ( rc )
        {
            pr_err( "Error handling poll notifications: %d\n", rc );
            continue;
        }
    } // while

ErrorExit:
    mwsocket_put_sockinst( sockinst );
    // Inform the cleanup function that this thread is done.
    complete( &g_mwsocket_state.poll_monitor_done );
    return rc;
}


/**
 * @brief Processes an mwsocket creation request. Reachable via IOCTL.
 */
int
mwsocket_create( OUT mwsocket_t * SockFd,
                 IN  int          Domain,
                 IN  int          Type,
                 IN  int          Protocol )
{
    int rc = 0;
    mwsocket_active_request_t  * actreq = NULL;
    mwsocket_instance_t        * sockinst = NULL;
    mt_request_socket_create_t   create;

    MYASSERT( SockFd );
    *SockFd = (mwsocket_t)-1;
    if ( !g_mwsocket_state.is_xen_iface_ready )
    {
        MYASSERT( !"Ring has not been initialized\n" );
        rc = -ENODEV;
        goto ErrorExit;
    }

    // We're creating 2 things here:
    //  (1) a pseudo-socket structure in the local OS, and
    //  (2) the new socket on the client,

    // (1) Local tasks first
    rc = mwsocket_create_sockinst( &sockinst, 0, true );
    if ( rc ) goto ErrorExit;
    
    // (2) Register the new socket on the client
    rc = mwsocket_create_active_request( sockinst, &actreq );
    if ( rc ) goto ErrorExit;

    create.base.type     = MtRequestSocketCreate;
    create.base.size     = MT_REQUEST_SOCKET_CREATE_SIZE;
    create.sock_fam      = Domain; // family == domain
    create.sock_type     = Type;
    create.sock_protocol = Protocol;

    // Send request, wait for response
    rc = mwsocket_send_message( sockinst,
                                (mt_request_generic_t *)&create,
                                true );
    if ( rc ) goto ErrorExit;

ErrorExit:

    mwsocket_destroy_active_request( actreq );
    if ( 0 == rc )
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
mwsocket_verify( const struct file * File )
{
    return (File->f_op == &mwsocket_fops);
}


static int
mwsocket_handle_attrib( IN struct file            * File,
                        IN mwsocket_attrib_t * SetAttribs )
{
    int rc = 0;
    mwsocket_instance_t * sockinst = NULL;
    mwsocket_active_request_t * actreq = NULL;
    mt_request_socket_attrib_t * request = NULL;
    mt_response_socket_attrib_t * response = NULL;

    rc = mwsocket_find_sockinst( &sockinst, File );
    if ( rc )   goto ErrorExit;

    // Create a new active request
    rc = mwsocket_create_active_request( sockinst, &actreq );
    if ( rc ) goto ErrorExit;

    // Populate the request. Do not validate request->attrib here.
    actreq->deliver_response = true; // we'll wait
    request = &actreq->rr.request.socket_attrib;
    request->base.type = MtRequestSocketAttrib;
    request->base.size = MT_REQUEST_SOCKET_ATTRIB_SIZE;

    request->modify = SetAttribs->modify;
    request->attrib = SetAttribs->attrib;
    request->value  = SetAttribs->value;

    rc = mwsocket_send_request( actreq, true );
    if ( rc ) goto ErrorExit;

    // Wait
    rc = wait_for_completion_timeout( &actreq->arrived, GENERAL_RESPONSE_TIMEOUT );
    if ( 0 == rc )
    {
        pr_warn( "Timed out while waiting for response\n" );
        rc = -ETIME;
        goto ErrorExit;
    }
    rc = 0;
    
    response = &actreq->rr.response.socket_attrib;
    if ( response->base.status < 0 )
    {
        rc = response->base.status;
        pr_err( "Operation failed on remote side: %d\n", rc );
        goto ErrorExit;
    }

    if ( SetAttribs->modify )
    {
        SetAttribs->value = response->outval;
    }

ErrorExit:
    mwsocket_destroy_active_request( actreq );
    return rc;
}


static ssize_t
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
    if ( rc )
    {
        MYASSERT( !"Couldn't find OS data associated with file\n" );
        goto ErrorExit;
    }

    if ( !sockinst->read_expected )
    {
        MYASSERT( !"Calling read() but fire-and-forget was indicated in write()" );
        rc = -EINVAL;
        goto ErrorExit;
    }

    // The read was attempted
    sockinst->read_expected = false;

    // Now find the outstanding request/response
    rc = mwsocket_find_active_request_by_id( &actreq, sockinst->blockid );
    if ( rc )
    {
        MYASSERT( !"Couldn't find outstanding request with ID." );
        goto ErrorExit;
    }

    if ( !actreq->deliver_response )
    {
        MYASSERT( !"Request was marked as non-blocking. No data is available." );
        rc = -EINVAL;
        goto ErrorExit;
    }

    if ( wait_for_completion_interruptible( &actreq->arrived ) )
    {
        // Keep the request alive. The user might try again.
        pr_warn( "read() was interrupted\n" );
        rc = -EINTR;
        goto ErrorExit;
    }

    // Data is ready for us. Validate and copy to user.
    response = (mt_response_generic_t *) &actreq->rr.response;
    if ( Len < response->base.size )
    {
        MYASSERT( !"User buffer is too small for response" );
        rc = -EINVAL;
        goto ErrorExit;
    }

    // If this is from accept(), install a new file descriptor in the
    // calling process and report it to the user via the response.
    rc = mwsocket_postproc_in_task( actreq, response );
    if ( rc ) goto ErrorExit;

    // Is there a pending error on this socket? If so, return it.
    rc = mwsocket_pending_error( sockinst, response->base.type & MT_TYPE_MASK );
    if ( rc )
    {
        pr_debug( "Delivering error: %d\n", (int)rc );
        goto ErrorExit;
    }

    rc = copy_to_user( Bytes, (void *)response, response->base.size );
    if ( rc )
    {
        MYASSERT( !"copy_to_user() failed" );
        rc = -EFAULT;
        goto ErrorExit;
    }
    
    // Success
    rc = response->base.size;

ErrorExit:
    if ( -EINTR != rc ) // XXXX: hacky -- could leak
    {
        // The "active" request is now dead
        //MYASSERT( MtResponseSocketAccept != actreq->rr.response.base.type );
        mwsocket_destroy_active_request( actreq );
    }

    return rc;
}








static ssize_t
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

    if ( Len < MT_REQUEST_BASE_SIZE )
    {
        MYASSERT( !"User provided too few bytes." );
        rc = -EINVAL;
        goto ErrorExit;
    }

    // Peek to discover the base
    rc = copy_from_user( &base, Bytes, sizeof(base) );
    if ( rc )
    {
        MYASSERT( !"copy_from_user failed." );
        rc = -EFAULT;
        goto ErrorExit;
    }

    rc = mwsocket_find_sockinst( &sockinst, File );
    if ( rc )   goto ErrorExit;

    if ( sockinst->read_expected )
    {
        MYASSERT( !"Calling write() but read() expected" );
        rc = -EINVAL;
        goto ErrorExit;
    }

    // Create a new active request
    rc = mwsocket_create_active_request( sockinst, &actreq );
    if ( rc ) goto ErrorExit;

    actreq->from_user = true;

    // Populate the request: read the entire thing from user
    // memory. We could read just the base here and later copy the
    // body from user memory into the ring buffer, but the extra calls
    // are not worth it performance-wise.
    request = &actreq->rr.request;

    rc = copy_from_user( request, Bytes, Len );
    if ( rc )
    {
        MYASSERT( !"copy_from_user failed." );
        rc = -EFAULT;
        goto ErrorExit;
    }

    if ( request->base.size > Len )
    {
        MYASSERT( !"Request is longer than provided buffer." );
        rc = -EINVAL;
        goto ErrorExit;
    }

    // Is there a pending error on this socket? If so, deliver it.
    rc = mwsocket_pending_error( sockinst, request->base.type );
    if ( rc )
    {
        pr_debug( "Delivering error: %d\n", (int)rc );
        goto ErrorExit;
    }

    // Write to the ring. If the ring is full, we will wait here on
    // behalf of the user. This is noticably faster (~100ms/MB) than
    // having the caller wait and try again later.
    rc = mwsocket_send_request( actreq, true );
    if ( rc ) goto ErrorExit;

    sent = true;

ErrorExit:
    // We're returning an error - don't expect a read
    if ( rc )
    {
        sockinst->read_expected = false;
    }

    if ( rc && !sent )
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
    if ( rc )
    {
        pr_err( "Called IOCTL on an invalid file %p\n", File );
        rc = -EBADFD;
        goto ErrorExit;
    }

    switch( Cmd )
    {
    case MW_IOCTL_SOCKET_ATTRIBUTES:
        rc = copy_from_user( &attrib, (void *)Arg, sizeof(attrib) );
        if ( rc )
        {
            MYASSERT( !"Invalid memory provided\n" );
            rc = -EFAULT;
            goto ErrorExit;
        }

        rc = mwsocket_handle_attrib( File, &attrib );
        if ( rc ) goto ErrorExit;

        if ( attrib.modify )
        {
            rc = copy_to_user( &uattrib->value,
                               &attrib.value,
                               sizeof(attrib.value) );
            if ( rc )
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
    pr_verbose( "Processing poll(), fd %d\n",
                sockinst->local_fd );
    
    poll_wait( File, &g_mwsocket_state.waitq, PollTbl );

    // Lock used by poll monitor during socket instance update
    mutex_lock( &g_mwsocket_state.sockinst_lock );

    events = sockinst->poll_events;
    sockinst->poll_events = 0;

    mutex_unlock( &g_mwsocket_state.sockinst_lock );

    if ( events )
    {
        pr_debug( "Returning events %lx, fd %d\n", events, sockinst->local_fd );
    }

    return events;
}


static int
mwsocket_release( struct inode *Inode,
                  struct file * File )
{
    int rc = 0;
    mwsocket_instance_t * sockinst = NULL;

    // Do not incur a decrement against our module for this close(),
    // since the file we're closing was not opened by an open()
    // callback.
    __module_get( THIS_MODULE );

    rc = mwsocket_find_sockinst( &sockinst, File );
    if ( rc )
    {
        MYASSERT( !"Failed to find associated socket instance" );
        rc = -EBADFD;
        goto ErrorExit;
    }

    pr_debug( "Processing release() on fd=%d\n", sockinst->local_fd );

    // Close the remote socket only if it exists. It won't exist in
    // the case where accept() was called but hasn't returned yet. In
    // that case, we've created a local sockinst, but the remote
    // socket does not yet exist.
    if ( MW_SOCKET_IS_FD( sockinst->remote_fd ) )
    {
        rc = mwsocket_close_remote( sockinst, true );
        // fall-through
    }

    mwsocket_put_sockinst( sockinst );

ErrorExit:

    return rc;
}


/******************************************************************************
 * Module-level init and fini function
 ******************************************************************************/
int
mwsocket_init( mw_region_t * SharedMem )
{
    // Do everything we can, then wait for the ring to be ready
    int rc = 0;

    bzero( &g_mwsocket_state, sizeof(g_mwsocket_state) );

    mutex_init( &g_mwsocket_state.request_lock );
    mutex_init( &g_mwsocket_state.active_request_lock );
    mutex_init( &g_mwsocket_state.sockinst_lock );

    sema_init( &g_mwsocket_state.event_channel_sem, 0 );
    init_completion( &g_mwsocket_state.response_reader_done );
    init_completion( &g_mwsocket_state.poll_monitor_done );
    init_completion( &g_mwsocket_state.xen_iface_ready );

    INIT_LIST_HEAD( &g_mwsocket_state.active_request_list );
    INIT_LIST_HEAD( &g_mwsocket_state.sockinst_list );
    init_waitqueue_head( &g_mwsocket_state.waitq );      
    
    gfn_new_inode_pseudo = (pfn_new_inode_pseudo_t *)
        kallsyms_lookup_name( "new_inode_pseudo" );
    gfn_dynamic_dname = (pfn_dynamic_dname_t *)
        kallsyms_lookup_name( "dynamic_dname" );

    if ( NULL == gfn_new_inode_pseudo
        || NULL == gfn_dynamic_dname )
    {
        MYASSERT( !"Couldn't find required kernel function\n" );
        rc = -ENXIO;
        goto ErrorExit;
    }
    
    rc = mwsocket_fs_init();
    if ( rc ) goto ErrorExit;

    g_mwsocket_state.active_request_cache =
        kmem_cache_create( "mw_active_requests",
                           sizeof( mwsocket_active_request_t ),
                           0, 0, NULL );

    if ( NULL == g_mwsocket_state.active_request_cache )
    {
        MYASSERT( !"kmem_cache_create() failed\n" );
        rc = -ENOMEM;
        goto ErrorExit;
    }

    g_mwsocket_state.sockinst_cache =
        kmem_cache_create( "mw_socket_instances",
                           sizeof( mt_request_generic_t ),
                           0, 0, NULL );

    if ( NULL == g_mwsocket_state.sockinst_cache )
    {
        MYASSERT( !"kmem_cache_create() failed\n" );
        rc = -ENOMEM;
        goto ErrorExit;
    }

    // Response consumer
    g_mwsocket_state.response_reader_thread =
        kthread_run( &mwsocket_response_consumer,
                     NULL,
                     "MwMsgConsumer" );
    if ( NULL == g_mwsocket_state.response_reader_thread )
    {
        MYASSERT( !"kthread_run() failed\n" );
        rc = -ESRCH;
        goto ErrorExit;
    }

    // Poll monitor thread
    g_mwsocket_state.poll_monitor_thread =
        kthread_run( &mwsocket_poll_monitor,
                     NULL,
                     "MwPollMonitor" );
    if ( NULL == g_mwsocket_state.poll_monitor_thread )
    {
        MYASSERT( !"kthread_run() failed\n" );
        rc = -ESRCH;
        goto ErrorExit;
    }

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

    // Destroy response consumer -- kick it. It might be waiting for
    // the ring to become ready, or it might be waiting for responses
    // to arrive on the ring. Wait for it to complete so shared
    // resources can be safely destroyed below.
    g_mwsocket_state.pending_exit = true;
    complete_all( &g_mwsocket_state.xen_iface_ready );

    up( &g_mwsocket_state.event_channel_sem );
    
    if ( NULL != g_mwsocket_state.response_reader_thread )
    {
        wait_for_completion( &g_mwsocket_state.response_reader_done );
    }

    // Similarly, destroy the poll notification thread. It regularly
    // checks pending_exit, so kicking isn't necessary.
    if ( NULL != g_mwsocket_state.poll_monitor_thread )
    {
        wait_for_completion( &g_mwsocket_state.poll_monitor_done );
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
    
    if ( NULL != g_mwsocket_state.active_request_cache )
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

    if ( NULL != g_mwsocket_state.sockinst_cache )
    {
        kmem_cache_destroy( g_mwsocket_state.sockinst_cache );
    }

    // Pseudo-filesystem cleanup
    if ( NULL != g_mwsocket_state.fs_mount )
    {
        kern_unmount( g_mwsocket_state.fs_mount );
    }

    if ( g_mwsocket_state.fs_registered )
    {
        unregister_filesystem( &mwsocket_fs_type );
    }
}
