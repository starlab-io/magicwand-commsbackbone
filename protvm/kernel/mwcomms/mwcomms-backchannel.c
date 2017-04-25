#include "mwcomms-common.h"

#include "mwcomms-backchannel.h"

#include <net/sock.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/list.h>
#include <linux/kthread.h>

#include <xen_keystore_defs.h>
//#include "mwcomms-xen-iface.h"



typedef struct _mwcomms_backchannel_info
{
    struct list_head list;
    struct socket * connection;
} mwcomms_backchannel_info_t;


typedef struct _mwcomms_backchannel_state
{
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

static mwcomms_backchannel_state_t g_mwbackchannel_state;


static int
mw_backchannel_listener( void * Arg )
{
    int rc = 0;

//ErrorExit:
    complete( &g_mwbackchannel_state.listen_thread_done );
    return rc;
}


int
mw_backchannel_fini( void )
{
    int rc = 0;

    g_mwbackchannel_state.exit_pending = true;
    if ( g_mwbackchannel_state.listen_thread )
    {
        wait_for_completion( &g_mwbackchannel_state.listen_thread_done );
    }

//ErrorExit:
    return rc;
}


int
mw_backchannel_init( void )
{
    struct sockaddr_in saddr;
    struct socket *server_sock = NULL;
    struct socket *new_sock = NULL;
    int rc = 0;

    bzero( &g_mwbackchannel_state, sizeof(g_mwbackchannel_state) );

    init_completion( &g_mwbackchannel_state.listen_thread_done );
    g_mwbackchannel_state.listen_thread =
        kthread_run( &mw_backchannel_listener,
                     NULL,
                     "MwNetflowHandler" );

    if ( NULL == g_mwbackchannel_state.listen_thread )
    {
        MYASSERT( !"kthread_run() failed" );
        rc = -ESRCH;
        goto ErrorExit;
    }

    rc = sock_create( AF_INET,
                      SOCK_STREAM,
                      IPPROTO_TCP,
                      &g_mwbackchannel_state.listen_sock );
    if ( rc )
    {
        MYASSERT( !"Failed to create backchannel socket" );
        goto ErrorExit;
    }

    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl( INADDR_ANY );
    saddr.sin_port = htons( 0 );

    // Bind
    rc = server_sock->ops->bind( server_sock,
                                 ( struct sockaddr* ) &saddr,
                                 sizeof( saddr ) );
    if ( rc )
    {
        MYASSERT( !"Failed to bind on backchannel socket" );
        goto ErrorExit;
    }

    // Listen
    rc = server_sock->ops->listen( server_sock, 1 );
    if ( rc )
    {
        MYASSERT( !"Failed to listen on backchannel socket" );
        goto ErrorExit;
    }

    pr_info( "Listening on port %d ip address: %pI4\n",
             saddr.sin_port, &saddr.sin_addr.s_addr );

    // Accept
    rc = server_sock->ops->accept( server_sock, new_sock, 0 );
    if ( rc )
    {
        pr_err( "Ins failed to accept connection on backchannel port\n" );
        goto ErrorExit;
    }

    pr_info( "Accepted connection" );

ErrorExit:
    if ( NULL != server_sock )
    {
        sock_release( server_sock );
    }
    if ( NULL != new_sock )
    {
        sock_release( new_sock );
    }
    return rc;
}
