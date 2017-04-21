#include "mwcomms-backchannel.h"

#include <net/sock.h>
#include <linux/socket.h>
#include <linux/in.h>


#include <xen_keystore_defs.h>
//#include "mwcomms-xen-iface.h"


int
mw_backchannel_init( void )
{

    struct sockaddr_in saddr, caddr;
    struct socket *server_sock = NULL;
    struct socket *new_sock = NULL;
    int err = -1;


    err = sock_create( AF_INET, SOCK_STREAM, IPPROTO_TCP, &server_sock);
    if ( err )
    {
        pr_err("Ins could not create backchannel socket\n");
        goto ErrorExit;
    }

    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl( INADDR_ANY );
    saddr.sin_port = htons( INS_BACKCHANNEL_PORT );

    //Bind
    err = server_sock->ops->bind( server_sock,
                           ( struct sockaddr* )&caddr,
                           sizeof( caddr ) );
    if ( err )
    {
        pr_err( "Ins could not bind to backchannel socket\n" ); 
        goto ErrorExit;
    }

    //Listen
    err = server_sock->ops->listen( server_sock, 1 );
    if ( err )
    {
        pr_err( "Ins failed to listen on backchannel socket\n" );
        goto ErrorExit;
    }

    pr_info( "Backchannel listening on port %d\n", INS_BACKCHANNEL_PORT );

    //Accept
    err = server_sock->ops->accept( server_sock, new_sock, 0 );
    if ( err )
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
    return err;
}
