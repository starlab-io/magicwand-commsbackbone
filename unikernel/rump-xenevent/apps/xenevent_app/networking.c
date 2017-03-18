#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netdb.h> 
#include <errno.h>

#include <arpa/inet.h>
#include <netinet/tcp.h>
//#include <sys/tcp.h>
#include <netinet/tcp.h>

#include <poll.h>
#include <fcntl.h>

#include "user_common.h"
#include "xenevent_app_common.h"
#include "networking.h"
#include "message_types.h"
#include "threadpool.h"
#include "translate.h"

extern xenevent_globals_t g_state;
extern uint16_t client_id;

//
// Upon recv(), should we attempt to receive all the bytes requested?
//
#define XE_RECEIVE_ALL 0


/*
//
// @brief Sets or clears the indicated flag on the file descriptor.
//
static int
xe_net_set_fd_flag( IN int  NativeFd,
                    IN int  Flag,
                    IN bool Enabled )
{
    int flags = 0;
    int rc = 0;

    DEBUG_PRINT( "%s flag(s) %x on fd %d\n",
                 Enabled ? "Setting" : "Clearing", Flag, NativeFd );

    flags = fcntl( NativeFd, F_GETFL );

    if ( Enabled )
    {
        flags = ( flags | Flag );
    }
    else
    {
        flags = ( flags & ~Flag );
    }

    rc = fcntl( NativeFd, F_SETFL, flags );
    MYASSERT( 0 == rc );

    return rc;
}
*/

void
xe_net_set_base_response( IN mt_request_generic_t   * Request,
                          IN size_t                   PayloadLen,
                          OUT mt_response_generic_t * Response )
{
    Response->base.sig    = MT_SIGNATURE_RESPONSE;
    Response->base.sockfd = Request->base.sockfd;

    Response->base.id   = Request->base.id;
    Response->base.type = MT_RESPONSE( Request->base.type );
    Response->base.size = PayloadLen;
}


int
xe_net_create_socket( IN  mt_request_socket_create_t  * Request,
                      OUT mt_response_socket_create_t * Response,
                      OUT thread_item_t               * WorkerThread )
{
    int sockfd = 0;
    mw_socket_fd_t extsock = 0;
    
    int native_fam  = xe_net_get_native_protocol_family( Request->sock_fam );
    int native_type = xe_net_get_native_sock_type( Request->sock_type );
    int native_proto = Request->sock_protocol;
    
    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );

    MYASSERT( AF_INET == native_fam ||
              AF_INET6 == native_fam );

    MYASSERT( SOCK_STREAM == native_type );

    Response->base.status = 0;
    
    sockfd = socket( native_fam,
                     native_type,
                     native_proto );
    if ( sockfd < 0 )
    {
        Response->base.status = XE_GET_NEG_ERRNO();
    }
    else
    {
        extsock = MW_SOCKET_CREATE( client_id, WorkerThread->idx );
    }

    // Set up Response; clobbers base.sockfd
    xe_net_set_base_response( (mt_request_generic_t *)Request,
                              MT_RESPONSE_SOCKET_CREATE_SIZE,
                              (mt_response_generic_t *)Response );

    // Set up BufferItem->assigned_thread for future reference during
    // this session

    Response->base.sockfd        = extsock;
    WorkerThread->sock_fd        = extsock;
    WorkerThread->native_sock_fd = sockfd;
    WorkerThread->sock_domain    = native_fam;
    WorkerThread->sock_type      = native_type;
    WorkerThread->sock_protocol  = Request->sock_protocol;

    // Apache hack
//    (void) xe_net_set_fd_flag( sockfd, O_NONBLOCK, true );

    DEBUG_PRINT ( "**** Thread %d <== socket %x / %d\n",
                  WorkerThread->idx, WorkerThread->sock_fd, sockfd );
    return 0;
}


int
xe_net_sock_attrib( IN  mt_request_socket_attrib_t  * Request,
                    OUT mt_response_socket_attrib_t * Response,
                    IN  thread_item_t               * WorkerThread )
{
    int flags = 0;
    int rc    = 0;
    int level = 0;
    int name  = 0;
    int err   = 0;
    
    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );
    MYASSERT( 1 == WorkerThread->in_use );
    MYASSERT( WorkerThread->idx >= 0 );

    if ( MtSockAttribNonblock == Request->attrib )
    {
        flags = fcntl( WorkerThread->native_sock_fd, F_GETFL );
        if ( Request->modify )
        {
            if ( Request->value )
            {
                flags |= O_NONBLOCK;
            }
            else
            {
                flags &= ~O_NONBLOCK;
            }
            rc = fcntl( WorkerThread->native_sock_fd, F_SETFL, flags );
            err = errno;
            MYASSERT( 0 == rc );
        }
        else
        {
            Response->outval = (uint32_t) (flags & O_NONBLOCK);
        }
        goto ErrorExit;
    }

    switch( Request->attrib )
    {
    case MtSockAttribReuseaddr:
        level = SOL_SOCKET;
        name = SO_REUSEADDR;
        break;
    case MtSockAttribKeepalive:
        level = SOL_SOCKET;
        name = SO_KEEPALIVE;
        break;
    case MtSockAttribNodelay:
        level = IPPROTO_TCP; //SOL_TCP;
        name  = TCP_NODELAY;
    case MtSockAttribDeferAccept:
        //level = SOL_TCP;
        // This option is not supported on Rump. We'll drop it.
        rc = 0;
        goto ErrorExit;
    default:
        MYASSERT( !"Unrecognized attribute given" );
        rc = -EINVAL;
        goto ErrorExit;
    }

    DEBUG_PRINT ( "Worker thread %d (socket %x / %d) is calling get/setsockopt %d/%d/%d\n",
                  WorkerThread->idx,
                  WorkerThread->sock_fd, WorkerThread->native_sock_fd,
                  level, name, Request->value );

    socklen_t len = sizeof(Request->value);
    if ( Request->modify )
    {
        rc = setsockopt( WorkerThread->native_sock_fd,
                         level, name,
                         &Request->value, len );
    }
    else
    {
        rc = getsockopt( WorkerThread->native_sock_fd,
                         level, name,
                         &Request->value, &len );
    }

    if ( rc )
    {
        err = errno;
        MYASSERT( !"getsockopt/setsockopt" );
        goto ErrorExit;
    }

ErrorExit:
    xe_net_set_base_response( (mt_request_generic_t *)  Request,
                              MT_RESPONSE_SOCKET_ATTRIB_SIZE,
                              (mt_response_generic_t *) Response );
    Response->base.status = rc ? XE_GET_NEG_ERRNO_VAL( err ) : 0;

    return 0;
}

int
xe_net_connect_socket( IN  mt_request_socket_connect_t  * Request,
                       OUT mt_response_socket_connect_t * Response,
                       IN  thread_item_t                * WorkerThread )
{
    int rc = 0;
//    char portBuf[6] = {0}; // Max: 65536\0

//    struct addrinfo serverHints   = {0};
    struct sockaddr_in sockaddr   = {0};
//    struct addrinfo * serverInfo  = NULL;
//    struct addrinfo * serverIter  = NULL;

    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );
    
    MYASSERT( WorkerThread->sock_fd == Request->base.sockfd );
    MYASSERT( 1 == WorkerThread->in_use );
    
//    serverHints.ai_family    = WorkerThread->sock_domain;
//    serverHints.ai_socktype  = WorkerThread->sock_type;

    Response->base.status = 0;

    populate_sockaddr_in( &sockaddr, &Request->sockaddr );

    DEBUG_PRINT ( "Worker thread %d (socket %x / %d) is connecting to %s:%d\n",
                  WorkerThread->idx,
                  WorkerThread->sock_fd, WorkerThread->native_sock_fd,
                  inet_ntoa( sockaddr.sin_addr ), ntohs(sockaddr.sin_port) );
    // Request->hostname, Request->port );

    rc = connect( WorkerThread->native_sock_fd,
                  (const struct sockaddr * ) &sockaddr,
                  sizeof( sockaddr ) );
    if ( rc < 0 )
    {
        Response->base.status = XE_GET_NEG_ERRNO();
        DEBUG_PRINT( "connect() failed with status %ld\n",
                     (unsigned long)Response->base.status );
        //MYASSERT( !"connect" );
    }


/*
    if ( snprintf( portBuf, sizeof(portBuf), "%d", htons( Request->sockaddr.sin_port ) ) <= 0 )
    {
        MYASSERT( !"snprintf failed to extract port number" );
        Response->base.status = -EINVAL;
        goto ErrorExit;
    }

    rc = getaddrinfo( (const char *)Request->hostname, portBuf, &serverHints, &serverInfo );
    if (  0 != rc )
    {
        DEBUG_PRINT( "getaddrinfo failed: %s\n", gai_strerror(rc) );
        MYASSERT( !"getaddrinfo" );
        Response->base.status = -EADDRNOTAVAIL;
        goto ErrorExit;
    }

    // Loop through all the results and connect to the first we can
    for ( serverIter = serverInfo; serverIter != NULL; serverIter = serverIter->ai_next )
    {
        if ( serverIter->ai_family != WorkerThread->sock_domain ) continue;

        rc = connect( WorkerThread->native_sock_fd,
                      serverIter->ai_addr,
                      serverIter->ai_addrlen );
        if ( rc < 0 )
        {
            continue; // Silently continue
        }

        break; // If we get here, we must have connected successfully
    }
    
    if ( serverIter == NULL )
    {
        // Looped off the end of the list with no connection.
        DEBUG_PRINT( "Couldn't connect() to %s:%s\n",
                     Request->hostname, portBuf );
        MYASSERT( !"connect" );
        Response->base.status = -EADDRNOTAVAIL;
    }
*/

//ErrorExit:
    xe_net_set_base_response( (mt_request_generic_t *)Request,
                              MT_RESPONSE_SOCKET_CONNECT_SIZE,
                              (mt_response_generic_t *)Response );
    return 0;
}


int
xe_net_bind_socket( IN mt_request_socket_bind_t     * Request,
                    OUT mt_response_socket_bind_t   * Response,
                    IN thread_item_t                * WorkerThread )
{
    
    MYASSERT( WorkerThread->sock_fd == Request->base.sockfd );
    MYASSERT( 1 == WorkerThread->in_use );

    struct sockaddr_in sockaddr;
    size_t addrlen = sizeof(sockaddr);;

    populate_sockaddr_in( &sockaddr, &Request->sockaddr );

    DEBUG_PRINT ( "Worker thread %d (socket %x / %d) is binding on %s:%d\n",
                  WorkerThread->idx,
                  WorkerThread->sock_fd, WorkerThread->native_sock_fd,
                  inet_ntoa( sockaddr.sin_addr ), ntohs(sockaddr.sin_port) );

    Response->base.status = bind( WorkerThread->native_sock_fd,
                                  (const struct sockaddr*) &sockaddr,
                                  addrlen );
    if ( Response->base.status < 0 )
    {
        Response->base.status = XE_GET_NEG_ERRNO();
        MYASSERT ( !"bind" );
    }

    xe_net_set_base_response( (mt_request_generic_t *) Request,
                              MT_RESPONSE_SOCKET_BIND_SIZE,
                              (mt_response_generic_t *) Response );

    return 0;
}


int
xe_net_listen_socket( IN    mt_request_socket_listen_t  * Request,
                      OUT   mt_response_socket_listen_t * Response,
                      IN    thread_item_t               * WorkerThread)
{
    MYASSERT( Request->base.sockfd == WorkerThread->sock_fd );
    MYASSERT( 1 == WorkerThread->in_use );

    DEBUG_PRINT ( "Worker thread %d (socket %x / %d) is listening\n",
                  WorkerThread->idx,
                  WorkerThread->sock_fd, WorkerThread->native_sock_fd );

    Response->base.status = listen( WorkerThread->native_sock_fd,
                                    Request->backlog);
    if( Response->base.status < 0 )
    {
        Response->base.status = XE_GET_NEG_ERRNO();
        MYASSERT( !"listen" );
    }

#if 0
    // HACK HACK HACK
    uint32_t val = 30;
    int rc = setsockopt( WorkerThread->native_sock_fd,
                         SOL_TCP,
                         TCP_DEFER_ACCEPT, 
                         &val,
                         sizeof(val) );
    MYASSERT( 0 == rc );
    // HACK HACK HACK
#endif

#if 0
    uint32_t val = 1;
    int rc = 0;
    rc = setsockopt( WorkerThread->native_sock_fd,
                     SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val) );
    MYASSERT( 0 == rc );

    rc = setsockopt( WorkerThread->native_sock_fd,
                     SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val) );
    MYASSERT( 0 == rc );

    //rc = setsockopt( WorkerThread->native_sock_fd,
    //SOL_TCP, TCP_NODELAY, &val, sizeof(val) );
    //MYASSERT( 0 == rc );
    
#endif
    
    xe_net_set_base_response( (mt_request_generic_t *)  Request,
                              MT_RESPONSE_SOCKET_LISTEN_SIZE,
                              (mt_response_generic_t *) Response);

    return 0;
}


int
xe_net_accept_socket( IN   mt_request_socket_accept_t  *Request,
                      OUT  mt_response_socket_accept_t *Response,
                      IN   thread_item_t               *WorkerThread )
{
    MYASSERT( Request->base.sockfd == WorkerThread->sock_fd );
    MYASSERT( 1 == WorkerThread->in_use );
    struct sockaddr_in sockaddr;
    int sockfd = 0;

    bzero( &sockaddr, sizeof(sockaddr));
    
    int addrlen = sizeof(sockaddr);

    DEBUG_PRINT ( "Worker thread %d (socket %x/%d) is accepting.\n",
                  WorkerThread->idx,
                  WorkerThread->sock_fd, WorkerThread->native_sock_fd );

    sockfd = accept( WorkerThread->native_sock_fd,
                     (struct sockaddr *) &sockaddr,
                     (socklen_t *) &addrlen );

    if ( sockfd < 0 )
    {
        Response->base.status = XE_GET_NEG_ERRNO();
        Response->base.sockfd = -1;
        // This happens frequently in non-blocking IO
        //MYASSERT( !"accept" );
    }
    else
    {
        // Caller must fix up the socket assignments
        Response->base.status = sockfd;

        // Apache hack
//        (void) xe_net_set_fd_flag( sockfd, O_NONBLOCK, true );

        DEBUG_PRINT ( "Worker thread %d (socket %x / %d) accepted from %s:%d\n",
                      WorkerThread->idx,
                      WorkerThread->sock_fd, WorkerThread->native_sock_fd,
                      inet_ntoa( sockaddr.sin_addr ), ntohs(sockaddr.sin_port) );
    }

    populate_mt_sockaddr_in( &Response->sockaddr, &sockaddr );

    xe_net_set_base_response( (mt_request_generic_t *)  Request,
                              MT_RESPONSE_SOCKET_ACCEPT_SIZE,
                              (mt_response_generic_t *) Response );
    return 0;
}

int
xe_net_recvfrom_socket( IN mt_request_socket_recv_t         *Request,
                        OUT mt_response_socket_recvfrom_t   *Response,
                        IN thread_item_t                    *WorkerThread )
{
    struct sockaddr_in   src_addr;
    socklen_t            addrlen = 0;
    uint64_t             bytes_read = 0;

    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );

    Response->base.status = 0;
    
    MYASSERT( WorkerThread->sock_fd == Request->base.sockfd );

    bytes_read = recvfrom( WorkerThread->native_sock_fd,
                           (void *) Response->bytes,
                           Request->requested,
                           Request->flags,
                           ( struct sockaddr * ) &src_addr,
                           &addrlen );

    if( bytes_read < 0 )
    {
        Response->base.status = XE_GET_NEG_ERRNO();
        MYASSERT( !"recvfrom" );
        bytes_read = 0;
    }
    else
    {
        Response->base.status = bytes_read;
    }

    populate_mt_sockaddr_in( &Response->src_addr, &src_addr );

    Response->addrlen  = addrlen;

    xe_net_set_base_response( ( mt_request_generic_t * ) Request,
                              MT_RESPONSE_SOCKET_RECVFROM_SIZE + bytes_read,
                              ( mt_response_generic_t * ) Response );
    return 0;
}



int
xe_net_recv_socket( IN   mt_request_socket_recv_t   * Request,
                    OUT  mt_response_socket_recv_t  * Response,
                    IN   thread_item_t              * WorkerThread )
{
    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );

    Response->base.status = 0;

    MYASSERT( WorkerThread->sock_fd == Request->base.sockfd );

// HACK HACK: force the socket to block
//    (void) xe_net_set_fd_flag( WorkerThread->native_sock_fd, O_NONBLOCK, false );
    // ==> recv(); errno == EAGAIN (35) [Rump] == EAGAIN (11) [Linux]

    DEBUG_PRINT ( "Worker thread %d (socket %x / %d) is receiving 0x%x bytes\n",
                  WorkerThread->idx,
                  WorkerThread->sock_fd, WorkerThread->native_sock_fd,
                  Request->requested );

    // This loop handles both evaulations for XE_RECEIVE_ALL. If any
    // data is received, it will be returned with a success status.

    ssize_t totRecv = 0;
    while ( totRecv < Request->requested )
    {
        ssize_t rcv = 0;
        do
        {
            rcv = recv( WorkerThread->native_sock_fd,
                        &Response->bytes[ totRecv ],
                        Request->requested - totRecv,
                        0 );
        } while( rcv < 0 && EINTR == errno );

        // recv() returned without being interrupted

        if ( rcv < 0 )
        {
            // This call failed. If any data has been received, let it
            // succeed. Otherwise fail it.
            Response->base.status = (totRecv > 0) ? 0 : XE_GET_NEG_ERRNO();
            break;
        }

        DEBUG_PRINT( "recv() got 0x%x bytes, status=%d\n",
                     (int)rcv, Response->base.status );

        totRecv += rcv;

        if ( !XE_RECEIVE_ALL || 0 == rcv )
        {
            // Either: we are not attempting to recv() all the
            // requested bytes, or nothing was received. We're done.
            break;
        }
    }

    DEBUG_PRINT( "recv() got total of 0x%x bytes, status=%d\n",
                 (int)totRecv, Response->base.status );

    xe_net_set_base_response( (mt_request_generic_t *)Request,
                              totRecv + MT_RESPONSE_SOCKET_RECV_SIZE,
                              (mt_response_generic_t *)Response );
    return 0;
}


int
xe_net_close_socket( IN  mt_request_socket_close_t  * Request,
                     OUT mt_response_socket_close_t * Response,
                     IN thread_item_t               * WorkerThread )
{

    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );

    MYASSERT( WorkerThread->sock_fd == Request->base.sockfd );

    DEBUG_PRINT ( "Worker thread %d (socket %x/%d) is closing\n",
                  WorkerThread->idx,
                  WorkerThread->sock_fd, WorkerThread->native_sock_fd );

    Response->base.status = close( WorkerThread->native_sock_fd );

    if ( Response->base.status < 0 )
    {
        Response->base.status = XE_GET_NEG_ERRNO();
        MYASSERT( !"close" );
    }

    xe_net_set_base_response( (mt_request_generic_t *)Request,
                              MT_RESPONSE_SOCKET_CLOSE_SIZE,
                              (mt_response_generic_t *)Response );

    return 0;
}

/*
int
xe_net_read_socket( IN  mt_request_socket_read_t  * Request,
                    OUT mt_response_socket_read_t * Response,
                    IN thread_item_t              * WorkerThread )
{
    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );

    ssize_t totRead = 0;
    Response->base.status = 0;
    Response->base.size   = MT_RESPONSE_SOCKET_READ_SIZE;
    
    MYASSERT( WorkerThread->sock_fd == Request->base.sockfd );
    
    DEBUG_PRINT ( "Worker thread %d (socket %d) is reading %d bytes\n",
                  WorkerThread->idx, WorkerThread->sock_fd, Request->requested);

    while ( totRead < Request->requested )
    {
        ssize_t rcv = recv( Request->base.sockfd,
                            &Response->bytes[ totRead ],
                            Request->requested - totRead,
                            0 );
        if ( rcv < 0 && totRead == 0 )
        {
            Response->base.status =  XE_GET_NEG_ERRNO();
            MYASSERT( !"read" );
            break;
        }
        if ( rcv =< 0 )
        {
            // We read some bytes but not all of them. Not an error,
            // but we're done.
            break;
        }

        // rcv > 0
        totRead += rcv;
        Response->base.size += rcv;
    }

    xe_net_set_base_response( (mt_request_generic_t *)Request,
                              MT_RESPONSE_SOCKET_READ_SIZE + totRead,
                              (mt_response_generic_t *)Response );

    return 0;
}
*/

int
xe_net_send_socket(  IN  mt_request_socket_send_t    * Request,
                     OUT mt_response_socket_send_t   * Response,
                     IN thread_item_t                * WorkerThread )
{
    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );

    ssize_t totSent = 0; // track total bytes sent here

    // payload length is declared size minus header size
    ssize_t maxExpected = Request->base.size - MT_REQUEST_SOCKET_SEND_SIZE;
    Response->base.status = 0;

    MYASSERT( WorkerThread->sock_fd == Request->base.sockfd );

    DEBUG_PRINT ( "Worker thread %d (socket %x / %d) is writing %ld bytes\n",
                  WorkerThread->idx,
                  WorkerThread->sock_fd, WorkerThread->native_sock_fd,
                  maxExpected );

    // base.size is the total size of the request; account for the
    // header.
    while ( totSent < maxExpected )
    {   
        ssize_t sent = send( WorkerThread->native_sock_fd,
                             &Request->bytes[ totSent ],
                             maxExpected - totSent,
                             (int) Request->flags );
        if ( sent < 0 )
        {
            Response->base.status = XE_GET_NEG_ERRNO();
            MYASSERT( !"send" );
            break;
        }

        totSent += sent;
    }

    Response->sent = totSent;

    xe_net_set_base_response( (mt_request_generic_t *)Request,
                              MT_RESPONSE_SOCKET_SEND_SIZE,
                              (mt_response_generic_t *)Response );

    return 0;
}


// Handles getsockname() and getpeername(), whose message types are
// equivalent.
int
xe_net_get_name( IN mt_request_socket_getname_t  * Request,
                 IN mt_response_socket_getname_t * Response,
                 IN  thread_item_t               * WorkerThread )
{
    int rc = 0;
    socklen_t addrlen = Request->maxlen;
    struct sockaddr addr;

    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );

    switch (Request->base.type )
    {
    case MtRequestSocketGetPeer:
        rc = getpeername( WorkerThread->native_sock_fd, &addr, &addrlen );
        break;
    case MtRequestSocketGetName:
        rc = getsockname( WorkerThread->native_sock_fd, &addr, &addrlen );
        break;
    default:
        MYASSERT( !"Invalid request" );
        rc = -1;
        goto ErrorExit;
    }

    Response->base.status = rc;
    Response->reslen      = (mt_size_t) addrlen;

    populate_mt_sockaddr_in( &Response->sockaddr, (struct sockaddr_in *)&addr );

    xe_net_set_base_response( (mt_request_generic_t *)Request,
                              MT_RESPONSE_SOCKET_GETNAME_SIZE,
                              (mt_response_generic_t *)Response );

ErrorExit:
    return rc;
}

