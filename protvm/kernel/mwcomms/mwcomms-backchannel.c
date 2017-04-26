#include "mwcomms-common.h"

#include "mwcomms-backchannel.h"

#include <net/sock.h>
#include <net/inet_sock.h>

#include <linux/socket.h>
#include <linux/in.h>
#include <linux/list.h>
#include <linux/kthread.h>


#include <xen_keystore_defs.h>
//#include "mwcomms-xen-iface.h"



typedef struct _mwcomms_backchannel_info
{
    struct list_head list;
    bool             active;
    struct socket    conn;
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

static mwcomms_backchannel_state_t g_mwbc_state;


int
mw_backchannel_write( void * Message, size_t Len )
{
    int rc = 0;
    struct msghdr hdr = {0};

ErrorExit:
    return rc;
}

static int
mw_backchannel_listener( void * Arg )
{
    int rc = 0;
    struct sockaddr_in saddr = {0};
    mwcomms_backchannel_info_t * backinfo = NULL;

    DEBUG_BREAK();
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

    rc = sock_create( AF_INET,
                      SOCK_STREAM,
                      IPPROTO_TCP,
                      &g_mwbc_state.listen_sock );
    if ( rc )
    {
        MYASSERT( !"Failed to create backchannel socket" );
        goto ErrorExit;
    }

/*
  static int inet_autobind(struct sock *sk)
175 {
176         struct inet_sock *inet;
177         // We may need to bind the socket. 
178         lock_sock(sk);
179         inet = inet_sk(sk);
180         if (!inet->inet_num) {
181                 if (sk->sk_prot->get_port(sk, 0)) {
182                         release_sock(sk);
183                         return -EAGAIN;
184                 }
185                 inet->inet_sport = htons(inet->inet_num);
186         }
187         release_sock(sk);
188         return 0;
*/
/*
    DEBUG_BREAK();
    struct sock      * sk   = g_mwbc_state.listen_sock->sk;
    struct inet_sock * inet = inet_sk( sk );

    lock_sock( sk );

    if ( sk->sk_prot->get_port( sk, 0 ) )
    {
        release_sock( sk );
        rc = -EAGAIN;
        goto ErrorExit;
    }
    inet->inet_dport =
        g_mwbc_state.listen_port =
        saddr.sin_port = htonl( inet->inet_num );
    DEBUG_BREAK();
    release_sock( sk );
    DEBUG_BREAK();
    saddr.sin_addr.s_addr = 0;
    saddr.sin_family = AF_INET;
    DEBUG_BREAK();

    MYASSERT( inet->inet_dport );
*/

    DEBUG_BREAK();
    g_mwbc_state.listen_port =
        saddr.sin_port = htons( BACKCHANNEL_PORT );
    DEBUG_BREAK();

    // Bind
    rc = g_mwbc_state.
        listen_sock->ops->bind( g_mwbc_state.listen_sock,
                                (struct sockaddr *) &saddr,
                                sizeof( saddr ) );
    if ( rc )
    {
        MYASSERT( !"Failed to bind on backchannel socket" );
        goto ErrorExit;
    }

    DEBUG_BREAK();

    // Listen
    rc = g_mwbc_state.
        listen_sock->ops->listen( g_mwbc_state.listen_sock, 1 );
    if ( rc )
    {
        MYASSERT( !"Failed to listen on backchannel socket" );
        goto ErrorExit;
    }

    pr_info( "Listening on port %d ip address: %pI4\n",
             BACKCHANNEL_PORT, &saddr.sin_addr.s_addr );

    DEBUG_BREAK();
    // Accept
    rc = g_mwbc_state.
        listen_sock->ops->accept( g_mwbc_state.listen_sock,
                                  &backinfo->conn,
                                  0 );
    if ( rc )
    {
        pr_err( "Ins failed to accept connection on backchannel port\n" );
        goto ErrorExit;
    }

    pr_info( "Accepted connection" );
    atomic_inc( &g_mwbc_state.connection_ct );

ErrorExit:
    complete( &g_mwbc_state.listen_thread_done );
    return rc;
}


void
mw_backchannel_fini( void )
{
    mwcomms_backchannel_info_t * curr = NULL;
    mwcomms_backchannel_info_t * prev = NULL;

    g_mwbc_state.exit_pending = true;

    if ( g_mwbc_state.listen_thread )
    {
        wait_for_completion( &g_mwbc_state.listen_thread_done );
    }

    list_for_each_entry_safe( curr, prev, &g_mwbc_state.connection_list, list )
    {
        if ( curr->active )
        {
            sock_release( &curr->conn );
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

    DEBUG_BREAK();    
    init_completion( &g_mwbc_state.listen_thread_done );

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
