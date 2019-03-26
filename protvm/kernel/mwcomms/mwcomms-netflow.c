/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

#include "mwcomms-common.h"

#include "mwcomms-netflow.h"
#include "mwcomms-socket.h"

#include <net/sock.h>
#include <net/inet_sock.h>

#include <linux/socket.h>
#include <linux/in.h>
#include <linux/list.h>
#include <linux/kthread.h>

#include <linux/kallsyms.h>
#include <linux/version.h>

#include <xen_keystore_defs.h>
#include "mwcomms-xen-iface.h"

#include <mw_netflow_iface.h>
#include <message_types.h>


// In rundown, how long to wait for listener thread to die?
#define LISTENER_KILL_TIMEOUT (HZ * 2) // X seconds

// Sampling rate of listening thread. Must be less than kill timeout above.
//#define LISTENER_MONITOR_INTERVAL (HZ * 1) // 1 sec
#define LISTENER_MONITOR_INTERVAL (HZ >> 1) // 1/2 sec

// State information for netflow channel
#define NETFLOW_CH_NONE       0x00000000
#define NETFLOW_CH_NO_MONITOR 0x00000001 // no monitoring data sent to channel

typedef int
pfn_import_single_range_t(int rw, void __user *buf, size_t len,
                          struct iovec *iov, struct iov_iter *i);

typedef struct _mwcomms_netflow_channel
{
    struct list_head list;
    bool             active;
    char             peer[64]; // IP:port of peer, helpful for debugging
    int              id;
    uint32_t         flags; // NETFLOW_CH_*
    struct socket  * conn;
} mwcomms_netflow_channel_t;


typedef struct _mwcomms_netflow_state
{
    bool                      initialized;

    pfn_import_single_range_t * p_import_single_range;

    // For connection management
    struct list_head          conn_list;
    struct rw_semaphore       conn_list_lock;
    atomic_t                  active_conns; // active connections
    atomic_t                  conn_ct;      // count; monotonically increases

    // Info about local end of netflow channel
    struct sockaddr         * local_ip;
    uint16_t                  listen_port;
    struct socket *           listen_sock;

    struct poll_table_struct  poll_tbl;

    // For thread management
    bool                      exit_pending;
    struct task_struct *      listen_thread;
    struct completion         listen_thread_done;
} mwcomms_netflow_state_t;

static mwcomms_netflow_state_t g_mwbc_state = {0};

static void
mw_netflow_dump( void )
{
#ifdef DEBUG
    pr_debug( "Netflow state\n" );

    printk( KERN_DEBUG "initialized:      %d\n", g_mwbc_state.initialized );
    printk( KERN_DEBUG "connection count: %d\n",
            atomic_read( &g_mwbc_state.active_conns ));
    printk( KERN_DEBUG "listen port:      %d\n", g_mwbc_state.listen_port );
    printk( KERN_DEBUG "\nsocket list: \n");

    mwcomms_netflow_channel_t * curr = NULL;

    down_read( &g_mwbc_state.conn_list_lock );
    list_for_each_entry( curr, &g_mwbc_state.conn_list, list )
    {
        printk( KERN_DEBUG "Connection %d from %s\n", curr->id, curr->peer );
    }
    up_read( &g_mwbc_state.conn_list_lock );
#endif
}


/**
 * @brief Read data off the given buffer. Won't block.
 */
static int
MWSOCKET_DEBUG_OPTIMIZE_OFF
mw_netflow_read( IN    mwcomms_netflow_channel_t * Channel,
                 OUT   void                      * Message,
                 INOUT size_t                    * Len )
{
    MYASSERT( Len );
    MYASSERT( *Len > 0 );
    MYASSERT( rwsem_is_locked( &g_mwbc_state.conn_list_lock ) );

    int rc = 0;
    size_t size = *Len;
    struct msghdr hdr = {0};
    struct iovec iov = {0};

    // See sendto syscall def in net/socket.c
    rc = g_mwbc_state.p_import_single_range( READ,
                                             Message,
                                             size,
                                             &iov,
                                             &hdr.msg_iter );
    if ( rc )
    {
        MYASSERT( !"import of message failed" );
        goto ErrorExit;
    }

    // In demostrating their commitment to backward compatibility, the
    // Linux kernel devs changed this function signature on us.

#if ( LINUX_VERSION_CODE <= KERNEL_VERSION(4,4,70) )
    rc = sock_recvmsg( Channel->conn, &hdr, *Len, MSG_DONTWAIT );
#else
    rc = sock_recvmsg( Channel->conn, &hdr, MSG_DONTWAIT );
#endif

    pr_debug( "Read %d bytes from socket (size = %zd)", rc, size );
    // Returns number of bytes (>=0) or error (<0)
    if ( 0 == rc )
    {
        // POLLIN event indicated but no data: channel is dead
        MYASSERT( !"Channel has no bytes to read" );
        rc = -EPIPE;
        Channel->active = false;
        goto ErrorExit;
    }
    else if ( -EAGAIN == rc )
    {
        // Return error, having cleared POLLIN flag from socket. There
        // is no data right now but the connection is still alive.
        goto ErrorExit;
    }
    else if ( rc < 0 )
    {
        // There's an unexpected failure. Consider the connection
        // dead.
        Channel->active = false;
        MYASSERT( !"sock_recvmsg" );
        goto ErrorExit;
    }

    // XXXX: did we read all the bytes?
    MYASSERT( rc == size );
    pr_debug( "Successfully read %d bytes from socket\n", rc );
    *Len = rc;
    rc = 0;

ErrorExit:
    return rc;
} // mw_netflow_read()


/**
 * @brief Writes the message to one connections.
 *
 * Caller must hold connection lock.
 */
static int
MWSOCKET_DEBUG_OPTIMIZE_OFF
mw_netflow_write_one( IN mwcomms_netflow_channel_t * Channel,
                      IN void                      * Message,
                      IN size_t                      Len )
{
    int rc = 0;
    struct msghdr hdr = {0};
    struct iovec iov = {0};

    // See sendto syscall def in net/socket.c
    rc = g_mwbc_state.p_import_single_range( WRITE,
                                             Message,
                                             Len,
                                             &iov,
                                             &hdr.msg_iter );
    if ( rc )
    {
        MYASSERT( !"import of message failed" );
        goto ErrorExit;
    }

    // N.B. sock_sendmsg modifies hdr

    rc = sock_sendmsg( Channel->conn, &hdr );
    if ( rc == Len )
    {
        rc = 0; // success
        goto ErrorExit;
    }

    MYASSERT( !"sock_sendmsg" );

    // Failure...
    pr_err( "Failed to write all %d bytes in message: %d\n", (int) Len, rc );
    if ( rc > 0 ) // incomplete write
    {
        rc = -EAGAIN;
    }
    else if ( rc <= 0 ) // failed write
    {
        Channel->active = false;
    }

ErrorExit:
    return rc;
}


/**
 * @brief Writes the message to all connections.
 *
 * Acquires connection lock.
 */
int
MWSOCKET_DEBUG_OPTIMIZE_OFF
mw_netflow_write_all( void * Message, size_t Len )
{
    int rc = 0;

//    MYASSERT( !rwsem_is_locked( &g_mwbc_state.conn_list_lock ) );
    down_read( &g_mwbc_state.conn_list_lock );

    mwcomms_netflow_channel_t * curr = NULL;
    list_for_each_entry( curr, &g_mwbc_state.conn_list, list )
    {
        if ( !( curr->flags & NETFLOW_CH_NO_MONITOR ) )
        {
            int rc2 = mw_netflow_write_one( curr, Message, Len );
            if ( rc2 )
            {
                // continue upon failure
                rc = rc2;
            }
        }
    }

    up_read( &g_mwbc_state.conn_list_lock );
    return rc;
}


int
mw_netflow_write_msg( const char * Fmt, ... )
{
    char buf[128] = {0};
    va_list args;
    int rc;

    va_start( args, Fmt );
    rc = vsnprintf( buf, sizeof(buf), Fmt, args );
    va_end( args );

    if ( rc < 0 )
    {
        MYASSERT( !"vnsprintf" );
        goto ErrorExit;
    }

    rc = mw_netflow_write_all( buf, rc );

ErrorExit:
    return rc;
}


/**
 * @brief Processes feature request, either on PVM or via ring buffer.
 */
static int
MWSOCKET_DEBUG_OPTIMIZE_OFF
mw_netflow_process_feat_req( IN  mw_feature_request_t  * Request,
                             IN mwcomms_netflow_channel_t * Channel,
                             OUT mw_feature_response_t * Response )
{
    MYASSERT( Request );
    MYASSERT( Response );
    MYASSERT( rwsem_is_locked( &g_mwbc_state.conn_list_lock ) );

    int rc = 0;
    bool modify = (Request->flags & MW_FEATURE_FLAG_WRITE);
    bool send = false;

    // if send is true, then create a new active connection, send it, and wait for response

    mt_request_socket_attrib_t  mtreq = {0};
    mt_response_socket_attrib_t mtres = {0};

    // Convert the inbound feature request into an
    // mt_request_socket_attrib_t. To simplify our logic and shorten
    // this function, we set some fields unnecessarily.

    mtreq.name = Request->name;
    switch( Request->name )
    {
    case MtChannelTrafficMonitorOn:
        Response->val.v32 = true;
        Channel->flags &= ~NETFLOW_CH_NO_MONITOR;
        break;
    case MtChannelTrafficMonitorOff:
        Response->val.v32 = true;
        Channel->flags |= NETFLOW_CH_NO_MONITOR;
        break;
    case MtSockAttribOwnerRunning:
        MYASSERT( Request->flags & MW_FEATURE_FLAG_BY_SOCK );
        Response->val.v32 = true;
        if ( modify )
        {
            rc = mwsocket_signal_owner_by_remote_fd( Request->ident.sockfd, SIGINT );
        }
        break;
    case MtSockAttribIsOpen:
        MYASSERT( Request->flags & MW_FEATURE_FLAG_BY_SOCK );
        Response->val.v32 = true;
        if ( modify )
        {
            rc = mwsocket_close_by_remote_fd( Request->ident.sockfd, true ); // wait
        }
        break;
    case MtSockAttribSndTimeo:
    case MtSockAttribRcvTimeo:
    case MtSockAttribSndBuf:
    case MtSockAttribRcvBuf:
    case MtSockAttribSndLoWat:
    case MtSockAttribRcvLoWat:
        // Global INS settings - sockfd/IP ignored
    case MtSockAttribGlobalCongctl:
    case MtSockAttribGlobalDelackTicks:
        send = true;
        break;
    case MtSockAttribAddrBlock:
        Request->val.v64 = __be64_to_cpu( Request->val.v64 );
        rc = mw_xen_for_each_live_ins( mwsocket_block_ip_mitigation, Request );
        break;
    default:
        MYASSERT( !"Unsupported name given" );
        rc = -EINVAL;
        break;
    }

    if ( send )
    {
        mtreq.base.size   = sizeof( mtreq );
        mtreq.base.type   = MtRequestSocketAttrib;
        mtreq.base.sockfd = Request->ident.sockfd;
        mtreq.modify    = modify;
        mtreq.name      = Request->name;
        mtreq.val       = Request->val;

        mtres.base.size = sizeof( mtres );

        rc = mwsocket_send_bare_request( (mt_request_generic_t *) &mtreq,
                                         (mt_response_generic_t *) &mtres );
        if ( !modify && !rc )
        {
            Response->val = mtres.val; // struct copy
        }
    }

    if ( rc && !Response->status ) { Response->status = rc; }
    return rc;
}


/**
 * @brief Reads feature request, processes it, and writes response.
 *
 * Caller must hold connection lock.
 */
static int
MWSOCKET_DEBUG_OPTIMIZE_OFF
mw_netflow_handle_feat_req( IN mwcomms_netflow_channel_t * Channel )
{
    int rc = 0;

    while ( true )
    {
        mw_feature_request_t req = {0};
        mw_feature_response_t res = {0};
        size_t size = sizeof( req );

        rc = mw_netflow_read( Channel, &req, &size );
        if ( rc || sizeof(req) != size ) { break; }

        req.base.sig = __be16_to_cpu( req.base.sig );
        req.base.id  = __be32_to_cpu( req.base.id );
        req.flags    = __be16_to_cpu( req.flags );
        req.name     = __be16_to_cpu( req.name );
        // req.val handled by mw_netflow_process_feat_req()
        res.val.t.s  = __be64_to_cpu( res.val.t.s  );
        res.val.t.us = __be64_to_cpu( res.val.t.us );

        MYASSERT( MW_MESSAGE_SIG_FEATURE_REQUEST == req.base.sig );

        if ( req.flags & MW_FEATURE_FLAG_BY_SOCK )
        {
            req.ident.sockfd = __be64_to_cpu( req.ident.sockfd );
        }
        else
        {
            req.ident.remote.af = __be32_to_cpu( req.ident.remote.af );
        }

        // Process the feature request. Response must be send!
        rc = mw_netflow_process_feat_req( &req, Channel, &res );
        if ( rc )
        {
            pr_warn( "Feature processing failed (%d). "
                     "Reporting on channel.\n", rc );
        } // fall-through to response code

        // Send response on channel
        res.base.sig = __cpu_to_be16( MW_MESSAGE_SIG_FEATURE_RESPONSE );
        res.base.id  = __cpu_to_be32( req.base.id );
        res.status   = __cpu_to_be32( res.status );
        res.val.t.s  = __cpu_to_be64( res.val.t.s );
        res.val.t.us = __cpu_to_be64( res.val.t.us );

        rc = mw_netflow_write_one( Channel, &res, sizeof( res ) );
        if ( rc ) { break; }
    }

    return rc;
}


bool
mw_netflow_consumer_exists( void )
{
    return ( atomic_read( &g_mwbc_state.active_conns ) > 0 );
}


static int
mw_netflow_add_conn( struct socket * AcceptedSock )
{
    int rc = 0;
    struct sockaddr_in addr;
    int addrlen = sizeof(struct sockaddr_in);

    mwcomms_netflow_channel_t * channel =
        (mwcomms_netflow_channel_t *) kmalloc( sizeof( *channel ),
                                                GFP_KERNEL | __GFP_ZERO );
    if ( NULL == channel )
    {
        rc = -ENOMEM;
        MYASSERT( !"kmalloc" );
        goto ErrorExit;
    }

    channel->conn = AcceptedSock;
    atomic_inc( &g_mwbc_state.active_conns );
    atomic_inc( &g_mwbc_state.conn_ct );

    channel->active = true;
    channel->id     = atomic_read( &g_mwbc_state.conn_ct );
    channel->flags  = NETFLOW_CH_NONE;

    rc = AcceptedSock->ops->getname( AcceptedSock,
                                     (struct sockaddr *) &addr,
                                     &addrlen,
                                     1 ); // get peer name
    if ( rc )
    {
        MYASSERT( !"getname() failed" );
        goto ErrorExit;
    }

    snprintf( channel->peer, sizeof(channel->peer),
              "%pI4:%hu", &addr.sin_addr, ntohs( addr.sin_port ) );

    pr_info( "Accepted connection from %s\n", channel->peer );

    // Nothing has failed: add to the global list
    down_write( &g_mwbc_state.conn_list_lock );
    list_add( &channel->list,
              &g_mwbc_state.conn_list );
    up_write( &g_mwbc_state.conn_list_lock );

    mw_netflow_dump();

ErrorExit:
    return rc;
}


static int
mw_netflow_init_listen_port( void )
{
    int rc = 0;
    uint16_t port = 0;
    char listenstr[32] = {0};
    struct sockaddr_in addr = {0};
    int addrlen = sizeof(struct sockaddr_in);

    rc = sock_create( AF_INET,
                      SOCK_STREAM,
                      IPPROTO_TCP,
                      &g_mwbc_state.listen_sock );
    if ( rc )
    {
        MYASSERT( !"Failed to create netflow socket" );
        goto ErrorExit;
    }

    // This effectively completes a bind() operation on 0.0.0.0 on a
    // kernel-chosen port. Compare to kernel's inet_autobind(). As
    // such, we do not call the socket's bind() operation. Note that
    // we bind on 0.0.0.0 but we advertise only the external address
    // in XenStore.

    struct sock      * sk   = g_mwbc_state.listen_sock->sk;
    struct inet_sock * inet = inet_sk( sk );

    lock_sock( sk );

    if ( sk->sk_prot->get_port( sk, 0 ) )
    {
        MYASSERT( !"get_port" );
        release_sock( sk );
        rc = -EAGAIN;
        goto ErrorExit;
    }
    // uint16_t port = inet->sk.__sk_common.skc_num;
    port = sk->sk_num;
    MYASSERT( port );

    inet->inet_dport = htons( port );
    release_sock( sk );

    g_mwbc_state.listen_port = port;

    rc = kernel_listen( g_mwbc_state.listen_sock, 5 );
    if ( rc )
    {
        MYASSERT( !"Failed to listen on netflow socket" );
        goto ErrorExit;
    }

    rc = g_mwbc_state.listen_sock->ops->getname( g_mwbc_state.listen_sock,
                                                 (struct sockaddr *) &addr,
                                                 &addrlen,
                                                 0 ); // self, not peer
    if ( rc )
    {
        MYASSERT( !"getname() failed" );
        goto ErrorExit;
    }

    //
    // Policy: if we couldn't discover our (singular) IP address, we
    // succeed in loading the driver but put an error string in
    // XenStore
    //
    if ( g_mwbc_state.local_ip->sa_family != AF_INET &&
         g_mwbc_state.local_ip->sa_family != AF_INET6 )
    {
        MYASSERT( !"No local IP address was found" );
        (void) snprintf( listenstr, sizeof(listenstr),
                         "PVM's IP not found!" );
    }
    else
    {
        (void) snprintf( listenstr, sizeof(listenstr),
                         "%pISc:%hu", g_mwbc_state.local_ip, port );
    }
    pr_info( "Netflow channel listening on %s\n", listenstr );

    // Make this a non-blocking socket
    //mw_netflow_nonblock( g_mwbc_state.listen_sock );
    if ( NULL == sock_alloc_file( g_mwbc_state.listen_sock, O_NONBLOCK, NULL ) )
    {
        MYASSERT( !"sock_alloc_file" );
        rc = -ENOMEM;
        goto ErrorExit;
    }

    rc = mw_xen_write_to_key( XENEVENT_XENSTORE_PVM,
                              SERVER_NETFLOW_PORT_KEY,
                              listenstr );
    if ( rc )
    {
        goto ErrorExit;
    }

ErrorExit:
    return rc;
}


static int
MWSOCKET_DEBUG_OPTIMIZE_OFF
mw_netflow_monitor( void * Arg )
{
    int rc = 0;

    while( true )
    {
        struct socket * newsock = NULL;

        if ( g_mwbc_state.exit_pending )
        {
            pr_debug( "Detected pending exit. Quitting thread.\n" );
            goto ErrorExit;
        }

        // Check the listen socket for new connections. It is
        // non-blocking.

        rc = kernel_accept( g_mwbc_state.listen_sock, &newsock, O_NONBLOCK );
        if ( 0 == rc )
        {
            // Success
            rc = mw_netflow_add_conn( newsock );
            if ( rc ) { goto ErrorExit; }
        }
        else
        {
            if ( -EAGAIN != rc )
            {
                MYASSERT( !"accept" );
                goto ErrorExit;
            }
        }

        // Now, poll the existing connections. Check for pending read
        // and close. Only hold the read lock for this, as the feature
        // handling code can call back into the netflow code.

        down_read( &g_mwbc_state.conn_list_lock );
        mwcomms_netflow_channel_t * curr = NULL;
        mwcomms_netflow_channel_t * next = NULL;

        list_for_each_entry( curr, &g_mwbc_state.conn_list, list )
        {
            unsigned int events = curr->conn->ops->poll( curr->conn->file,
                                                         curr->conn,
                                                         &g_mwbc_state.poll_tbl );
            if ( (POLLHUP | POLLRDHUP) & events )
            {
                // Remote side closed
                curr->active = false;
            }
            else if ( POLLIN & events )
            {
                // There's data to read. We must read it to clear this
                // flag, even if that just results in EAGAIN status.
                //
                // The only failure we care about is if the socket is
                // no longer useable, in which case curr->active has
                // been cleared.
                (void) mw_netflow_handle_feat_req( curr );
            }
        }
        up_read( &g_mwbc_state.conn_list_lock );

        // Purge inactive connections, this time holding the write lock.
        down_write( &g_mwbc_state.conn_list_lock );
        list_for_each_entry_safe( curr, next, &g_mwbc_state.conn_list, list )
        {
            if ( curr->active ) { continue; }

            // We determined from above that the connection is
            // dead. Close this side and remove from our list.
            pr_info( "Dropping connection to %s\n", curr->peer );
            kernel_sock_shutdown( curr->conn, SHUT_RDWR );
            sock_release( curr->conn );
            atomic_dec( &g_mwbc_state.active_conns );
            list_del( &curr->list );
            kfree( curr );
        }
        up_write( &g_mwbc_state.conn_list_lock );

        // Sleep
        set_current_state( TASK_INTERRUPTIBLE );
        schedule_timeout( LISTENER_MONITOR_INTERVAL );
    }

ErrorExit:
    complete( &g_mwbc_state.listen_thread_done );
    return rc;
}


void
mw_netflow_fini( void )
{
    mwcomms_netflow_channel_t * curr = NULL;
    mwcomms_netflow_channel_t * prev = NULL;

    if ( !g_mwbc_state.initialized )
    {
        pr_debug( "Intialization was never invoked. Nothing to clean up.\n" );
        goto ErrorExit;
    }

    g_mwbc_state.exit_pending = true;

    if ( g_mwbc_state.listen_thread )
    {
        wait_for_completion_timeout( &g_mwbc_state.listen_thread_done,
                                     LISTENER_KILL_TIMEOUT );
    }

    down_write( &g_mwbc_state.conn_list_lock );
    list_for_each_entry_safe( curr, prev, &g_mwbc_state.conn_list, list )
    {
        if ( curr->active )
        {
            sock_release( curr->conn );
        }
        list_del( &curr->list );
        kfree( curr );
    }
    up_write( &g_mwbc_state.conn_list_lock );

    if ( g_mwbc_state.listen_port )
    {
        sock_release( g_mwbc_state.listen_sock );
        g_mwbc_state.listen_sock = NULL;
    }

ErrorExit:
    return;
}


int
mw_netflow_init( struct sockaddr * LocalIp )
{
    int rc = 0;

    bzero( &g_mwbc_state, sizeof(g_mwbc_state) );

    g_mwbc_state.initialized = true;
    g_mwbc_state.local_ip = LocalIp;

    INIT_LIST_HEAD( &g_mwbc_state.conn_list );
    init_completion( &g_mwbc_state.listen_thread_done );
    init_rwsem( &g_mwbc_state.conn_list_lock );

    init_poll_funcptr( &g_mwbc_state.poll_tbl, NULL );

    g_mwbc_state.p_import_single_range = (pfn_import_single_range_t *)
        kallsyms_lookup_name( "import_single_range" );
    if ( NULL == g_mwbc_state.p_import_single_range )
    {
        MYASSERT( !"Couldn't find required symbol" );
        rc = -ENXIO;
        goto ErrorExit;
    }

    rc = mw_netflow_init_listen_port();
    if ( rc ) { goto ErrorExit; }

    g_mwbc_state.listen_thread =
        kthread_run( &mw_netflow_monitor,
                     NULL,
                     "mw_netflow_monitor" );

    if ( NULL == g_mwbc_state.listen_thread )
    {
        MYASSERT( !"kthread_run() failed" );
        rc = -ESRCH;
        goto ErrorExit;
    }

ErrorExit:
    if ( rc )
    {
        mw_netflow_fini();
    }
    return rc;
}
