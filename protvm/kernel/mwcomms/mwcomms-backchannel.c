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

    struct list_head connection_list;
//    struct mutex     connection_list_lock;
    atomic_t         connection_ct;

    uint16_t         listen_port;
    struct socket *  listen_sock;
    struct poll_table_struct poll_tbl;

    bool                 exit_pending;
    struct task_struct * listen_thread;
    struct completion    listen_thread_done;
} mwcomms_backchannel_state_t;

static mwcomms_backchannel_state_t g_mwbc_state;


// See sendto syscall def in net/socket.c
int
mw_backchannel_write( void * Message, size_t Len )
{
    int rc = 0;
    struct msghdr hdr = {0};
    struct iovec iov;
    mwcomms_backchannel_info_t * curr = NULL;

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

    // XXXX: lock the connection list
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

ErrorExit:
    return rc;
}


bool
mw_backchannel_consumer_exists( void )
{
    return ( atomic_read( &g_mwbc_state.connection_ct ) > 0 );
}


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
mw_backchannel_listener( void * Arg )
{
    int rc = 0;
    mwcomms_backchannel_info_t * backinfo = NULL;
    char msg[48] = {0};
    struct sockaddr_in addr;
    int addrlen = sizeof(struct sockaddr_in);

    pr_debug( "Entering\n" );

    backinfo = kmalloc( sizeof( *backinfo ),
                        GFP_KERNEL | __GFP_ZERO );
    if ( NULL == backinfo )
    {
        rc = -ENOMEM;
        MYASSERT( !"kmalloc" );
        goto ErrorExit;
    }

    list_add( &backinfo->list,
              &g_mwbc_state.connection_list );

    // Accept
    rc = kernel_accept( g_mwbc_state.listen_sock,
                        &backinfo->conn,
                        0 );
    if ( rc )
    {
        pr_err( "Ins failed to accept connection on backchannel port\n" );
        goto ErrorExit;
    }

    atomic_inc( &g_mwbc_state.connection_ct );
    backinfo->active = true;

    rc = backinfo->conn->ops->getname( backinfo->conn,
                                       (struct sockaddr *) &addr,
                                       &addrlen,
                                       1 ); // peer
    if ( rc )
    {
        MYASSERT( !"getname() failed" );
        goto ErrorExit;
    }

    pr_info( "Accepted connection from %pI4:%hu\n",
             &addr.sin_addr, ntohs( addr.sin_port ) );

    snprintf( msg, sizeof(msg),
              "Hello from backchannel, connection #%d\n",
              atomic_read( &g_mwbc_state.connection_ct ) );

    rc = mw_backchannel_write( (void *) msg, strlen(msg) + 1 );
    if ( rc )
    {
        goto ErrorExit;
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

    g_mwbc_state.exit_pending = true;

#define LISTENER_KILL_TIMEOUT (HZ * 2) // X seconds
    if ( g_mwbc_state.listen_thread )
    {
        wait_for_completion_timeout( &g_mwbc_state.listen_thread_done,
                                     LISTENER_KILL_TIMEOUT );
    }

    list_for_each_entry_safe( curr, prev, &g_mwbc_state.connection_list, list )
    {
        if ( curr->active )
        {
            sock_release( curr->conn );
        }
        list_del( &curr->list );
        kfree( curr );
    }

    if ( g_mwbc_state.listen_port )
    {
        sock_release( g_mwbc_state.listen_sock );
        g_mwbc_state.listen_sock = NULL;
    }
}


int
mw_backchannel_init( void )
{
    int rc = 0;

    bzero( &g_mwbc_state, sizeof(g_mwbc_state) );

    INIT_LIST_HEAD( &g_mwbc_state.connection_list );

    init_completion( &g_mwbc_state.listen_thread_done );

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
        kthread_run( &mw_backchannel_listener,
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
