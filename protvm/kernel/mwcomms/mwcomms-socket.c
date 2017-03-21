/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

//
// Implementation of MW sockets. An MW socket is backed by a
// registered file object and file descriptor with its own file
// operations.
//

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
#include <message_types.h>
#include <xen_keystore_defs.h>

// Defines:
// mwevent_sring_t
// mwevent_front_ring_t
// mwevent_back_ring_t
DEFINE_RING_TYPES( mwevent, mt_request_generic_t, mt_response_generic_t );

// Magic for the mwsocket filesystem
#define MWSOCKET_FS_MAGIC  0x4d77536f // MwSo

// Disables optimization on a per-function basis. 
#define MWSOCKET_DEBUG_ATTRIB  __attribute__((optimize("O0")))

// Is the remote side checking for Xen events?
#define MW_DO_SEND_RING_EVENTS 0

// Times - self-explanatory
#define GENERAL_RESPONSE_TIMEOUT      ( HZ * 2)
#define POLL_MONITOR_RESPONSE_TIMEOUT ( HZ * 1 )
//#define POLL_MONITOR_QUERY_INTERVAL   ( HZ * 1 ) // >= 1 sec
#define POLL_MONITOR_QUERY_INTERVAL   ( HZ >> 3 ) // 1/8 sec

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


//
// For tracking per-mwsocket data. An mwsocket is built on top of the
// mwsocket filesystem. A good example of this model is found in the
// Linux kernel: fs/pipe.c
//
typedef struct _mwsocket_instance
{
    //
    // Local kernel registration info
    //

    // The process in which this socket was created
    struct task_struct * proc;
    struct inode       * inode;
    struct file        * file;
    int                  local_fd; // local FD for this mwsocket

    // poll() support
    unsigned long     poll_events;

    // Our latest known value for the file->f_flags, used to track changes.
    int             flags;
    
    // Remote (INS) value for this socket
    mw_socket_fd_t  remote_fd;

    // Error encountered on INS that has not (yet) been delivered to
    // caller. Supports fire-and-forget model.
    int              pending_errno;
    bool             pending_sigpipe;
    bool             delivered_sigpipe;

    // How many active requests are using this mwsocket? N.B. due to
    // the way release() is implemented on the INS, it is not valid to
    // destroy an mwsocket instance immediately upon release. Destroy
    // instead when this count reaches 0.
    atomic_t            refct;

    // The ID of the blocking request. Only one at a time!
    mt_id_t             blockid;

    // Did the user indicate that a read would follow the write?
    bool                read_expected;

    bool                remote_close_requested;

    bool                release_started;

    //
    // For accept() support
    //
    //struct _mwsocket_active_request * accept_actreq;

    struct mutex lock; // ??????????

    // Valid only while an accept() against this FD is
    // in-flight. N.B. We must obtain an FD while in the context of
    // the calling process.
    struct  _mwsocket_instance * child_inst;

    // All the instances. Must hold global sockinst_lock to access.
    struct list_head    list_all;

} mwsocket_instance_t;


//
// For tracking state on requests whose responses have not yet arrived
//
typedef struct _mwsocket_active_request
{
    mt_id_t id;

    // The process that is issuing IO against the sockinst
    struct task_struct * issuer;

    // Will the requestor wait? If so, we deliver the response by
    // copying it to the response field here and completing 'arrived'
    // variable. Requestor can be user or kernel.
    bool   deliver_response;

    // Track whether it was delivered?
    
    // Signaled when the response arrives and is available in the
    // response field. Only signaled if deliver_response is true.
    struct completion arrived;

    // These are large, and we only need one at a time.
    union
    {
        mt_request_generic_t request;        

        // The response is copied here iff someone is waiting on it.
        mt_response_generic_t response;
    };

    //
    // For accept() support
    //
    //mwsocket_instance_t * accept_sockinst;

    // OS-data, including requesting thread.
    mwsocket_instance_t * sockinst;

    struct list_head list_all;
} mwsocket_active_request_t;


typedef struct _mwsocket_globals {
    // Used to signal socket subsystem threads
    struct completion ring_ready;
    bool              is_ring_ready;

    // Ring memory descriptions
    mw_region_t       ring;
    struct mwevent_sring * sring;
    struct mwevent_front_ring front_ring;
    
    // Lock on ring access
    struct mutex request_lock;

    // Indicates response(s) available
    struct semaphore event_channel_sem;

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

    // Filesystem data
    struct vfsmount * fs_mount;
    bool   fs_registered;

    // Kernel thread info: response consumer
    struct task_struct * response_reader_thread;
    struct completion    response_reader_done;

    // Kernel thread info: poll notifier
    struct task_struct * poll_monitor_thread;
    struct completion    poll_monitor_done;

    // Support for poll(): to inform waiters of data
    wait_queue_head_t   waitq;
    //struct mutex        poll_lock;
    
    bool   pending_exit;
} mwsocket_globals_t;

mwsocket_globals_t g_mwsocket_state;

// Kernel symbols

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
mwsocket_send_request( IN mwsocket_active_request_t * ActiveReq );

static int
mwsocket_send_message( IN mwsocket_instance_t  * SockInst,
                       IN mt_request_generic_t * Request,
                       IN bool                   AwaitResponse );

/******************************************************************************
 * Primitive Functions
 ******************************************************************************/

#define MWSOCKET_UNASSIGNED_ID (mt_id_t)0
// Tracks the current request ID. Corresponds to mt_request_base_t.id
static mt_id_t
mwsocket_get_next_id( void )
{
    static atomic64_t counter = ATOMIC64_INIT( 0 );
    mt_id_t id = (mt_id_t) atomic64_inc_return( &counter );

    if ( MWSOCKET_UNASSIGNED_ID == id )
    {
        id = (mt_id_t) atomic64_inc_return( &counter );
    }
    
    return id;
}


// Callback for arrival of event on Xen event channel
void
mwsocket_event_cb ( void )
{
    up( &g_mwsocket_state.event_channel_sem );
}

void
mwsocket_notify_ring_ready( void )
{
    SHARED_RING_INIT( g_mwsocket_state.sring );

    FRONT_RING_INIT( &g_mwsocket_state.front_ring,
                     g_mwsocket_state.sring,
                     g_mwsocket_state.ring.pagect * PAGE_SIZE );

    g_mwsocket_state.is_ring_ready = true;
    complete_all( &g_mwsocket_state.ring_ready );
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

// @breif Reference the socket instance
static void
mwsocket_get_sockinst(  mwsocket_instance_t * SockInst )
{
    int val = 0;
    
    MYASSERT( SockInst );

    val = atomic_inc_return( &SockInst->refct );

    pr_debug( "Referenced socket instance %p fd=%d, refct=%d\n",
              SockInst, SockInst->local_fd, val );

    //MYASSERT( val < 20 );

}

// @brief Dereferences the socket instance, destroying upon 0 reference count
static void
MWSOCKET_DEBUG_ATTRIB
mwsocket_put_sockinst( mwsocket_instance_t * SockInst )
{
    int val = 0;
    
    if ( NULL == SockInst )
    {
        pr_debug( "Called with NULL\n" );
        goto ErrorExit;
    }

    // XXXX: use file_get() and fput() instead of our own reference
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
                          IN bool CreateBackingFile )
{
    int rc = 0;
    mwsocket_instance_t * sockinst = NULL;
    struct inode * inode = NULL;
    struct file  * file  = NULL;
    struct path path;
    static struct qstr name = { .name = "" };
    unsigned int flags = O_RDWR;
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
    
    sockinst->proc = current;
    // refct starts at 1; an extra put() is done upon close()
    atomic_set( &sockinst->refct, 1 );

    mutex_init( &sockinst->lock );

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
    sockinst->flags = flags;

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
            list_del( &sockinst->list_all );
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
    close = &actreq->request.socket_close;
    close->base.sig    = MT_SIGNATURE_REQUEST;
    close->base.type   = MtRequestSocketClose;
    close->base.size   = MT_REQUEST_SOCKET_CLOSE_SIZE;
    close->base.id     = actreq->id;
    close->base.sockfd = SockInst->remote_fd;

    rc = mwsocket_send_request( actreq );
    if ( rc ) goto ErrorExit;

    if ( !WaitForResponse ) goto ErrorExit; // no more work

    rc = wait_for_completion_timeout( &actreq->arrived, GENERAL_RESPONSE_TIMEOUT );
    if ( 0 == rc )
    {
        pr_warn( "Timed out while waiting for response\n" );
        rc = -ETIME;
    }
    else
    {
        pr_debug( "Successfully waited for close to complete\n" );
        rc = 0;
    }

    rc = rc ? rc : actreq->response.base.status;

ErrorExit:
    mwsocket_destroy_active_request( actreq );
    return rc;
}


static void
mwsocket_destroy_active_request( mwsocket_active_request_t * Request )
{
    if ( NULL == Request )
    {
        pr_debug( "Called with NULL\n" );
        return;
    }

    mwsocket_put_sockinst( Request->sockinst );

    // Dereference the file ????
    //put_filp( Request->sockinst->file );

    // Remove from list
    mutex_lock( &g_mwsocket_state.active_request_lock );
    list_del( &Request->list_all );
    mutex_unlock( &g_mwsocket_state.active_request_lock );

    pr_debug( "Destroyed active request %p id=%lx\n",
              Request, (unsigned long) Request->id );

    kmem_cache_free( g_mwsocket_state.active_request_cache, Request );
}


// @brief Gets a new active request struct from the cache and does basic init.
static int 
mwsocket_create_active_request( IN mwsocket_instance_t * SockInst,
                                OUT mwsocket_active_request_t ** ActReq )
{
   mwsocket_active_request_t * actreq = NULL;
   mt_id_t                      id = MWSOCKET_UNASSIGNED_ID;
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

    id = mwsocket_get_next_id();
    actreq->id = id;

    init_completion( &actreq->arrived );

    mwsocket_get_sockinst( SockInst );
    actreq->sockinst = SockInst;

    // Add the new active request to the global list
    mutex_lock( &g_mwsocket_state.active_request_lock );

    list_add( &actreq->list_all,
              &g_mwsocket_state.active_request_list );

    mutex_unlock( &g_mwsocket_state.active_request_lock );

    pr_debug( "Created active request %p id=%lx\n",
              actreq, (unsigned long)id );

    *ActReq = actreq;
    
ErrorExit:
    return rc;
}


static int
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


// @brief Delivers signal to current process
//
// @return Returns pending error, or 0 if none
static int
mwsocket_pending_error( mwsocket_instance_t * SockInst, bool ClearOldVals )
{
    MYASSERT( SockInst );
    int rc = 0;
    
    if ( SockInst->pending_sigpipe
        && !SockInst->delivered_sigpipe )
    {
        pr_info( "Delivering SIGPIPE to process %d (%s) for socket %d\n",
                 SockInst->proc->pid, SockInst->proc->comm,
                 SockInst->local_fd );
        send_sig( SIGPIPE, current, 0 );
        SockInst->delivered_sigpipe = true;
        rc = -EPIPE;
    }
    else if ( SockInst->pending_errno )
    {
        pr_info( "Delivering pending error %d to process %d (%s) for socket %d\n",
                 SockInst->pending_errno,
                 SockInst->proc->pid, SockInst->proc->comm,
                 SockInst->local_fd );
        rc = SockInst->pending_errno;
    }

    if ( ClearOldVals )
    {
        SockInst->pending_sigpipe = false;
        SockInst->pending_errno = 0;
    }
    
    return rc;
}


// @brief Prepares request and socket instance state prior to putting
// request on ring buffer.
static int
mwsocket_pre_process_request( mwsocket_active_request_t * ActRequest )
{
    int rc = 0;
    mwsocket_instance_t * acceptinst = NULL; // for usage with accept() only
    mt_request_generic_t * request = NULL;

    MYASSERT( ActRequest );
    MYASSERT( ActRequest->sockinst );

    // Reset sigpipe state - we can deliver one per read/write cycle
    ActRequest->sockinst->delivered_sigpipe = false;
    
    request = &ActRequest->request;

    // Put remote FD in the request
    request->base.sockfd = ActRequest->sockinst->remote_fd;

    // Will the user wait for the response to this request? If so, update state
    if ( MT_REQUEST_CALLER_WAITS( request ) )
    {
        // This write() will be followed immediately by a
        // read(). Indicate the ID that blocks the calling thread.
        ActRequest->sockinst->read_expected = true;
        ActRequest->sockinst->blockid = ActRequest->id;
        ActRequest->deliver_response = true;
    }
    
    switch( request->base.type )
    {
    case MtRequestSocketAccept:
        rc = mwsocket_create_sockinst( &acceptinst, true );
        if ( rc )
        {
            break;
        }
        ActRequest->sockinst->flags |= request->socket_accept.flags;
        ActRequest->sockinst->child_inst = acceptinst;
        break;
        //case MtRequestSocketClose:
        //ActRequest->sockinst->remote_close_requested = true;
        //break;
    default:
        break;
    }
    
    return rc;
}

static void
MWSOCKET_DEBUG_ATTRIB
mwsocket_post_process_response( mwsocket_active_request_t * ActRequest,
                                mt_response_generic_t     * Response )
{
    MYASSERT( ActRequest );
    MYASSERT( ActRequest->sockinst );
    MYASSERT( Response );

    int status = Response->base.status;

    // XXXX: In case the request failed and the requestor will not
    // process the response, we have to inform the user of the error
    // in some other way. In the worst case, we can close the FD
    // underneath the user.

    if ( Response->base.flags & _MT_RESPONSE_FLAG_REMOTE_CLOSED
        && !ActRequest->sockinst->pending_sigpipe )
    {
        // A SIGPIPE is delivered when writing to a closed socket.
        bool sigpipe = ( Response->base.type == MtResponseSocketSend );

        ActRequest->sockinst->pending_sigpipe = sigpipe;
        
        pr_info( "Remote side of socket %d closed. "
                 "Will%s deliver SIGPIPE to %d [%s]\n",
                 ActRequest->sockinst->local_fd,
                 (sigpipe ? "" : " not"),
                 ActRequest->sockinst->proc->pid,
                 ActRequest->sockinst->proc->comm );

    }

    // N.B. Do not use ActRequest->response; it might not be valid yet.
    if ( status < 0 )
    {
        ActRequest->sockinst->pending_errno = status;

        // A critical error is translated into EPIPE - meaning that we
        // assume the remote socket has closed. Is this correct??
        if ( Response->base.sockfd > 0
             && MtResponseSocketClose != Response->base.type )
        {
            if ( status == -MW_EPIPE || IS_CRITICAL_ERROR( status ) )
            {
                ActRequest->sockinst->pending_sigpipe = true;
            }
            else
            {
                ActRequest->sockinst->pending_errno = status;
            }
        }

        if ( MtResponseSocketAccept == Response->base.type )
        {
            // In mwsocket_write() we created the local accept socket even
            // though the creation had not yet completed. Destroy it here.
            MYASSERT( !"Socket accept() failed - verify me" );
            MYASSERT( ActRequest->sockinst->child_inst );
            // Race to destroy: on termination with socket in accept() state
            if ( ActRequest->sockinst->child_inst
                 && !ActRequest->sockinst->release_started )
            {
                fput( ActRequest->sockinst->child_inst->file );
            }
        }
        goto ErrorExit;
    }

    // Success case
    switch( Response->base.type )
    {
    case MtResponseSocketCreate:
        pr_debug( "Associating local fd %d to remote fd %x\n",
                  ActRequest->sockinst->local_fd, Response->base.sockfd );
        ActRequest->sockinst->remote_fd = Response->base.sockfd;
        break;

    case MtResponseSocketAccept:
        // The accept() call was sent with a new local FD already in
        // place. Put its FD in the response.
        MYASSERT( ActRequest->sockinst->child_inst );

        ActRequest->sockinst->child_inst->remote_fd = Response->base.sockfd;

        pr_debug( "Associating local fd %d to remote fd %x\n",
                  ActRequest->sockinst->child_inst->local_fd,
                  Response->base.sockfd );

        // Inform the caller of the new FD
        Response->base.sockfd
            = Response->base.status
            = ActRequest->sockinst->child_inst->local_fd;

        // We're done with the accept(). The originating instance can forget about it.
        ActRequest->sockinst->child_inst = NULL;
        break;
/*
    case MtResponseSocketRecv:
    case MtResponseSocketRecvFrom:
        // Readable data was indicated. Was anything read? If not,
        // the socket closed.
        if ( ActRequest->sockinst->poll_events & MW_POLLIN
             && 0 == Response->base.status )
        {
            DEBUG_BREAK();
            pr_info( "Detected remote close condition on socket %x/%d\n",
                     ActRequest->sockinst->remote_fd,
                     ActRequest->sockinst->local_fd );
            Response->base.status = -EPIPE;
            ActRequest->sockinst->pending_errno = -EPIPE;
        }
        break;
    case MtResponseSocketSend:
        // Socket was writable but last write() didn't write
        // anything. Therefore it has closed.
        if ( ActRequest->sockinst->poll_events & MW_POLLOUT
             && 0 == Response->socket_send.sent )
        {
            DEBUG_BREAK();
            pr_info( "Detected remote close condition on socket %x/%d\n",
                     ActRequest->sockinst->remote_fd,
                     ActRequest->sockinst->local_fd );
            Response->base.status = -EPIPE;
            ActRequest->sockinst->pending_errno = -EPIPE;
        }
        break;
        */
    default:
        break;
    }

ErrorExit:
    return;
}


static int
MWSOCKET_DEBUG_ATTRIB
mwsocket_send_request( IN mwsocket_active_request_t * ActiveReq )
{
    int rc = 0;
    void * dest = NULL;
    mt_request_generic_t * request = &ActiveReq->request;

    // Fill in the basics so the callers don't have to
    request->base.sig = MT_SIGNATURE_REQUEST;
    request->base.id  = ActiveReq->id;
    request->base.sockfd = ActiveReq->sockinst->remote_fd;

    pr_debug( "Sending request %lx fd %lx/%d type %x\n",
              (unsigned long)request->base.id,
              (unsigned long)request->base.sockfd,
              ActiveReq->sockinst->local_fd,
              request->base.type );

    // Hold this for duration of the operation. 
    mutex_lock( &g_mwsocket_state.request_lock );

    if ( !MT_IS_REQUEST( request ) )
    {
        MYASSERT( !"Invalid request given\n" );
        rc = -EINVAL;
        goto ErrorExit;
    }
    
    if ( !g_mwsocket_state.is_ring_ready )
    {
        MYASSERT( !"Received request too early - ring not ready.\n" );
        rc = -EIO;
        goto ErrorExit;
    }

    if ( RING_FULL( &g_mwsocket_state.front_ring ) )
    {
        MYASSERT( !"Front ring is full" );
        rc = -EAGAIN;
        goto ErrorExit;
    }

    dest = RING_GET_REQUEST( &g_mwsocket_state.front_ring,
                             g_mwsocket_state.front_ring.req_prod_pvt );
    if ( !dest ) 
    {
        pr_err("destination buffer is NULL\n");
        rc = -EIO;
        goto ErrorExit;
    }

    // Copy the request, update shared memory and notify remote side
    memcpy( dest, request, request->base.size );
    ++g_mwsocket_state.front_ring.req_prod_pvt;
    RING_PUSH_REQUESTS( &g_mwsocket_state.front_ring );

#if MW_DO_SEND_RING_EVENTS
    mw_xen_send_event();
#endif
    
ErrorExit:
    mutex_unlock( &g_mwsocket_state.request_lock );

    return rc;
}


// @brief Sends the message based on the socket instance.
//
// @returns status from sending, or status from response if
// AwaitResponse is true.
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

    memcpy( &actreq->request, Request, Request->base.size );

    rc = mwsocket_send_request( actreq );
    if ( rc ) goto ErrorExit;

    if ( !AwaitResponse ) goto ErrorExit;

    wait_for_completion_interruptible( &actreq->arrived );
    pr_debug( "Response arrived: %lx\n", (unsigned long)actreq->id );

    remoterc = -actreq->response.base.status;

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
    int rc = 0;
    bool available = false;
    mwsocket_active_request_t * actreq = NULL;
    mt_response_generic_t * response = NULL;

#define RING_CONSUME_RESPONSE()                                         \
    ++g_mwsocket_state.front_ring.rsp_cons;                             \
    RING_FINAL_CHECK_FOR_RESPONSES( &g_mwsocket_state.front_ring, available )

    // Wait for the ring buffer's initialization to complete
    rc = wait_for_completion_interruptible( &g_mwsocket_state.ring_ready );
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

    pr_info( "Proc %d awaiting responses from ring buffer (0x%x slots).\n",
             current->pid, RING_SIZE( &g_mwsocket_state.front_ring ) );

    // Consume responses until the module is unloaded. When it is
    // unloaded, consume whatever is still on the ring, then
    // quit. Only leave this loop on a requested exit or a fatal
    // error.
    while( true )
    {
        do
        {
            available =
                RING_HAS_UNCONSUMED_RESPONSES( &g_mwsocket_state.front_ring );
            if ( !available )
            {
                if ( g_mwsocket_state.pending_exit )
                {
                    // Nothing available and waiting on exit
                    goto ErrorExit;
                }

                if ( down_interruptible( &g_mwsocket_state.event_channel_sem ) )
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
            RING_GET_RESPONSE( &g_mwsocket_state.front_ring,
                               g_mwsocket_state.front_ring.rsp_cons);

        pr_debug( "Response ID %lx size %x type %x status %d on ring at idx %x\n",
                  (unsigned long)response->base.id,
                  response->base.size, response->base.type,
                  response->base.status,
                  g_mwsocket_state.front_ring.rsp_cons );

        if ( !MT_IS_RESPONSE( response ) )
        {
            // Fatal: The ring is corrupted.
            pr_crit( "Received data that is not a response at idx %d\n",
                     g_mwsocket_state.front_ring.rsp_cons );
            rc = -EIO;
            goto ErrorExit;
        }

        // Hereafter: Advance index as soon as we're done with the
        // item in the ring buffer.
        rc = mwsocket_find_active_request_by_id( &actreq, response->base.id );
        if ( rc )
        {
            //MYASSERT( !"Read unrecognized response" );
            pr_warn( "Couldn't find active request with ID %lx\n",
                     (unsigned long) response->base.id );
            RING_CONSUME_RESPONSE();
            continue; // move on
        }

        // Post process the response. Then copy the data and signal
        // the completion variable, if requested. Otherwise destroy.
        // N.B. The response is still only in the ring.
        mwsocket_post_process_response( actreq, response );

        if ( actreq->deliver_response )
        {
            memcpy( &actreq->response, response, response->base.size );
            // Indicate that more blocking IO can occur on this socket
            //actreq->sockinst->blockid = MWSOCKET_UNASSIGNED_ID;
            complete_all( &actreq->arrived );
        }

        // We're done with this slot of the ring
        RING_CONSUME_RESPONSE();
        
        // If nobody will consume this active request's response,
        // destroy it
        if ( !actreq->deliver_response )
        {
            mwsocket_destroy_active_request( actreq );
        }
    } // while( true )

ErrorExit:
    complete( &g_mwsocket_state.response_reader_done );
    return rc;
}


static int
MWSOCKET_DEBUG_ATTRIB
mwsocket_poll_handle_notifications( IN mwsocket_instance_t * SockInst )
{
    int rc = 0;
    mwsocket_active_request_t      * actreq = NULL;
    mt_request_pollset_query_t   * request = NULL;
    mt_response_pollset_query_t * response = NULL;

    rc = mwsocket_create_active_request( SockInst, &actreq );
    if ( rc ) goto ErrorExit;

    request = &actreq->request.pollset_query;
    request->base.sig  = MT_SIGNATURE_REQUEST;
    request->base.id   = actreq->id;    
    request->base.type = MtRequestPollsetQuery;
    request->base.size = MT_REQUEST_POLLSET_QUERY_SIZE;

    // Sent the request. We will wait for response.
    actreq->deliver_response = true;

    rc = mwsocket_send_request( actreq );
    if ( rc ) goto ErrorExit;

    // Wait
    rc = wait_for_completion_timeout( &actreq->arrived,
                                      POLL_MONITOR_RESPONSE_TIMEOUT );
    if ( 0 == rc )
    {
        // The response did not arrive, so we cannot process it
        pr_warn( "Timed out while waiting for response\n" );
        rc = -ETIME;
        goto ErrorExit;
    }

    rc = 0;
    response = &actreq->response.pollset_query;

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

    pr_debug( "Processing %d FDs with IO events\n", response->count );

    // This lock protects socket instance list access and is used in
    // the poll() callback for accessing the instance itself.
    mutex_lock( &g_mwsocket_state.sockinst_lock );

    // XXXX: Must we track whether events were consumed?
    mwsocket_instance_t * currsi = NULL;
    list_for_each_entry( currsi, &g_mwsocket_state.sockinst_list, list_all )
    {
        // if this instance is referenced in the response, set its
        // events; otherwise clear them
        currsi->poll_events = 0;    

        for ( int i = 0; i < response->count; ++i )
        {
            // Find the associated sockinst, matching up by (remote) mwsocket
        
            if ( currsi->remote_fd != response->items[i].sockfd ) continue;

            // Transfer response's events to sockinst's, notify poll()
            // N.B. MW_POLL* == linux values
            currsi->poll_events = response->items[i].events;

            pr_debug( "Indicating IO events %lx on socket %d in process %s (wq %p)\n",
                      currsi->poll_events, currsi->local_fd, currsi->proc->comm,
                      &g_mwsocket_state.waitq );

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


// @brief Thread function that monitors for local changes in pollset
// request and for remote IO events.
static int
MWSOCKET_DEBUG_ATTRIB
mwsocket_poll_monitor( void * Arg )
{
    int rc = 0;
    mwsocket_instance_t * sockinst = NULL;

    // Create a socket instance without a backing file 
    rc = mwsocket_create_sockinst( &sockinst, false );
    if ( rc ) goto ErrorExit;

    // Wait for the ring buffer's initialization to complete
    rc = wait_for_completion_interruptible( &g_mwsocket_state.ring_ready );
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
        set_current_state( TASK_INTERRUPTIBLE );
        schedule_timeout( POLL_MONITOR_QUERY_INTERVAL );
        
        // If there are any open mwsockets at all, complete a poll
        // query exchange. The socket instance from this function
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
    complete( &g_mwsocket_state.poll_monitor_done );
    return rc;
}


// @brief Processes an mwsocket creation request. Reachable via IOCTL.
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
    if ( !g_mwsocket_state.is_ring_ready )
    {
        MYASSERT( !"Ring has not been initialized\n" );
        rc = -ENODEV;
        goto ErrorExit;
    }

    // We're creating 2 things here:
    //  (1) a pseudo-socket structure in the local OS, and
    //  (2) the new socket on the client,

    // (1) Local tasks first
    rc = mwsocket_create_sockinst( &sockinst, true );
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


// @brief Returns whether the given file descriptor is backed by an MW socket.
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
    request = &actreq->request.socket_attrib;
    request->base.type = MtRequestSocketAttrib;
    request->base.size = MT_REQUEST_SOCKET_ATTRIB_SIZE;

    request->modify = SetAttribs->modify;
    request->attrib = SetAttribs->attrib;
    request->value  = SetAttribs->value;

    rc = mwsocket_send_request( actreq );
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
    
    response = &actreq->response.socket_attrib;
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

    // Is there a pending error on this socket? If so, return it.
    rc = mwsocket_pending_error( sockinst, true );
    if ( rc ) goto ErrorExit;
    
    if ( wait_for_completion_interruptible( &actreq->arrived ) )
    {
        pr_warn( "read() was interrupted\n" );
        // Keep the request alive, although user will not consume it.
        actreq->deliver_response = false;
        rc = -EINTR;
        goto ErrorExit;
    }

    // Data is ready for us. Validate and copy to user.
    response = (mt_response_generic_t *) &actreq->response;
    if ( Len < response->base.size )
    {
        MYASSERT( !"User buffer is too small for response" );
        rc = -EINVAL;
        goto ErrorExit;
    }

    // Did the remote side close unexpectedly? Deliver SIGPIPE;
    // otherwise don't do anything else.
    mwsocket_pending_error( sockinst, false );

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
    if ( -EINTR != rc )
    {
        // The "active" request is now dead
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
    //mwsocket_instance_t * acceptinst = NULL; // for usage with accept() only
    mt_request_base_t base;
    bool sent = false;
    // Do not expect a read() after this if we return -EAGAIN
    
    pr_debug( "Processing write()\n" );

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

    // Is there a pending error on this socket? If so, deliver it.
    rc = mwsocket_pending_error( sockinst, true );
    if ( rc ) goto ErrorExit;
    
    // Create a new active request
    rc = mwsocket_create_active_request( sockinst, &actreq );
    if ( rc ) goto ErrorExit;

    // Populate the request
    request = &actreq->request;

    rc = copy_from_user( request, Bytes, Len );
    if ( rc )
    {
        MYASSERT( !"copy_from_user failed." );
        rc = -EFAULT;
        goto ErrorExit;
    }

    if ( request->base.size < Len )
    {
        MYASSERT( !"Request is longer than provided buffer." );
        rc = -EINVAL;
        goto ErrorExit;
    }

    // Now we have the full request. Populate it further as needed. 
    rc = mwsocket_pre_process_request( actreq );
    if ( rc ) goto ErrorExit;

    // Write to the ring
    rc = mwsocket_send_request( actreq );
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
        mwsocket_put_sockinst( sockinst->child_inst );
    }

    return rc;
}


// @brief IOCTL callback for ioctls against mwsocket files themselves
// (vs. the mwcomms device)
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
        pr_err( "Called IOCTL on an invalid file\n" );
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
    pr_debug( "Processing poll(), fd %d wq %p\n",
              sockinst->local_fd,
              &g_mwsocket_state.waitq );
    
    poll_wait( File, &g_mwsocket_state.waitq, PollTbl );

    // Lock used by poll monitor during socket instance update
    mutex_lock( &g_mwsocket_state.sockinst_lock );
    //mutex_lock( &g_mwsocket_state.poll_lock );

    events = sockinst->poll_events;
    sockinst->poll_events = 0;

    mutex_unlock( &g_mwsocket_state.sockinst_lock );
    //mutex_unlock( &g_mwsocket_state.poll_lock );

    pr_debug( "Returning events %lx, fd %d wq %p\n",
              events, sockinst->local_fd, &g_mwsocket_state.waitq );
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

    sockinst->release_started = true;

    pr_info( "Processing release() on fd=%d\n",
             sockinst->local_fd );

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
    init_completion( &g_mwsocket_state.ring_ready );

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

    g_mwsocket_state.ring = *SharedMem;
    g_mwsocket_state.sring = (struct mwevent_sring *) SharedMem->ptr;

ErrorExit:
    return rc;
}

void
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
    complete_all( &g_mwsocket_state.ring_ready );
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
                (unsigned long)currar->id, currar->request.base.type );
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
