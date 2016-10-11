#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <errno.h>


#include "app_common.h"
#include "networking.h"
#include "message_types.h"
#include "threadpool.h"


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


static int
xe_net_get_native_protocol_family( mt_protocol_family_t Fam )
{
    int pfam = -1;

    switch( Fam )
    {
    case MT_PF_INET:
        pfam = AF_INET;
        break;
    case MT_PF_INET6:
        pfam = AF_INET6;
        break;
    case MT_PF_UNSET:
    default:
        MYASSERT( !"Invalid protocol family requested" );
    }

    return pfam;
}

static int
xe_net_get_native_sock_type( mt_sock_type_t Type )
{
    int stype = -1;

    switch( Type )
    {
    case MT_ST_DGRAM:
        stype = SOCK_DGRAM;
        break;
    case MT_ST_STREAM:
        stype = SOCK_STREAM;
        break;
    case MT_ST_UNSET:
    default:
        MYASSERT( !"Invalid socket type requested" );
    }

    return stype;
}


int
xe_net_create_socket( IN  mt_request_socket_create_t  * Request,
                      OUT mt_response_socket_create_t * Response,
                      OUT thread_item_t               * WorkerThread )
{
    int sockfd = 0;

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
        Response->base.status = errno;
    }
    
    // Set up Response
    xe_net_set_base_response( (mt_request_generic_t *)Request,
                              MT_RESPONSE_SOCKET_CREATE_SIZE,
                              (mt_response_generic_t *)Response );

    Response->base.sockfd = sockfd;

    // Set up BufferItem->assigned_thread for future reference during this session
    WorkerThread->sock_fd       = sockfd;
    WorkerThread->sock_domain   = native_fam;
    WorkerThread->sock_type     = native_type;
    WorkerThread->sock_protocol = Request->sock_protocol;

    DEBUG_PRINT ( "**** Thread %d <== socket %d\n",
                  WorkerThread->idx, sockfd );
    
    return Response->base.status;
}


int
xe_net_connect_socket( IN  mt_request_socket_connect_t  * Request,
                       OUT mt_response_socket_connect_t * Response,
                       IN  thread_item_t                * WorkerThread )
{
    int rc = 0;
    char portBuf[6] = {0}; // Max: 65536\0

    struct addrinfo serverHints = {0};
    struct addrinfo * serverInfo = NULL;
    struct addrinfo * serverIter = NULL;

    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );
    
    MYASSERT( WorkerThread->sock_fd == Request->base.sockfd );
    MYASSERT( 1 == WorkerThread->in_use );
    
    serverHints.ai_family    = WorkerThread->sock_domain;
    serverHints.ai_socktype  = WorkerThread->sock_type;

    Response->base.status = 0;

    DEBUG_PRINT ( "Worker thread %d (socket %d) is connecting to %s:%d\n",
                  WorkerThread->idx, WorkerThread->sock_fd,
                  Request->hostname, Request->port );

    if ( snprintf( portBuf, sizeof(portBuf), "%d", Request->port ) <= 0 )
    {
        MYASSERT( !"snprintf failed to extract port number" );
        Response->base.status = EINVAL;
        goto ErrorExit;
    }

    rc = getaddrinfo( (const char *)Request->hostname, portBuf, &serverHints, &serverInfo );
    if ( 0 != rc )
    {
        DEBUG_PRINT( "getaddrinfo failed: %s\n", gai_strerror(rc) );
        MYASSERT( !"getaddrinfo" );
        Response->base.status = EADDRNOTAVAIL;
        goto ErrorExit;
    }

    // Loop through all the results and connect to the first we can
    for (serverIter = serverInfo; serverIter != NULL; serverIter = serverIter->ai_next)
    {
        if ( serverIter->ai_family != WorkerThread->sock_domain ) continue;

        rc = connect( Request->base.sockfd, serverIter->ai_addr, serverIter->ai_addrlen);
        if ( rc < 0 )
        {
            // Silently continue
            continue;
        }

        // If we get here, we must have connected successfully
        break; 
    }
    
    if (serverIter == NULL)
    {
        // Looped off the end of the list with no connection.
        DEBUG_PRINT( "Couldn't connect() to %s:%s\n",
                     Request->hostname, portBuf );
        DEBUG_BREAK();
        Response->base.status = EADDRNOTAVAIL;
    }

ErrorExit:
    xe_net_set_base_response( (mt_request_generic_t *)Request,
                              MT_RESPONSE_SOCKET_CONNECT_SIZE,
                              (mt_response_generic_t *)Response );
    
    return Response->base.status;
}

int
xe_net_close_socket( IN  mt_request_socket_close_t  * Request,
                     OUT mt_response_socket_close_t * Response,
                     IN thread_item_t               * WorkerThread )
{
    int rc = 0;

    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );

    MYASSERT( WorkerThread->sock_fd == Request->base.sockfd );
    
    DEBUG_PRINT ( "Worker thread %d (socket %d) is closing connection\n",
                  WorkerThread->idx, WorkerThread->sock_fd );

    Response->base.status = 0;
    
    rc = close( Request->base.sockfd );
    if ( rc < 0 )
    {
        Response->base.status = errno;
    }

    xe_net_set_base_response( (mt_request_generic_t *)Request,
                              MT_RESPONSE_SOCKET_CLOSE_SIZE,
                              (mt_response_generic_t *)Response );

//    xe_net_set_base_response( Request, 0, Response );

    return Response->base.status;
}

int
xe_net_read_socket( IN  mt_request_socket_read_t  * Request,
                    OUT mt_response_socket_read_t * Response,
                    IN thread_item_t              * WorkerThread )
{
    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );

    ssize_t totRead = 0;
    //Response->base.size   = 0; // track total bytes received here
    Response->base.status = 0;

    MYASSERT( WorkerThread->sock_fd == Request->base.sockfd );
    
    DEBUG_PRINT ( "Worker thread %d (socket %d) is reading %d bytes\n",
                  WorkerThread->idx, WorkerThread->sock_fd, Request->requested);

    while ( Response->base.size < Request->requested )
    {
        ssize_t rcv = recv( Request->base.sockfd,
                            &Response->bytes[ Response->base.size ],
                            Request->requested - Response->base.size,
                            0 );
        if ( rcv < 0 )
        {
            Response->base.status = errno;
            MYASSERT( !"recv" );
            break;
        }

        totRead += rcv;
    }
    xe_net_set_base_response( (mt_request_generic_t *)Request,
                              MT_RESPONSE_SOCKET_READ_SIZE + totRead,
                              (mt_response_generic_t *)Response );

    return Response->base.status;
}


int
xe_net_write_socket( IN  mt_request_socket_write_t  * Request,
                     OUT mt_response_socket_write_t * Response,
                     IN thread_item_t               * WorkerThread )
{
    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );

    ssize_t totSent = 0; // track total bytes sent here
    Response->base.status = 0;

    MYASSERT( WorkerThread->sock_fd == Request->base.sockfd );
    
    DEBUG_PRINT ( "Worker thread %d (socket %d) is writing %d bytes\n",
                  WorkerThread->idx, WorkerThread->sock_fd, Request->base.size );

    // base.size is the total size of the request; account for the
    // header.
    while ( totSent < Request->base.size - MT_REQUEST_SOCKET_WRITE_SIZE )
    {
        ssize_t sent = send( Request->base.sockfd,
                             &Request->bytes[ totSent ],
                             Request->base.size - totSent,
                             0 );
        if ( sent < 0 )
        {
            Response->base.status = errno;
            MYASSERT( !"send" );
            break;
        }

        totSent += sent;
    }

    xe_net_set_base_response( (mt_request_generic_t *)Request,
                              MT_RESPONSE_SOCKET_WRITE_SIZE,
                              (mt_response_generic_t *)Response );

    return Response->base.status;
}

