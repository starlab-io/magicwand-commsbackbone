#include "mwcomms-common.h"

#include "mwcomms-backchannel.h"

#include <net/sock.h>
#include <net/inet_sock.h>

#include <linux/socket.h>
#include <linux/in.h>
#include <linux/list.h>
#include <linux/kthread.h>

#include <linux/kallsyms.h>

#include <xen_keystore_defs.h>
#include "mwcomms-xen-iface.h"


// In rundown, how long to wait for listener thread to die?
#define LISTENER_KILL_TIMEOUT (HZ * 2) // X seconds

// Sampling rate of listening thread. Must be less than kill timeout above.
#define LISTENER_MONITOR_INTERVAL (HZ * 1)

// XXXX: When this is 1, we hit a deadlock upon receiving incoming
// data and later unloading
#define HANDLE_INCOMING_DATA 1


typedef int
pfn_import_single_range_t(int rw, void __user *buf, size_t len,
                          struct iovec *iov, struct iov_iter *i);

typedef struct _mwcomms_backchannel_info
{
    struct list_head list;
    bool             active;
    // XXXX: how to detect remote close?
    struct socket  * conn;
} mwcomms_backchannel_info_t;


typedef struct _mwcomms_backchannel_state
{

    pfn_import_single_range_t * p_import_single_range;

    bool                      initialized;
    struct list_head          connection_list;
    struct rw_semaphore       connection_list_lock;
    atomic_t                  connection_ct;

    uint16_t                  listen_port;
    struct socket *           listen_sock;
    struct poll_table_struct  poll_tbl;

    bool                      exit_pending;
    struct task_struct *      listen_thread;
    struct completion         listen_thread_done;
} mwcomms_backchannel_state_t;

static mwcomms_backchannel_state_t g_mwbc_state = {0};

#ifdef DEBUG
static void
print_backchannel_state(void)
{

        mwcomms_backchannel_info_t * curr = NULL;
        int i = 0;
        struct sockaddr_in addr;
        int addrlen = sizeof(struct sockaddr_in);

        
        pr_debug("initialized:         %d\n", g_mwbc_state.initialized );
        pr_debug("connection count:    %d\n", g_mwbc_state.connection_ct.counter );
        pr_debug("listen port:         %d\n", g_mwbc_state.listen_port );

        pr_debug("\nsocket list: \n");
        list_for_each_entry( curr, &g_mwbc_state.connection_list, list )
        {

                 curr->conn->ops->getname( curr->conn,
                                     (struct sockaddr *) &addr,
                                     &addrlen,
                                     1 ); // get peer name
             pr_debug( "Connection %d connection from %pI4:%hu\n", i,
             &addr.sin_addr, ntohs( addr.sin_port ) );

             i++;
        }
}
#endif



#if 0
// @brief Make the socket non-blocking
//
// Likely not needed; cannot be used until sock_alloc_file() has been called
static void
mw_backchannel_nonblock( struct socket * Sock )
{
    if ( NULL == Sock ||
         NULL == Sock->file )
    {
        DEBUG_BREAK();
        return;
    }
    spin_lock( &Sock->file->f_lock );
    Sock->file->f_flags |= O_NONBLOCK;
    spin_unlock( &Sock->file->f_lock );
}
#endif



// @brief Read data off the given buffer. Won't block.
static int
MWSOCKET_DEBUG_ATTRIB
mw_backchannel_read( IN    mwcomms_backchannel_info_t * Channel,
                     OUT   void   * Message,
                     INOUT size_t * Len )
{
    int rc = 0;
    struct msghdr hdr = {0};
    struct iovec iov = {0};

    MYASSERT( Len );
    MYASSERT( *Len > 0 );


    // See sendto syscall def in net/socket.c
    rc = g_mwbc_state.p_import_single_range( READ,
                                             Message,
                                             *Len,
                                             &iov,
                                             &hdr.msg_iter );
    if ( rc )
    {
        MYASSERT( !"import of message failed" );
        goto ErrorExit;
    }

    // The prototype is one of these:
    //
    // sock_recvmsg( struct socket * soc, struct msghdr * m,
    //               size_t total_len, int flags);
    //
    // sock_recvmsg( struct socket * soc, struct msghdr * m, int flags);

    rc = sock_recvmsg( Channel->conn, &hdr, *Len, 0 );
    //rc = sock_recvmsg( Channel->conn, &hdr, 0 );

    pr_debug("Read %d bytes from socket", rc);
    // Returns number of bytes returned (>=0) or error (<0)
    if ( 0 == rc )
    {
        // POLLIN event indicated but no data
        //Channel->active = false;
        goto ErrorExit;
    }
    else if ( -EAGAIN == rc )
    {
        rc = 0;
        goto ErrorExit; // no (more) data
    }

    if ( rc < 0 )
    {
        MYASSERT( !"sock_recvmsg" );
        goto ErrorExit;
    }

    *Len = rc;
    rc = 0;

    pr_debug( "Read %d bytes from socket\n", rc );

ErrorExit:
    return rc;
} // mw_backchannel_read()


// @brief Consume all read buffers in the given socket
static int
MWSOCKET_DEBUG_ATTRIB
mw_backchannel_readall_drop( IN mwcomms_backchannel_info_t * Channel )
{
    int rc = 0;
    char buf[32] = {0};

    while ( true )
    {
            
        size_t size = sizeof(buf);
        memset( &buf, 0, sizeof(buf) );

        rc = mw_backchannel_read( Channel, buf, &size );
        
        if ( ! rc )
        {
            break;
        }
        if ( 0 == size )
        {
            break;
        }

    }

    //ErrorExit:
    return rc;
}

int
MWSOCKET_DEBUG_ATTRIB
mw_backchannel_write( void * Message, size_t Len )
{
    int rc = 0;
    struct msghdr hdr = {0};
    struct iovec iov;
    mwcomms_backchannel_info_t * curr = NULL;

    
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

    down_read( &g_mwbc_state.connection_list_lock );

    list_for_each_entry( curr, &g_mwbc_state.connection_list, list )
    {


        rc = sock_sendmsg( curr->conn, &hdr );
        // continue upon failure

        if ( rc != Len )
        {
            pr_err( "Failed to write all %d bytes in message: %d\n",
                    (int) Len, rc );
            MYASSERT( !"sendmsg() didn't send whole message" );
            if ( rc > 0 )
            {
                rc = -ENOSPC;
            }
        }
        else
        {
            rc = 0;
        }
    }

    up_read( &g_mwbc_state.connection_list_lock );

ErrorExit:
    return rc;
} // mw_backchannel_write()


int
mw_backchannel_write_msg( const char * Fmt, ... )
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

    rc = mw_backchannel_write( buf, rc );

ErrorExit:
    return rc;
}


bool
mw_backchannel_consumer_exists( void )
{
    return ( atomic_read( &g_mwbc_state.connection_ct ) > 0 );
}


static unsigned int
mw_backchannel_poll_socket( struct socket * Sock )
{
    unsigned int events = Sock->ops->poll( Sock->file,
                                           Sock,
                                           &g_mwbc_state.poll_tbl );
    MYASSERT( events >= 0 );

    return events;
}


static int
mw_backchannel_add_conn( struct socket * AcceptedSock )
{
    int rc = 0;
    mwcomms_backchannel_info_t * backinfo = NULL;
    char msg[80] = {0};
    struct sockaddr_in addr;
    int addrlen = sizeof(struct sockaddr_in);

    backinfo = (mwcomms_backchannel_info_t *)
        kmalloc( sizeof( *backinfo ), GFP_KERNEL | __GFP_ZERO );
    if ( NULL == backinfo )
    {
        rc = -ENOMEM;
        MYASSERT( !"kmalloc" );
        goto ErrorExit;
    }

    backinfo->conn = AcceptedSock;
    atomic_inc( &g_mwbc_state.connection_ct );
    backinfo->active = true;

    rc = AcceptedSock->ops->getname( AcceptedSock,
                                     (struct sockaddr *) &addr,
                                     &addrlen,
                                     1 ); // get peer name
    if ( rc )
    {
        MYASSERT( !"getname() failed" );
        goto ErrorExit;
    }

    
    pr_info( "Accepted connection from %pI4:%hu\n",
             &addr.sin_addr, ntohs( addr.sin_port ) );

    snprintf( msg, sizeof(msg),
              "Hello, you are connection #%d. You are %pI4:%hu\n",
              atomic_read( &g_mwbc_state.connection_ct ),
              &addr.sin_addr, ntohs( addr.sin_port ) );

    if ( rc )
    {
        goto ErrorExit;
    }

    // Nothing has failed: add to the global list
    down_write( &g_mwbc_state.connection_list_lock );
    list_add( &backinfo->list,
              &g_mwbc_state.connection_list );
    up_write( &g_mwbc_state.connection_list_lock );
    
    rc = mw_backchannel_write( (void *) msg, strlen(msg) + 1 );

ErrorExit:
    return rc;
}


static int
mw_backchannel_init_listen_port( void )
{
    int rc = 0;
    uint16_t port = 0;
    char portstr[6] = {0};
    struct sockaddr_in addr;
    int addrlen = sizeof(struct sockaddr_in);

    rc = sock_create( AF_INET,
                      SOCK_STREAM,
                      IPPROTO_TCP,
                      &g_mwbc_state.listen_sock );
    if ( rc )
    {
        MYASSERT( !"Failed to create backchannel socket" );
        goto ErrorExit;
    }

    // This effectively completes a bind() operation on 0.0.0.0 on a
    // kernel-chosen port. Compare to kernel's inet_autobind(). As
    // such, we do not call the socket's bind() operation.

    struct sock      * sk   = g_mwbc_state.listen_sock->sk;
    struct inet_sock * inet = inet_sk( sk );

    lock_sock( sk );

    if ( sk->sk_prot->get_port( sk, 0 ) )
    {
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

    rc = kernel_listen( g_mwbc_state.listen_sock, 2 );
    if ( rc )
    {
        MYASSERT( !"Failed to listen on backchannel socket" );
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

    pr_info( "Listening on %pI4:%hu (%hu)",
             &addr.sin_addr, ntohs( addr.sin_port ), port );

    (void) snprintf( portstr, sizeof(portstr), "%hu", port );

    // Make this a non-blocking socket
    //mw_backchannel_nonblock( g_mwbc_state.listen_sock );
    if ( NULL == sock_alloc_file( g_mwbc_state.listen_sock, O_NONBLOCK, NULL ) )
    {
        MYASSERT( !"sock_alloc_file" );
        rc = -ENOMEM;
        goto ErrorExit;
    }

    rc = mw_xen_write_to_key( XENEVENT_XENSTORE_PVM,
                              SERVER_BACKCHANNEL_PORT_KEY,
                              portstr );
    if ( rc )
    {
        goto ErrorExit;
    }

ErrorExit:
    return rc;
}


static int
MWSOCKET_DEBUG_ATTRIB
mw_backchannel_monitor( void * Arg )
{
    int rc = 0;

    pr_debug( "Entering\n" );

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
            DEBUG_BREAK();
            // Success
            rc = mw_backchannel_add_conn( newsock );
            if ( rc )
            {
                goto ErrorExit;
            }
        }
        else
        {
            if ( -EAGAIN != rc )
            {
                MYASSERT( !"accept" );
                goto ErrorExit;
            }
        }

        // Now, poll the existing connections. Check for pending read and close.
        down_write( &g_mwbc_state.connection_list_lock );
        mwcomms_backchannel_info_t * curr = NULL;
        mwcomms_backchannel_info_t * next = NULL;

        list_for_each_entry_safe( curr, next, &g_mwbc_state.connection_list, list )
        {
            unsigned int events = mw_backchannel_poll_socket( curr->conn );
	    
            if ( ( POLLHUP | POLLRDHUP ) & events )
            {
                 curr->active = false;
            }
            else if ( POLLIN & events )
            {
                // Data is available. At some point this is how we'll
                // receive a mitigation messages from the analytics
                // system. For now, read the data and throw it away to
                // clear the POLLIN event.
#if HANDLE_INCOMING_DATA

                (void) mw_backchannel_readall_drop( curr );
#else
                pr_info( "Ignoring incoming data on socket\n" );
#endif
            }

            if ( !curr->active )
            {
                pr_debug("Killing socket\n");

                //Killing connection to $IP port $PORT

                kernel_sock_shutdown( curr->conn, SHUT_RDWR );
                sock_release( curr->conn );
                atomic_dec( &g_mwbc_state.connection_ct );
                list_del( &curr->list );
                kfree( curr );
            }
        }

        up_write( &g_mwbc_state.connection_list_lock );

        // Sleep
        set_current_state( TASK_INTERRUPTIBLE );
        schedule_timeout( LISTENER_MONITOR_INTERVAL );
    }

ErrorExit:
    complete( &g_mwbc_state.listen_thread_done );
    pr_debug( "Leaving\n" );
    return rc;
}


void
mw_backchannel_fini( void )
{
    mwcomms_backchannel_info_t * curr = NULL;
    mwcomms_backchannel_info_t * prev = NULL;

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

    down_write( &g_mwbc_state.connection_list_lock );
    list_for_each_entry_safe( curr, prev, &g_mwbc_state.connection_list, list )
    {
        if ( curr->active )
        {
            sock_release( curr->conn );
        }
        list_del( &curr->list );
        kfree( curr );
    }
    up_write( &g_mwbc_state.connection_list_lock );

    if ( g_mwbc_state.listen_port )
    {
        sock_release( g_mwbc_state.listen_sock );
        g_mwbc_state.listen_sock = NULL;
    }

ErrorExit:
    return;
}


int
mw_backchannel_init( void )
{
    int rc = 0;

    bzero( &g_mwbc_state, sizeof(g_mwbc_state) );

    g_mwbc_state.initialized = true;


    
    INIT_LIST_HEAD( &g_mwbc_state.connection_list );
    init_completion( &g_mwbc_state.listen_thread_done );
    init_rwsem( &g_mwbc_state.connection_list_lock );

    init_poll_funcptr( &g_mwbc_state.poll_tbl, NULL );

    g_mwbc_state.p_import_single_range = (pfn_import_single_range_t *)
        kallsyms_lookup_name( "import_single_range" );
    if ( NULL == g_mwbc_state.p_import_single_range )
    {
        MYASSERT( !"Couldn't find required symbol" );
        rc = -ENXIO;
        goto ErrorExit;
    }

    rc = mw_backchannel_init_listen_port();
    if ( rc )
    {
        goto ErrorExit;
    }

    g_mwbc_state.listen_thread =
        kthread_run( &mw_backchannel_monitor,
                     NULL,
                     "MwNetflowHandler" );

    if ( NULL == g_mwbc_state.listen_thread )
    {
        MYASSERT( !"kthread_run() failed" );
        rc = -ESRCH;
        goto ErrorExit;
    }

ErrorExit:
    if ( rc )
    {
        mw_backchannel_fini();
    }
    return rc;
}
