#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <poll.h>

#include "user_common.h"
#include "xenevent_app_common.h"
#include "networking.h"
#include "message_types.h"
#include "threadpool.h"
#include "translate.h"

extern xenevent_globals_t g_state;
extern uint16_t client_id;

// Get a negative value, preferably (-1 * errno)
#define XE_GET_NEG_ERRNO()                              \
    ( errno > 0 ? (mt_status_t)(-errno) : (mt_status_t)(-1) )

//
// Upon recv(), should we attempt to receive all the bytes requested?
//
#define XE_RECEIVE_ALL 0


// XXXXXXXX verify that errno values match between Linux and NetBSD

static void
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

    DEBUG_PRINT ( "**** Thread %d <== socket %x / %d\n",
                  WorkerThread->idx, WorkerThread->sock_fd, sockfd );
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

#if 1
    uint32_t val = 1;
    int rc;
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
        //MYASSERT( !"accept" );
    }
    else
    {
        // Caller must fix up the socket assignments
        Response->base.status = sockfd;

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
    
    DEBUG_PRINT ( "Worker thread %d (socket %x / %d) is receiving %d bytes\n",
                  WorkerThread->idx,
                  WorkerThread->sock_fd, WorkerThread->native_sock_fd,
                  Request->requested );

#if XE_RECEIVE_ALL
    ssize_t totRecv = 0;
    while ( totRecv < Request->requested )
    {
        ssize_t rcv = recv( WorkerThread->native_sock_fd,
                            &Response->bytes[ totRecv ],
                            Request->requested - totRecv,
                            0 );
        if ( rcv < 0 )
        {
            // Error, even if some data has been received already
            Response->base.status = XE_GET_NEG_ERRNO();
            MYASSERT( !"recv" );
            break;
        }

        // rcv >= 0
        totRecv += rcv;

        if ( !XE_RECEIVE_ALL || 0 == rcv )
        {
            // Either: we are not attempting to recv() all the
            // requested bytes, or nothing was received. We're done.
            break;
        }
    }

    xe_net_set_base_response( (mt_request_generic_t *)Request,
                              MT_RESPONSE_SOCKET_RECV_SIZE + totRecv,
                              (mt_response_generic_t *)Response );
#else
    uint64_t bytes_read = 0;

    bytes_read = recv( WorkerThread->native_sock_fd,
                       (void *) Response->bytes,
                       Request->requested,
                       Request->flags );
    if ( bytes_read < 0 )
    {
        Response->base.status = XE_GET_NEG_ERRNO();
        MYASSERT( !"recv" );
        bytes_read = 0;

    }
    else
    {
        Response->base.status = bytes_read;
    }

    xe_net_set_base_response( (mt_request_generic_t*) Request,
                               MT_RESPONSE_SOCKET_RECV_SIZE + bytes_read,
                              (mt_response_generic_t *) Response);
#endif // XE_RECEIVE_ALL
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

// Create a pseudo-fd. The only resource we're reserving is a
// thread. No network resources are used.
int
xe_net_poll_create( IN  mt_request_poll_create_t  * Request,
                    OUT mt_response_poll_create_t * Response,
                    IN  thread_item_t             * WorkerThread )
{
    int rc = 0;
    mw_socket_fd_t fd = 0;
    
    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );

    fd = MW_EPOLL_CREATE_FD( client_id, WorkerThread->idx );

    // Set up Response; clobbers base.sockfd
    xe_net_set_base_response( (mt_request_generic_t *)Request,
                              MT_RESPONSE_POLL_CREATE_SIZE,
                              (mt_response_generic_t *)Response );

    Response->base.status = 0;
    Response->base.sockfd = fd;
    WorkerThread->sock_fd = fd;
    WorkerThread->native_sock_fd = MT_INVALID_FD;

    DEBUG_PRINT ( "**** Thread %d <== epoll fd %x\n",
                  WorkerThread->idx, fd );

    return rc;
}


int
xe_net_poll_close( IN  mt_request_poll_create_t  * Request,
                   OUT mt_response_poll_create_t * Response,
                   IN  thread_item_t             * WorkerThread )
{
    Response->base.status = 0;
    xe_net_set_base_response( (mt_request_generic_t *)Request,
                              MT_RESPONSE_SOCKET_CLOSE_SIZE,
                              (mt_response_generic_t *)Response );
    return 0;
}


int
xe_net_poll_wait( IN  mt_request_poll_wait_t  * Request,
                  OUT mt_response_poll_wait_t * Response,
                  IN  thread_item_t           * WorkerThread )
{
    int rc = 0;
    struct pollfd fds[ MAX_POLL_FD_COUNT ] = {0};
    int fdcount = Request->count;

    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );

    if ( Request->base.size
         != (MT_REQUEST_POLL_WAIT_SIZE + fdcount * MT_POLL_INFO_SIZE) )
    {
        _DEBUG_EMIT_BREAKPOINT();
        MYASSERT( !"The request's size is wrong" );
        Response->base.status = -EINVAL;
        fdcount = 0;
        goto ErrorExit;
    }

    for ( int i = 0; i < fdcount; ++i )
    {
        // Get the native FD and flags from the request
        mt_poll_info_t * pi = &Request->pollinfo[i];
        mw_socket_fd_t mwfd = pi->sockfd;

        int thridx = MW_SOCKET_GET_ID( mwfd );

        if ( thridx >= MAX_THREAD_COUNT
            || 0 == g_state.worker_threads[ thridx ].in_use )
        {
            Response->base.status = -EINVAL;
            MYASSERT( !"Invalid FD passed in" );
            goto ErrorExit;
        }

        fds[i].fd      = g_state.worker_threads[ thridx ].native_sock_fd;
        fds[i].events  = pi->events;
        fds[i].revents = 0;

        Response->pollinfo[i].sockfd = MT_INVALID_SOCKET_FD;
    } // for

    Response->count = fdcount; // Copy input count

    Response->base.status = poll( fds, Request->count, Request->timeout );
    DEBUG_PRINT( "poll() returned %ld\n", (long)Response->base.status );
    MYASSERT( 0 == Response->base.status );

    if ( Response->base.status < 0 )
    {
        Response->base.status = XE_GET_NEG_ERRNO();
        MYASSERT( !"poll" );
        goto ErrorExit;
    }

    // Pass back all the elements given. Report all return events but
    // only passing FDs.
    for ( int i = 0; i < fdcount; ++i )
    {
        if ( 0 != fds[i].revents )
        {
            Response->pollinfo[i].sockfd = Request->pollinfo[i].sockfd;
        }
        Response->pollinfo[i].events = fds[i].revents;
    } // for

    
ErrorExit:
    xe_net_set_base_response( (mt_request_generic_t *)Request,
                              MT_RESPONSE_POLL_WAIT_SIZE
                              + fdcount * MT_POLL_INFO_SIZE ,
                              (mt_response_generic_t *)Response );

    return rc;
}


