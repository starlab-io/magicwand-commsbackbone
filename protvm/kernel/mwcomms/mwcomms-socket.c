/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

//
// Implementation of MW sockets. An MW socket is backed by a
// registered file object and file descriptor with it's own file
// operations.
//

#include "mwcomms-common.h"
#include "mwcomms-socket.h"
#include "mwcomms-xen-iface.h"

#include <linux/slab.h>

// Only one of these should be needed
#include <linux/net.h>

#include <linux/fs.h>
#include <linux/mount.h>

#include <linux/file.h>
#include <linux/poll.h>

#include <linux/kallsyms.h>

#include <linux/kthread.h>
#include <message_types.h>
#include <xen_keystore_defs.h>

// Defines:
// mwevent_sring_t
// mwevent_front_ring_t
// mwevent_back_ring_t
DEFINE_RING_TYPES( mwevent, mt_request_generic_t, mt_response_generic_t );

// Is the remote side checking for Xen events?
#define MW_DO_SEND_RING_EVENTS 0

#define MWSOCKET_FS_MAGIC  0x4d77536f // MwSo

#define MWSOCKET_DEBUG_ATTRIB  __attribute__((optimize("O0")))


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

static unsigned int
mwsocket_poll( struct file * File,
               struct poll_table_struct * PollTbl );

static int
mwsocket_release( struct inode *Inode,
                  struct file * File );


static struct file_operations
mwsocket_fops =
{
    // A close() request will decrement this module's usage count.
    // Since we don't have a traditional open() we have to compensate
    // for this behavior, which we do in release.
    .owner   = THIS_MODULE,
    .read    = mwsocket_read,
    .write   = mwsocket_write,
    .poll    = mwsocket_poll,
    .release = mwsocket_release
};


/******************************************************************************
 * Types and globals
 ******************************************************************************/
//struct _mwsocket_thread_response;

//
// For tracking per-mwsocket data. An mwsocket is built on top of the
// mwsocket filesystem. A good example of this model is found in the
// Linux kernel: fs/pipe.c
//
typedef struct _mwsocket_instance
{
    struct file  * file;
    struct inode * inode;

    struct task_struct * proc;

    // Sockets: local FD and remote
    int             local_fd;
    mw_socket_fd_t  remote_fd;

    // Non-zero indicates error condition
    int pending_errno;
    
    // How many active requests are using this mwsocket?
    atomic_t            user_ct;

    // The (singular) ID of the blocking request.
    mt_id_t             blockid;

    bool                read_expected;

    // Valid only while an accept() against this FD is in-flight
    struct  _mwsocket_instance * accept_inst;
    
    struct list_head list_all;
} mwsocket_instance_t;


//
// For tracking state on requests whose responses have not yet arrived
//
typedef struct _mwsocket_active_request
{
    mt_id_t id;

    // Will the requestor wait
    bool   requestor_consumes_response;

    // Track whether it was delivered?
    
    // Signaled when the response arrives and is available in the
    // response field. Only signaled if requestor_consumes_response is true.
    struct completion arrived;

    // These are large, and we only need one at a time.
    union
    {
        mt_request_generic_t request;        

        // The response is copied here, if someone is waiting on
        // it. Otherwise, minimal processing is done on it.
        mt_response_generic_t response;
    };
    
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

    // OS data
    struct kmem_cache * sockinst_cache;
    struct list_head    sockinst_list;
    struct mutex        sockinst_lock;

    // Filesystem data
    struct vfsmount * fs_mount;
    bool   fs_registered;

    // Kernel thread info: response consumer
    struct task_struct * response_reader_thread;
    struct completion    response_reader_done;

    // Kernel thread info: poll notifier
    struct task_struct * poll_notifier_thread;
    struct completion    poll_notifier_done;

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

static mwsocket_active_request_t *
mwsocket_create_active_request( mwsocket_instance_t * SockInst );

static int
mwsocket_send_request( IN mwsocket_active_request_t * ActiveReq );

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
    complete( &g_mwsocket_state.ring_ready );
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


// @brief Releases SockInst that was successfully built
static void
mwsocket_destroy_sockinst( mwsocket_instance_t * SockInst )
{
    if ( NULL == SockInst )
    {
        return;
    }

    MYASSERT( 0 == atomic_read( &SockInst->user_ct ) );

    mutex_lock( &g_mwsocket_state.sockinst_lock );
    list_del( &SockInst->list_all );
    mutex_unlock( &g_mwsocket_state.sockinst_lock );

    pr_debug( "Destroyed socket instance %d\n", SockInst->local_fd );
    
    kmem_cache_free( g_mwsocket_state.sockinst_cache, SockInst );
}


static int
mwsocket_create_sockinst( OUT mwsocket_instance_t ** SockInst )
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
    
    sockinst->local_fd = -1;
    sockinst->proc = current;

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

    file->f_flags = O_RDWR;

    fd = get_unused_fd_flags( flags );
    if ( fd < 0 )
    {
        MYASSERT( !"get_unused_fd_flags() failed\n" );
        rc = -EMFILE;
        goto ErrorExit;
    }

    // Success
    sockinst->local_fd = fd;

    pr_debug( "Installing mwsocket FD %d\n", fd );
    fd_install( fd, sockinst->file );

    *SockInst = sockinst;

    mutex_lock( &g_mwsocket_state.sockinst_lock );
    list_add( &sockinst->list_all, &g_mwsocket_state.sockinst_list );
    mutex_unlock( &g_mwsocket_state.sockinst_lock );

    pr_debug( "Created socket instance %d\n", fd );
    
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
        if ( sockinst ) kmem_cache_free( g_mwsocket_state.sockinst_cache, sockinst );
    }

    return rc;
}

// @brief Close the remote socket.
//
// @return 0 on success, or error, which could be either local or remote
static int
mwsocket_close_remote( IN mwsocket_instance_t * SockInst,
                       IN bool WaitForResponse )
{
    int rc = 0;
    mwsocket_active_request_t * actreq = NULL;
    mt_request_socket_close_t * close = NULL;

    actreq = mwsocket_create_active_request( SockInst );
    if ( NULL == actreq )
    {
        rc = -ENOMEM;
        goto ErrorExit;
    }

    actreq->requestor_consumes_response = WaitForResponse;
    close = &actreq->request.socket_close;
    close->base.sig    = MT_SIGNATURE_REQUEST;
    close->base.type   = MtRequestSocketClose;
    close->base.size   = MT_REQUEST_SOCKET_CLOSE_SIZE;
    close->base.id     = actreq->id;
    close->base.sockfd = SockInst->remote_fd;

    rc = mwsocket_send_request( actreq );
    if ( rc )
    {
        goto ErrorExit;
    }

    if ( !WaitForResponse )
    {
        goto ErrorExit; // done
    }

    wait_for_completion_interruptible( &actreq->arrived );
    rc = actreq->response.base.status;

ErrorExit:
    mwsocket_destroy_active_request( actreq );
    return rc;
}


static void
mwsocket_destroy_active_request( mwsocket_active_request_t * Request )
{
    if ( NULL == Request )
    {
        return;
    }

    atomic_dec( &Request->sockinst->user_ct );

    // Dereference the file ????
    //put_filp( Request->sockinst->file );

    // Remove from list
    mutex_lock( &g_mwsocket_state.active_request_lock );
    list_del( &Request->list_all );
    mutex_unlock( &g_mwsocket_state.active_request_lock );

    pr_debug( "Destroyed active request %lx\n", (unsigned long) Request->id );

    kmem_cache_free( g_mwsocket_state.active_request_cache, Request );
}


// @brief Gets a new active request struct from the cache and does basic init.
static mwsocket_active_request_t *
mwsocket_create_active_request( mwsocket_instance_t * SockInst )
{
   mwsocket_active_request_t * actreq = NULL;
   mt_id_t                      id = MWSOCKET_UNASSIGNED_ID;
   
   MYASSERT( SockInst );

   actreq = (mwsocket_active_request_t *)
        kmem_cache_alloc( g_mwsocket_state.active_request_cache,
                          GFP_KERNEL | __GFP_ZERO );
    if ( NULL == actreq )
    {
        MYASSERT( !"kmem_cache_alloc failed\n" );
        goto ErrorExit;
    }

    // Success
    id = mwsocket_get_next_id();
    
    actreq->id = id;

    init_completion( &actreq->arrived );

    atomic_inc( &SockInst->user_ct );
    actreq->sockinst = SockInst;

    // Add the new active request to the global list
    mutex_lock( &g_mwsocket_state.active_request_lock );

    list_add( &actreq->list_all,
              &g_mwsocket_state.active_request_list );

    mutex_unlock( &g_mwsocket_state.active_request_lock );

    pr_debug( "Created active request %lx\n", (unsigned long)id );
ErrorExit:
    return actreq;
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


static int
MWSOCKET_DEBUG_ATTRIB
mwsocket_post_process_response( mwsocket_active_request_t * ActRequest,
                                mt_response_generic_t * Response )
{
    MYASSERT( ActRequest );
    MYASSERT( ActRequest->sockinst );
    MYASSERT( Response );

    bool failed = Response->base.status < 0;
    //mwsocket_instance_t * sockinst = NULL;
    int rc = 0;
    
    // XXXX: In case the request failed and the requestor will not
    // process the response, we have to inform the user of the error
    // in some other way. In the worst case, we can close the FD
    // underneath the user.

    // Do not use ActRequest->response; it might not be valid yet.
    
    if ( failed && !ActRequest->requestor_consumes_response )
    {
        // There is an error and the caller doesn't consume the response.
        MYASSERT( !"Failure not consumed by requestor" );
        pr_err( "Request %lx failed. Requestor will not consume the response.\n",
                (unsigned long) ActRequest->id );
        // Ignore errors here. The caller will recognize them on the
        // next synchronous call. An alternative is SIGPIPE if the
        // process expects it.

        ActRequest->sockinst->pending_errno = Response->base.status;
        goto ErrorExit;
    }


    switch( Response->base.type )
    {
        // The accept() call was sent with a new local FD already in
        // place. Map it here, just like for socket().

    case MtResponseSocketCreate:
        pr_debug( "Associating local fd %d to remote fd %x\n",
                  ActRequest->sockinst->local_fd, Response->base.sockfd );
        ActRequest->sockinst->remote_fd = Response->base.sockfd;
        break;
        
    case MtResponseSocketAccept:
        MYASSERT( ActRequest->sockinst->accept_inst );

        ActRequest->sockinst->accept_inst->remote_fd = Response->base.sockfd;

        pr_debug( "Associating local fd %d to remote fd %x\n",
                  ActRequest->sockinst->accept_inst->local_fd,
                  Response->base.sockfd );

        // Inform the caller of the new DF
        Response->base.sockfd
            = Response->base.status
            = ActRequest->sockinst->accept_inst->local_fd;

        // We're done with the accept()
        ActRequest->sockinst->accept_inst = NULL;
        break;
    default:
        break;
    }

ErrorExit:
    return rc;
}


// @brief Calculates return state based on blocked status and request type.
static int
mwsocket_derive_return( IN  mwsocket_active_request_t * ActReq,
                        OUT int          * ReturnVal,
                        OUT int          * ErrNo )
{
    int rc = 0;
    mt_request_generic_t * request = NULL;

    MYASSERT( ActReq );
    MYASSERT( ReturnVal );
    MYASSERT( ErrNo   );
    MYASSERT( !ActReq->requestor_consumes_response );
    
    // The requestor has stated that it will not block on the
    // response's arrival. 
    request = &ActReq->request;
    //MYASSERT( !MT_BLOCKS( request->base.type ) );

    switch( request->base.type )
    {
    case MtRequestSocketCreate:  // blocks
    case MtRequestSocketClose:   // blocks
    case MtRequestSocketGetName: // blocks
    case MtRequestSocketGetPeer: // blocks
        *ReturnVal = 0;
        *ErrNo = 0;
        break;
    case MtRequestSocketConnect:
        *ReturnVal = -1;
        *ErrNo     = -EINPROGRESS;
        break;
    case MtRequestSocketRead:
    case MtRequestSocketSend:
    case MtRequestSocketBind:
    case MtRequestSocketListen:
    case MtRequestSocketAccept:
    case MtRequestSocketRecv:
    case MtRequestSocketRecvFrom:
        *ReturnVal = -1;
        *ErrNo     = -EAGAIN;
        break;
    case MtRequestPollsetAdd:
        break;
    case MtRequestPollsetRemove:
        break;
    case MtRequestPollsetQuery:
        break;
    default:
        MYASSERT( !"Unknown request type" );
        rc = -EINVAL;
        break;
    }
/*    

    request = &actreq->request;
    if ( MT_BLOCKS( request->base.type ) )
    {
        block = true;
    }
    else if ( MT_NOBLOCK( request->base.type ) )
    {
        block = false;
    }
    else
    {
        block = sockinst->file->f_flag & O_NONBLOCK;
    }
*/
    return rc;    
}


static int
mwsocket_send_request( IN mwsocket_active_request_t * ActiveReq )
{
    int rc = 0;
    void * dest = NULL;
    mt_request_generic_t * request = &ActiveReq->request;

    pr_debug( "Sending request %lx\n", (unsigned long)ActiveReq->id );

    // Hold this for duration of the operation. 
    mutex_lock( &g_mwsocket_state.request_lock );

    if ( ActiveReq->id != request->base.id
         || !MT_IS_REQUEST( request ) )
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
        pr_alert("Front ring is full. Is remote side up?\n");
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
    DEBUG_BREAK();
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
            MYASSERT( !"Read unrecognized response" );
            RING_CONSUME_RESPONSE();
            continue; // move on
        }

        // Post process the response. Then copy the data and signal
        // the completion variable, if requested. Otherwise destroy.
        // N.B. The response is still only in the ring.
        rc = mwsocket_post_process_response( actreq, response );
        if ( rc )
        {
            // Fail the response and continue processing
            MYASSERT( rc < 0 );
            response->base.status = rc;
        }

        if ( actreq->requestor_consumes_response )
        {
            memcpy( &actreq->response, response, response->base.size );
            // Indicate that more blocking IO can occur on this socket
            actreq->sockinst->blockid = MWSOCKET_UNASSIGNED_ID;
            complete_all( &actreq->arrived );
        }

        // We're done with this slot of the ring
        RING_CONSUME_RESPONSE();

        // If nobody will consume this active request's response, destroy it
        if ( !actreq->requestor_consumes_response )
        {
            mwsocket_destroy_active_request( actreq );
        }
    } // while( true )

ErrorExit:
    complete( &g_mwsocket_state.response_reader_done );
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
    mwsocket_active_request_t     * actreq = NULL;
    mt_request_socket_create_t * create = NULL;
    mwsocket_instance_t          * sockinst = NULL;

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
    rc = mwsocket_create_sockinst( &sockinst );
    if ( rc )
    {
        goto ErrorExit;
    }
    
    // (2) Register the new socket on the client
    actreq = mwsocket_create_active_request( sockinst );
    if ( NULL == actreq )
    {
        rc = -ENOMEM;
        goto ErrorExit;
    }

    create = &actreq->request.socket_create;
    create->base.sig      = MT_SIGNATURE_REQUEST;
    create->base.type     = MtRequestSocketCreate;
    create->base.size     = MT_REQUEST_SOCKET_CREATE_SIZE;
    create->base.id       = actreq->id;
    create->base.sockfd   = MT_INVALID_SOCKET_FD;
    create->sock_fam      = Domain; // family == domain
    create->sock_type     = Type;
    create->sock_protocol = Protocol;
    // Will we wait for response?
    actreq->requestor_consumes_response = true;

    rc = mwsocket_send_request( actreq );
    if ( rc )
    {
        // The send failed. Therefore the consumer will never see the response.
        //
        goto ErrorExit;
    }

    // Wait for response
    wait_for_completion_interruptible( &actreq->arrived );
    pr_debug( "Response %lx has arrived\n", (unsigned long)actreq->id );

ErrorExit:

    mwsocket_destroy_active_request( actreq );
    if ( rc )
    {
        // Failure: release socket instance
        mwsocket_destroy_sockinst( sockinst );
    }
    else
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

int
mwsocket_modify_pollset( IN int  MwFd,
                         IN bool AddFd )
{
    int rc = 0;

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
/*
    if ( MWSOCKET_UNASSIGNED_ID == sockinst->blockid )
    {
        MYASSERT( !"There is no outstanding blocking IO on this socket.\n" );
        rc = -EPERM;
        goto ErrorExit;
    }
*/  
    // Now find the outstanding request/response
    rc = mwsocket_find_active_request_by_id( &actreq, sockinst->blockid );
    if ( rc )
    {
        MYASSERT( !"Couldn't find outstanding request with ID." );
        goto ErrorExit;
    }

    if ( !actreq->requestor_consumes_response )
    {
        MYASSERT( !"Request was marked as non-blocking. No data is available." );
        rc = -EINVAL;
        goto ErrorExit;
    }
    
    if ( wait_for_completion_interruptible( &actreq->arrived ) )
    {
        MYASSERT( !"read() was interrupted\n" );
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
    // The "active" request is now dead
    mwsocket_destroy_active_request( actreq );
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
    mwsocket_instance_t * acceptinst = NULL; // for usage with accept() only
    mt_request_base_t base;
    int retval = 0;
    int lerrno = 0;
    bool sent = false;
    
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

    // If accept, create new sock instance (and FD) now, in the
    // caller's context
    if ( MtRequestSocketAccept == base.type )
    {
        rc = mwsocket_create_sockinst( &acceptinst );
        if ( rc )    goto ErrorExit;
        sockinst->accept_inst = acceptinst;
    }
    /*
    if ( MWSOCKET_UNASSIGNED_ID != sockinst->blockid )
    {
        MYASSERT( !"There's outstanding blocking IO on this socket. "
                  "Writing not permitted\n" );
        rc = -EPERM;
        goto ErrorExit;
    }
    */
    // Is there a pending error on this socket? If so, return it.
    if ( 0 != sockinst->pending_errno )
    {
        MYASSERT( !"There's a pending error on this socket. Delivering.\n" );
        rc = sockinst->pending_errno;
        goto ErrorExit;
    }
    
    // Create a new active request
    actreq = mwsocket_create_active_request( sockinst );
    if ( NULL == actreq )
    {
        rc = -ENOMEM;
        goto ErrorExit;
    }
    
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
    request->base.sockfd = actreq->sockinst->remote_fd; 
    request->base.id = actreq->id;

    if ( request->base.pvm_blocked_on_response )
    {
        // This write() will be followed by a read(). Indicate the ID
        // that blocks the calling thread.
        sockinst->read_expected = true;
        actreq->requestor_consumes_response = true;
        actreq->sockinst->blockid = actreq->id;         // ????
    }

    // Write to the ring
    rc = mwsocket_send_request( actreq );
    if ( rc )
    {
        goto ErrorExit;
    }

    sent = true;

    // The caller is writing this request and will not read the response.
    if ( !request->base.pvm_blocked_on_response )
    {
        // Handle return from non-blocking IO
        //rc = mwsocket_derive_return( actreq, &retval, &lerrno );
        //mwsocket_destroy_active_request( actreq );
        // Fall-through with rc
    }

ErrorExit:
    if ( rc && !sent )
    {
        mwsocket_destroy_active_request( actreq );
        mwsocket_destroy_sockinst( acceptinst );
    }
    else
    {
        rc = lerrno;
    }
    return rc;
}

static unsigned int
mwsocket_poll( struct file * File,
               struct poll_table_struct * PollTbl )
{
    ssize_t rc = 0;
    pr_debug( "Processing poll()\n" );
    
//ErrorExit:
    return rc;
}


static int
mwsocket_release( struct inode *Inode,
                  struct file * File )
{
    int rc = 0;
    mwsocket_instance_t * sockinst = NULL;

    pr_debug( "Processing release()\n" );

    // Do not incur a decrement against our module for this close(),
    // since the file we're closing was not opened via an open() call.
    __module_get( THIS_MODULE );

    rc = mwsocket_find_sockinst( &sockinst, File );
    if ( rc )
    {
        MYASSERT( !"Failed to find associated socket instance" );
        rc = -EBADFD;
        goto ErrorExit;
    }

    rc = mwsocket_close_remote( sockinst, true );
    // fall-through
    
ErrorExit:
    mwsocket_destroy_sockinst( sockinst );
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

    INIT_LIST_HEAD( &g_mwsocket_state.active_request_list );
    INIT_LIST_HEAD( &g_mwsocket_state.sockinst_list );

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
    if ( rc )
    {
        goto ErrorExit;
    }

    g_mwsocket_state.active_request_cache =
        kmem_cache_create( "Active Requests",
                           sizeof( mwsocket_active_request_t ),
                           0, 0, NULL );

    if ( NULL == g_mwsocket_state.active_request_cache )
    {
        MYASSERT( !"kmem_cache_create() failed\n" );
        rc = -ENOMEM;
        goto ErrorExit;
    }

    g_mwsocket_state.sockinst_cache =
        kmem_cache_create( "Socket Instances",
                           sizeof( mt_request_generic_t ),
                           0, 0, NULL );

    if ( NULL == g_mwsocket_state.sockinst_cache )
    {
        MYASSERT( !"kmem_cache_create() failed\n" );
        rc = -ENOMEM;
        goto ErrorExit;
    }

    sema_init( &g_mwsocket_state.event_channel_sem, 0 );
    init_completion( &g_mwsocket_state.response_reader_done );
    init_completion( &g_mwsocket_state.ring_ready );

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
    
    g_mwsocket_state.ring = *SharedMem;
    g_mwsocket_state.sring = (struct mwevent_sring *) SharedMem->ptr;

ErrorExit:
    return rc;
}

void
mwsocket_fini( void )
{
    if ( NULL != g_mwsocket_state.active_request_cache )
    {
        kmem_cache_destroy( g_mwsocket_state.active_request_cache );
    }

    if ( NULL != g_mwsocket_state.fs_mount )
    {
        kern_unmount( g_mwsocket_state.fs_mount );
    }

    if ( g_mwsocket_state.fs_registered )
    {
        unregister_filesystem( &mwsocket_fs_type );
    }

    // Destroy worker thread: kick it. It might be waiting for the
    // ring to become ready, or it might be waiting for responses to
    // arrive on the ring. Wait for it to complete so shared resources
    // can be destroyed.

    g_mwsocket_state.pending_exit = true;
    complete_all( &g_mwsocket_state.ring_ready );
    up( &g_mwsocket_state.event_channel_sem );

    if ( NULL != g_mwsocket_state.response_reader_thread )
    {
        wait_for_completion( &g_mwsocket_state.response_reader_done );
    }
    
}
