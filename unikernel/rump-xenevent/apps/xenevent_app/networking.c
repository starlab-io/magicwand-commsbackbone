/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

/**
 * @file    networking.c
 * @author  Matt Leinhos
 * @date    29 March 2017
 * @version 0.1
 * @brief   MagicWand INS network API, used by xenevent.c
 *
 * This file defines the API that xenevent uses to interact with the
 * TCP/IP stack. There is a function here for each message type
 * defined in message_types.h.
 */

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
#include "mwerrno.h"

#include "pollset.h"

extern xenevent_globals_t g_state;
extern uint16_t client_id;


static int
xe_net_translate_msg_flags( IN mt_flags_t MsgFlags )
{
    int flags = 0;

    if ( MsgFlags & MW_MSG_OOB )          flags |= MSG_OOB;
    if ( MsgFlags & MW_MSG_PEEK )         flags |= MSG_PEEK;
    if ( MsgFlags & MW_MSG_DONTROUTE )    flags |= MSG_DONTROUTE;
    if ( MsgFlags & MW_MSG_CTRUNC )       flags |= MSG_CTRUNC;
    //if ( MsgFlags & MW_MSG_PROXY )        flags |= MSG_PROXY;
    if ( MsgFlags & MW_MSG_TRUNC )        flags |= MSG_TRUNC;
    if ( MsgFlags & MW_MSG_DONTWAIT )     flags |= MSG_DONTWAIT;
    if ( MsgFlags & MW_MSG_EOR )          flags |= MSG_EOR;
    if ( MsgFlags & MW_MSG_WAITALL )      flags |= MSG_WAITALL;
    //if ( MsgFlags & MW_MSG_FIN )          flags |= MSG_FIN;
    //if ( MsgFlags & MW_MSG_SYN )          flags |= MSG_SYN;
    //if ( MsgFlags & MW_MSG_CONFIRM )      flags |= MSG_CONFIRM;
    //if ( MsgFlags & MW_MSG_RST )          flags |= MSG_RST;
    //if ( MsgFlags & MW_MSG_ERRQUEUE )     flags |= MSG_ERRQUEUE;
    if ( MsgFlags & MW_MSG_NOSIGNAL )     flags |= MSG_NOSIGNAL;
    //if ( MsgFlags & MW_MSG_MORE )         flags |= MSG_MORE;
    if ( MsgFlags & MW_MSG_WAITFORONE )   flags |= MSG_WAITFORONE;
    //if ( MsgFlags & MW_MSG_FASTOPEN )     flags |= MSG_FASTOPEN;
    if ( MsgFlags & MW_MSG_CMSG_CLOEXEC ) flags |= MSG_CMSG_CLOEXEC;

    return flags;
}



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
    WorkerThread->public_fd      = extsock;
    WorkerThread->local_fd       = sockfd;
    WorkerThread->sock_domain    = native_fam;
    WorkerThread->sock_type      = native_type;
    WorkerThread->sock_protocol  = Request->sock_protocol;

    DEBUG_PRINT ( "**** Thread %d <== socket %x / %d\n",
                  WorkerThread->idx, WorkerThread->public_fd, sockfd );
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
        flags = fcntl( WorkerThread->local_fd, F_GETFL );
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
            rc = fcntl( WorkerThread->local_fd, F_SETFL, flags );
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
                  WorkerThread->public_fd, WorkerThread->local_fd,
                  level, name, Request->value );

    socklen_t len = sizeof(Request->value);
    if ( Request->modify )
    {
        rc = setsockopt( WorkerThread->local_fd,
                         level, name,
                         &Request->value, len );
    }
    else
    {
        rc = getsockopt( WorkerThread->local_fd,
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
    struct sockaddr_in sockaddr   = {0};

    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );
    
    MYASSERT( WorkerThread->public_fd == Request->base.sockfd );
    MYASSERT( 1 == WorkerThread->in_use );
    
    Response->base.status = 0;

    populate_sockaddr_in( &sockaddr, &Request->sockaddr );

    DEBUG_PRINT ( "Worker thread %d (socket %x / %d) is connecting to %s:%d\n",
                  WorkerThread->idx,
                  WorkerThread->public_fd, WorkerThread->local_fd,
                  inet_ntoa( sockaddr.sin_addr ), ntohs(sockaddr.sin_port) );

    rc = connect( WorkerThread->local_fd,
                  (const struct sockaddr * ) &sockaddr,
                  sizeof( sockaddr ) );
    if ( rc < 0 )
    {
        Response->base.status = XE_GET_NEG_ERRNO();
        DEBUG_PRINT( "connect() failed with status %ld\n",
                     (unsigned long)Response->base.status );
    }

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
    
    MYASSERT( WorkerThread->public_fd == Request->base.sockfd );
    MYASSERT( 1 == WorkerThread->in_use );

    struct sockaddr_in sockaddr;
    size_t addrlen = sizeof(sockaddr);;

    populate_sockaddr_in( &sockaddr, &Request->sockaddr );

    DEBUG_PRINT ( "Worker thread %d (socket %x / %d) is binding on %s:%d\n",
                  WorkerThread->idx,
                  WorkerThread->public_fd, WorkerThread->local_fd,
                  inet_ntoa( sockaddr.sin_addr ), ntohs(sockaddr.sin_port) );

    Response->base.status = bind( WorkerThread->local_fd,
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
    MYASSERT( Request->base.sockfd == WorkerThread->public_fd );
    MYASSERT( 1 == WorkerThread->in_use );

    DEBUG_PRINT ( "Worker thread %d (socket %x / %d) is listening\n",
                  WorkerThread->idx,
                  WorkerThread->public_fd, WorkerThread->local_fd );

    Response->base.status = listen( WorkerThread->local_fd,
                                    Request->backlog);
    if( Response->base.status < 0 )
    {
        Response->base.status = XE_GET_NEG_ERRNO();
        MYASSERT( !"listen" );
    }
    
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
    MYASSERT( Request->base.sockfd == WorkerThread->public_fd );
    MYASSERT( 1 == WorkerThread->in_use );
    struct sockaddr_in sockaddr;
    int sockfd = 0;
    int rc = 0;
    int addrlen = sizeof(sockaddr);
    
    bzero( &sockaddr, addrlen );

    DEBUG_PRINT ( "Worker thread %d (socket %x/%d) is accepting.\n",
                  WorkerThread->idx,
                  WorkerThread->public_fd, WorkerThread->local_fd );

    // NetBSD does not implement accept4. Therefore the flags are dropped.
    if ( Request->flags )
    {
        DEBUG_PRINT( "Dropping PVM flags 0x%x in accept()\n", Request->flags );
    }

    sockfd = accept( WorkerThread->local_fd,
                     (struct sockaddr *) &sockaddr,
                     (socklen_t *) &addrlen );
    if ( sockfd < 0 )
    {
        Response->base.status = XE_GET_NEG_ERRNO();
        Response->base.sockfd = -1;
        // This happens frequently in non-blocking IO. Don't assert.
    }
    else
    {
        // Poor man's implementation of SOCK_NONBLOCK flag
        if ( Request->flags & MW_SOCK_NONBLOCK )
        {
            int flags = fcntl( sockfd, F_GETFL );
            flags |= O_NONBLOCK;
            rc = fcntl( sockfd, F_SETFL, flags );
            if ( rc )
            {
                Response->base.status = XE_GET_NEG_ERRNO();
                MYASSERT( !"fcntl" );
                Response->base.sockfd = -1;
                close( sockfd );
                goto ErrorExit; // internal error
            }
        }
        
        // Caller must fix up the socket assignments
        Response->base.status = sockfd;

        DEBUG_PRINT ( "Worker thread %d (socket %x / %d) accepted from %s:%d\n",
                      WorkerThread->idx,
                      WorkerThread->public_fd, WorkerThread->local_fd,
                      inet_ntoa( sockaddr.sin_addr ), ntohs(sockaddr.sin_port) );
    }

    populate_mt_sockaddr_in( &Response->sockaddr, &sockaddr );

    xe_net_set_base_response( (mt_request_generic_t *)  Request,
                              MT_RESPONSE_SOCKET_ACCEPT_SIZE,
                              (mt_response_generic_t *) Response );
ErrorExit:
    return rc;
}


int
xe_net_recvfrom_socket( IN mt_request_socket_recv_t         *Request,
                        OUT mt_response_socket_recvfrom_t   *Response,
                        IN thread_item_t                    *WorkerThread )
{
    struct sockaddr_in src_addr;
    socklen_t      addrlen = 0;
    int             events = 0;
    int                 rc = 0;
    bool            polled = false;    
    int              flags = xe_net_translate_msg_flags( Request->flags );

    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );
    MYASSERT( WorkerThread->public_fd == Request->base.sockfd );
    
    Response->count       = 0;
    Response->base.status = 0;

    while( true )
    {
        do
        {
            Response->count = recvfrom( WorkerThread->local_fd,
                                        (void *) Response->bytes,
                                        Request->requested,
                                        flags,
                                        ( struct sockaddr * ) &src_addr,
                                        &addrlen );
        } while( Response->count < 0 && EINTR == errno );

        // recvfrom() returned without being interrupted

        if ( Response->count < 0 )
        {
            Response->base.status = XE_GET_NEG_ERRNO();
            Response->count       = 0;
            break;
        }

        // Success
        Response->base.status = Response->count;

        if ( Response->count > 0 ) break;

        // recvfrom() ==> 0. Check for remote close

        // Check for events from previous loop: there was supposed
        // to be data, but we didn't read any. The connection was
        // closed on the other end.
        if ( events & (POLLIN | POLLRDNORM) )
        {
            WorkerThread->state_flags |= _MT_FLAGS_REMOTE_CLOSED;
            DEBUG_BREAK();
            break;
        }

        // poll() was called but no readable data was
        // indicated. There's nothing to read but the connection
        // remains open.
        if ( polled )
        {
            WorkerThread->state_flags = 0;
            break;
        }
        
        // poll() has not been invoked yet. Invoke it and check for results again.
        rc = xe_pollset_query_one( WorkerThread->local_fd, &events );
        // Check for failure: this counts as an internal error
        if ( rc ) goto ErrorExit;

        polled = true;
    } // while

    populate_mt_sockaddr_in( &Response->src_addr, &src_addr );

    Response->addrlen  = addrlen;

    xe_net_set_base_response( ( mt_request_generic_t * ) Request,
                              MT_RESPONSE_SOCKET_RECVFROM_SIZE + Response->count,
                              ( mt_response_generic_t * ) Response );

ErrorExit: // label for internal errors only
    return rc;
}



int
xe_net_recv_socket( IN   mt_request_socket_recv_t   * Request,
                    OUT  mt_response_socket_recv_t  * Response,
                    IN   thread_item_t              * WorkerThread )
{
    int             events = 0;
    int                 rc = 0;
    bool            polled = false;    
    int              flags = xe_net_translate_msg_flags( Request->flags );
    
    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );

    Response->count       = 0;
    Response->base.status = 0;

    MYASSERT( WorkerThread->public_fd == Request->base.sockfd );

    DEBUG_PRINT ( "Worker thread %d (socket %x / %d) is receiving 0x%x bytes\n",
                  WorkerThread->idx,
                  WorkerThread->public_fd, WorkerThread->local_fd,
                  Request->requested );

    while ( true )
    {
        do
        {
            Response->count = recv( WorkerThread->local_fd,
                                    &Response->bytes[ Response->count ],
                                    Request->requested - Response->count,
                                    flags );
        } while( Response->count < 0 && EINTR == errno );

        // recv() returned without being interrupted

        if ( Response->count < 0 )
        {
            Response->base.status = (Response->count > 0) ? 0 : XE_GET_NEG_ERRNO();
            Response->count       = 0;
            //MYASSERT( !"recv" );
            break;
        }

        // Success
        Response->base.status = Response->count;

        if ( Response->count > 0 ) break;

        // recv() ==> 0. Check for remote close.
        if ( events & (POLLIN | POLLRDNORM) )
        {
            WorkerThread->state_flags |= _MT_FLAGS_REMOTE_CLOSED;
            break;
        }

        // poll() was called but no readable data was indicated. The
        // connection is still open.
        if ( polled )
        {
            WorkerThread->state_flags = 0;
            break;
        }
        
        // poll() has not been invoked yet. Invoke it and check for results again.
        rc = xe_pollset_query_one( WorkerThread->local_fd, &events );
        // Check for failure: this counts as an internal error
        if ( rc ) goto ErrorExit;

        polled = true;
    } // while

    DEBUG_PRINT( "recv() got total of 0x%x bytes, status=%d\n",
                 (int)Response->count, Response->base.status );

    xe_net_set_base_response( (mt_request_generic_t *)Request,
                              Response->count + MT_RESPONSE_SOCKET_RECV_SIZE,
                              (mt_response_generic_t *)Response );
ErrorExit:
    return rc;
}


int
xe_net_shutdown_socket( IN  mt_request_socket_shutdown_t  * Request,
                        OUT mt_response_socket_shutdown_t * Response,
                        IN  thread_item_t                 * WorkerThread )
{
    int rc = 0;

    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );
    MYASSERT( WorkerThread->public_fd == Request->base.sockfd );

    rc = shutdown( WorkerThread->local_fd,
                   Request->how );

    Response->base.status = (0 == rc) ? 0 : XE_GET_NEG_ERRNO();
    DEBUG_PRINT( "shutdown(%d, %d) ==> %d\n",
                 WorkerThread->local_fd, Request->how, Response->base.status );

    xe_net_set_base_response( (mt_request_generic_t *)Request,
                              MT_RESPONSE_SOCKET_SHUTDOWN_SIZE,
                              (mt_response_generic_t *)Response );
    return 0;
}


int
xe_net_internal_close_socket( IN thread_item_t * WorkerThread )
{
    int rc = 0;

    if ( MT_INVALID_SOCKET_FD != WorkerThread->local_fd )
    {
        DEBUG_PRINT ( "Worker thread %d (socket %x/%d) is closing\n",
                      WorkerThread->idx,
                      WorkerThread->public_fd, WorkerThread->local_fd );

        rc = close( WorkerThread->local_fd );
        if ( rc )
        {
            rc = XE_GET_NEG_ERRNO();
            MYASSERT( !"close" );
            goto ErrorExit;
        }
    }

ErrorExit:
    WorkerThread->local_fd  = MT_INVALID_SOCKET_FD;
    WorkerThread->public_fd = MT_INVALID_SOCKET_FD;

    return rc;
}


int
xe_net_close_socket( IN  mt_request_socket_close_t  * Request,
                     OUT mt_response_socket_close_t * Response,
                     IN thread_item_t               * WorkerThread )
{
    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );

    MYASSERT( WorkerThread->public_fd == Request->base.sockfd );

    Response->base.status = xe_net_internal_close_socket( WorkerThread );

    xe_net_set_base_response( (mt_request_generic_t *)Request,
                              MT_RESPONSE_SOCKET_CLOSE_SIZE,
                              (mt_response_generic_t *)Response );

    return 0;
}


int
xe_net_send_socket(  IN  mt_request_socket_send_t    * Request,
                     OUT mt_response_socket_send_t   * Response,
                     IN thread_item_t                * WorkerThread )
{
    int flags = xe_net_translate_msg_flags( Request->flags );
    // payload length is declared size minus header size
    ssize_t maxExpected = Request->base.size - MT_REQUEST_SOCKET_SEND_SIZE;

    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );
    MYASSERT( WorkerThread->public_fd == Request->base.sockfd );
    
    Response->count       = 0;
    Response->base.status = 0;
    // Pass flags back to the PVM for processing
    Response->flags       = Request->flags;

    DEBUG_PRINT ( "Worker thread %d (socket %x / %d) is sending %ld bytes\n",
                  WorkerThread->idx,
                  WorkerThread->public_fd, WorkerThread->local_fd,
                  maxExpected );

    // base.size is the total size of the request; account for the
    // header.
    while ( Response->count < maxExpected )
    {   
        ssize_t sent = send( WorkerThread->local_fd,
                             &Request->bytes[ Response->count ],
                             maxExpected - Response->count,
                             flags );
        if ( sent < 0 )
        {
            int err = errno;

            // Never return EAGAIN to the PVM. This effectively makes
            // the send() blocking and it relieves the PVM of doing
            // send requests serially, where it must wait for a
            // response before issuing the next send().
            if ( (EAGAIN == err || EWOULDBLOCK == err)  )
            {
                // The send would block on this non-blocking
                // socket. Force retry.
                continue;
            }

            Response->base.status = XE_GET_NEG_ERRNO_VAL( err );
            // The remote side of this connection has closed
            if ( -MW_EPIPE == Response->base.status )
            {
                WorkerThread->state_flags = _MT_FLAGS_REMOTE_CLOSED;
            }
            else
            {
                WorkerThread->state_flags = 0;
            }
            break;
        }

        // Success
        MYASSERT( sent > 0 );
        Response->count += sent;
    }

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
        rc = getpeername( WorkerThread->local_fd, &addr, &addrlen );
        break;
    case MtRequestSocketGetName:
        rc = getsockname( WorkerThread->local_fd, &addr, &addrlen );
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

