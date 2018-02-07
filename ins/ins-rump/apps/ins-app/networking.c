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

#include <sys/param.h>
#include <sys/sysctl.h>

#include <poll.h>
#include <fcntl.h>

#include <sys/ioctl.h>

#include "user_common.h"
#include "xenevent_app_common.h"
#include "networking.h"
#include "message_types.h"
#include "threadpool.h"
#include "translate.h"
#include "mwerrno.h"
#include "ins-ioctls.h"
#include "pollset.h"
#include <mw_netflow_iface.h>

#define XE_NET_NETWORK_PARAM_WIDTH 30

extern xenevent_globals_t g_state;


#if MODIFY_NETWORK_PARAMETERS
//
// Describe the network configuration parameters that are accepted
// over XenStore via an ioctl()
//
typedef struct _xe_net_param
{
    char * key;
    char * path;
    // Base of the value: 16 for hex value, 0 for string
    int    base;
} xe_net_param_t;

static xe_net_param_t g_net_params[] =
{
    // See src-netbsd/sys/netinet/tcp_usrreq.c for some of the options
    { "sendbuf_auto",   "net.inet.tcp.sendbuf_auto",    16 },
    { "sendspace",      "net.inet.tcp.sendspace",       16 },
    { "sendbuf_inc",    "net.inet.tcp.sendbuf_inc",     16 },
    { "sendbuf_max",    "net.inet.tcp.sendbuf_max",     16 },
    { "recvbuf_auto",   "net.inet.tcp.recvbuf_auto",    16 },
    { "recvspace",      "net.inet.tcp.recvspace",       16 },
    { "recvbuf_inc",    "net.inet.tcp.recvbuf_inc",     16 },
    { "recvbuf_max",    "net.inet.tcp.recvbuf_max",     16 },
    { "init_win",       "net.inet.tcp.init_win",        16 },
    { "init_win_local", "net.inet.tcp.init_win_local",  16 },
    { "delack_ticks",   "net.inet.tcp.delack_ticks",    16 },
    { "congctl",        "net.inet.tcp.congctl.selected", 0 }, // str val
};
#endif // MODIFY_NETWORK_PARAMETERS


//This is a struct that is required for the defer accept code to work
//it holds the pollfd and the time a connection was accepted
//so we can close a socket if it's been idle for too long
struct mw_pollfd
{
    struct pollfd poll_fd;
    struct timeval conn_tv;
};


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


static int
xe_net_init_socket( IN int SockFd )
{
    int rc = 0;
    socklen_t len = 0;

    if ( g_state.so_sndbuf )
    {
        len = sizeof( g_state.so_sndbuf );
        rc = setsockopt( SockFd, SOL_SOCKET, SO_SNDBUF,
                         &g_state.so_sndbuf, len );
        if ( rc )
        {
            MYASSERT( !"setsockopt" );
            goto ErrorExit;
        }
    }
    if ( g_state.so_rcvbuf )
    {
        len = sizeof( g_state.so_rcvbuf );
        rc = setsockopt( SockFd, SOL_SOCKET, SO_RCVBUF,
                         &g_state.so_rcvbuf, len );
        if ( rc )
        {
            MYASSERT( !"setsockopt" );
            goto ErrorExit;
        }
    }
    if ( g_state.so_sndtimeo.tv_sec || g_state.so_sndtimeo.tv_usec )
    {
        len = sizeof( g_state.so_sndtimeo );
        rc = setsockopt( SockFd, SOL_SOCKET, SO_SNDTIMEO,
                         &g_state.so_sndtimeo, len );
        if ( rc )
        {
            MYASSERT( !"setsockopt" );
            goto ErrorExit;
        }
    }
    if ( g_state.so_rcvtimeo.tv_sec || g_state.so_rcvtimeo.tv_usec )
    {
        len = sizeof( g_state.so_rcvtimeo );
        rc = setsockopt( SockFd, SOL_SOCKET, SO_RCVTIMEO,
                         &g_state.so_rcvtimeo, len );
        if ( rc )
        {
            MYASSERT( !"setsockopt" );
            goto ErrorExit;
        }
    }

ErrorExit:
    return rc;
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

    int rc = 0;

    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );

    MYASSERT( AF_INET == native_fam ||
              AF_INET6 == native_fam );

    MYASSERT( SOCK_STREAM == native_type );

    Response->base.status = 0;

    do
    {
        sockfd = socket( native_fam,
                         native_type,
                         native_proto );
        // XXXX: handle occasional error from socket()
    } while ( sockfd < 0 && ENOBUFS == errno );
    
    if ( sockfd < 0 )
    {
        Response->base.status = XE_GET_NEG_ERRNO();
        MYASSERT( !"socket" );
    }
    else
    {
        extsock = MW_SOCKET_CREATE( g_state.client_id, WorkerThread->idx );
    }

    // Set up Response; clobbers base.sockfd
    xe_net_set_base_response( (mt_request_generic_t *)Request,
                              MT_RESPONSE_SOCKET_CREATE_SIZE,
                              (mt_response_generic_t *)Response );

    // Init from global config
    rc = xe_net_init_socket( sockfd );
    if ( rc )
    {
        goto ErrorExit;
    }

    // Set up BufferItem->assigned_thread for future reference during
    // this session
    Response->base.sockfd        = extsock;
    WorkerThread->public_fd      = extsock;
    WorkerThread->local_fd       = sockfd;
    WorkerThread->sock_domain    = native_fam;
    WorkerThread->sock_type      = native_type;
    WorkerThread->sock_protocol  = Request->sock_protocol;

    ++g_state.network_stats_socket_ct;

    DEBUG_PRINT ( "**** Thread %d <== socket %lx / %d\n",
                  WorkerThread->idx, WorkerThread->public_fd, sockfd );
ErrorExit:
    return rc;
}


static int
xe_net_sock_fcntl( IN  mt_request_socket_attrib_t  * Request,
                   OUT mt_response_socket_attrib_t * Response,
                   IN  thread_item_t               * WorkerThread )
{
    MYASSERT( MtSockAttribNonblock == Request->name );

    int rc = 0;
    int err = 0;
    int flags = fcntl( WorkerThread->local_fd, F_GETFL );

    if ( Request->modify )
    {
        if ( Request->val.v32 ) { flags |= O_NONBLOCK; }
        else                    { flags &= ~O_NONBLOCK; }

        rc = fcntl( WorkerThread->local_fd, F_SETFL, flags );
        err = errno;
        MYASSERT( 0 == rc );
    }
    else
    {
        Response->val.v32 = (uint32_t) (flags & O_NONBLOCK);
    }

    errno = err;
    Response->base.status = rc ? XE_GET_NEG_ERRNO_VAL( err ) : 0;
    return rc;
}


int
xe_net_sock_attrib( IN  mt_request_socket_attrib_t  * Request,
                    OUT mt_response_socket_attrib_t * Response,
                    IN  thread_item_t               * WorkerThread )
{
    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );
    MYASSERT( 1 == WorkerThread->in_use );
    MYASSERT( WorkerThread->idx >= 0 );

    int rc    = 0;
    int level = SOL_SOCKET; // good for most cases
    int name  = 0;
    int err   = 0;
    // We have to manage the input/output buffer. If input, point to
    // Request; if output, point to response.

    bool timeval = false;
    // Unconditionally stuff the optval into this struct....
    struct timeval t = {0};
    // set to reasonable default values
    //mt_sockfeat_arg_t * optval = &Request->val.v32;
    socklen_t len = sizeof( Request->val.v32 );
    *(uint32_t *) &t = Request->val.v32;

    if ( MtSockAttribNonblock == Request->name )
    {
        rc = xe_net_sock_fcntl( Request, Response, WorkerThread );
        goto ErrorExit;
    }

    switch( Request->name )
    {
    case MtSockAttribReuseaddr:
        name = SO_REUSEADDR;
        break;
    case MtSockAttribReuseport:
        name = SO_REUSEPORT;
        break;
    case MtSockAttribKeepalive:
        name = SO_KEEPALIVE;
        break;
    case MtSockAttribNodelay:
        level = IPPROTO_TCP; //SOL_TCP;
        name  = TCP_NODELAY;
        break;
    case MtSockAttribSndBuf:
        name = SO_SNDBUF;
        break;
    case MtSockAttribRcvBuf:
        name = SO_RCVBUF;
        break;
    case MtSockAttribSndTimeo:
    case MtSockAttribRcvTimeo:
        timeval = true;
        name = (Request->name == MtSockAttribSndTimeo
                ? SO_SNDTIMEO : SO_RCVTIMEO );
        // Point arg to timeval; set t whether or not we modify
        t.tv_sec  = Request->val.t.s;
        t.tv_usec = Request->val.t.us;
        //optval = (void *) &t;
        len = sizeof( t );
        break;
    case MtSockAttribSndLoWat:
        name = SO_SNDLOWAT;
        break;
    case MtSockAttribRcvLoWat:
        name = SO_RCVLOWAT;
        break;
    case MtSockAttribGlobalCongctl:
    case MtSockAttribGlobalDelackTicks:
        // globals [ via sysctl() ]
        goto ErrorExit;
    case MtSockAttribDeferAccept:
        WorkerThread->defer_accept = true;
        rc = 0;
        goto ErrorExit;
    default:
        MYASSERT( !"Unrecognized attribute given" );
        rc = -EINVAL;
        goto ErrorExit;
    }

    DEBUG_PRINT ( "Worker thread %d (socket %lx / %d) is calling get/setsockopt"
                  "%d/%d/%d\n",
                  WorkerThread->idx,
                  WorkerThread->public_fd, WorkerThread->local_fd,
                  level, name, Request->val.v32 );

    if ( Request->modify ) // Set the feature's value
    {
        rc = setsockopt( WorkerThread->local_fd,
                         level, name,
                         (void *) &t, len );
    }
    else // Get the feature's value, put it into t
    {
        rc = getsockopt( WorkerThread->local_fd,
                         level, name,
                         (void *) &t, &len );
        if ( timeval )
        {
            Response->val.t.s  = t.tv_sec;
            Response->val.t.us = t.tv_usec;
        }
        else
        {
            bzero( &Response->val, sizeof(Response->val) );
            Response->val.v32 = *(uint32_t *) &t;
        }
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

    DEBUG_PRINT ( "Worker thread %d (socket %lx / %d) is connecting to %s:%d\n",
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

    DEBUG_PRINT ( "Worker thread %d (socket %lx / %d) is binding on %s:%d\n",
                  WorkerThread->idx,
                  WorkerThread->public_fd, WorkerThread->local_fd,
                  inet_ntoa( sockaddr.sin_addr ), ntohs(sockaddr.sin_port) );

    Response->base.status = bind( WorkerThread->local_fd,
                                  (const struct sockaddr*) &sockaddr,
                                  addrlen );
    if ( Response->base.status < 0 )
    {
        Response->base.status = XE_GET_NEG_ERRNO();
        perror( "bind" );
        MYASSERT ( !"bind" );
    }
    else
    {
        WorkerThread->bound_port_num = ntohs( sockaddr.sin_port );
        g_state.pending_port_change = true;
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

    DEBUG_PRINT ( "Worker thread %d (socket %lx / %d) is listening\n",
                  WorkerThread->idx,
                  WorkerThread->public_fd, WorkerThread->local_fd );

    Response->base.status = listen( WorkerThread->local_fd,
                                    Request->backlog);
    if( Response->base.status < 0 )
    {
        Response->base.status = XE_GET_NEG_ERRNO();
        perror( "listen" );
        MYASSERT( !"listen" );
    }
    
    xe_net_set_base_response( (mt_request_generic_t *)  Request,
                              MT_RESPONSE_SOCKET_LISTEN_SIZE,
                              (mt_response_generic_t *) Response);

    return 0;
}

void
xe_net_defer_accept_wait( void )
{
    struct timespec ts = {0,1000};

    while ( ts.tv_nsec > 0 )
    {
        (void) nanosleep( &ts, &ts );
    }
}


int
xe_net_idle_socket_timeout( struct mw_pollfd *MwPollFd )
{

    struct timeval curr_tv = {0};
    struct timeval res_tv  = {0};
    double elapsed = 0.0;
    int rc = 0;

    MYASSERT( MwPollFd );

    gettimeofday( &curr_tv, NULL );
    
    timersub( &curr_tv, &MwPollFd->conn_tv, &res_tv );

    elapsed = (double)res_tv.tv_sec + ( res_tv.tv_usec/1000000.0 );

    if ( elapsed >= DEFER_ACCEPT_MAX_IDLE )
    {
        DEBUG_PRINT( "socket: %d idle for more than 1 second, closing\n",
                     MwPollFd->poll_fd.fd );
        rc = 1;
        goto ErrorExit;
    }

ErrorExit:
    return rc;
}


//Checks status of socket by reading from the socket
//returns 0 for disconnect, 1 for data available and -1 for error
int
INS_DEBUG_ATTRIB
xe_net_check_idle_socket( struct mw_pollfd *MwPollFd )
{
    int rc = -1;
    int bytes_read = 0;
    char buf = 0;
    
    if( MwPollFd->poll_fd.fd == -1 )
    {
        rc = -1;
        goto ErrorExit;
    }


    //Check for socket timeout if timeout has passed, return 0
    if( xe_net_idle_socket_timeout( MwPollFd ) == 1 )
    {
        rc = 0;
        goto ErrorExit;
    }

    if( ( MwPollFd->poll_fd.revents & ( POLLIN | POLLRDNORM ) ) ||
        ( MwPollFd->poll_fd.revents == 0 ) )
    {
        
        bytes_read = recv( MwPollFd->poll_fd.fd, ( void * ) &buf, 1, MSG_PEEK );
        
        if( bytes_read > 0 )
        {
            rc = 1;
            goto ErrorExit;
        }

        if( bytes_read == 0 )
        {
            rc = 0;
            goto ErrorExit;
        }
        
        //For some reason read sometimes returns -1 and errno:0
        //when it seems like errno should be EAGAIN
        if( ( bytes_read < 0 && ( errno == EAGAIN ) ) ||
            ( bytes_read < 0 && ( errno == 0 ) ) )
        {
            rc = -1;
            goto ErrorExit;
        }


        //if we got here, this is not good
        DEBUG_PRINT("recieve error when checking for peer disconnect. rc: %d "
                    "errno: %d sockfd: %d\n",
                    bytes_read, errno, MwPollFd->poll_fd.fd );
        rc = -1;
    }

ErrorExit:
    DEBUG_PRINT( "xe_net_data_on_socket returning: %d socket: %d "
                 "revents: 0x%x errno: %d\n",
                 rc, MwPollFd->poll_fd.fd, MwPollFd->poll_fd.revents, errno );
    return rc;
}


int
INS_DEBUG_ATTRIB
xe_net_unset_sock_nonblock( int SockFd )
{
    int rc = 0;

    //Set flag to non blocking (xe_net_accept_socket will handle
    //accept4 flags
    rc = fcntl( SockFd, F_GETFL, 0 );
    if( rc < 0 )
    {
        rc = -EIO;
        perror("Could not get flags for fd in defer_accept_socket");
    }

    //Returns 0 on success
    rc = fcntl( SockFd, F_SETFL, ( rc & ~O_NONBLOCK ) );
    if( rc < 0 )
    {
        rc = -EIO;
        perror("Unsetting O_NONBLOCK failed");
    }

    return rc;
}
    

    
int
INS_DEBUG_ATTRIB
xe_net_defer_accept_socket( int LocalFd,
                            struct sockaddr * sockaddr,
                            socklen_t * addrlen )
{
    
    int rc = 0;
    struct pollfd listen_poll_fd = {0};

//For some reason, gdb will not work if we have thread local storage defined here
//if that is the case, use a single threaded version of apache and standard
//static variables
//    static int last_idx = 0;
//    static bool init = false;
//    static struct mw_pollfd mw_peer_poll_fds[ MAX_THREAD_COUNT ] = {0};

    static __thread int last_idx = 0;
    static __thread bool init = false;
    static __thread struct mw_pollfd mw_peer_poll_fds[ MAX_THREAD_COUNT ] = {0};
    

    struct pollfd peer_poll_fds[ MAX_THREAD_COUNT ] = {0};

    if( !init )
    {
        for( int i = 0; i < MAX_THREAD_COUNT; i++ )
        {
            mw_peer_poll_fds[i].poll_fd.fd = -1;
            mw_peer_poll_fds[i].poll_fd.events = POLLIN | POLLRDNORM;
        }
        
        init = true;
    }

    listen_poll_fd.fd = LocalFd;
    listen_poll_fd.events = POLLIN | POLLRDNORM | POLLHUP | POLLNVAL;
    listen_poll_fd.revents = 0;

    do
    {


        //Poll for incoming connection
        do
        {
            rc = poll( &listen_poll_fd, 1, 0 );
            if( rc < 0 )
            {
                perror("Polling listening socket failed");
                break;
            }

            if( listen_poll_fd.revents & POLLNVAL )
            {
                DEBUG_PRINT("Listening socket closed while defer accept was waiting");
                errno = -ENOENT;
                rc = -1;
                goto ErrorExit;
            }

            //Listen FD has incomming connection
            if( listen_poll_fd.revents & ( POLLIN | POLLRDNORM ) )
            {
                rc = paccept( LocalFd,
                              sockaddr,
                              addrlen,
                              NULL,
                              SOCK_NONBLOCK );
                if( rc < 0 )
                {
                    perror("Defer accept call to accept failed");
                    break;
                }

                //Add connection to peer pool
                for( int i = 0; i < MAX_THREAD_COUNT; i++ )
                {
                    if( mw_peer_poll_fds[i].poll_fd.fd == -1 )
                    {
                        gettimeofday( &mw_peer_poll_fds[i].conn_tv, NULL );
                        mw_peer_poll_fds[i].poll_fd.fd = rc;
                        mw_peer_poll_fds[i].poll_fd.revents = 0;
                        
                        DEBUG_PRINT("Added socket %d to defer accept list\n", rc );
                        break;
                    }
                    
                    if( i == ( MAX_THREAD_COUNT - 1 ) )
                    {
                        DEBUG_PRINT( "Socket count exceeded closing socket: %d\n", rc );
                        close( rc );
                    }
                }
            }
            
        } while( rc > 0 );

        //Copy mw_peer_poll_fds info into peer_poll_fds for poll call
        for( int i = 0; i < MAX_THREAD_COUNT; i++ )
        {
            peer_poll_fds[i].fd      = mw_peer_poll_fds[i].poll_fd.fd;
            peer_poll_fds[i].events  = mw_peer_poll_fds[i].poll_fd.events;
            peer_poll_fds[i].revents = mw_peer_poll_fds[i].poll_fd.revents;
        }

        rc = poll( peer_poll_fds, MAX_THREAD_COUNT, 0 );
        if( rc < 0 )
        {
            perror("Defer accept poll failed");
        }

        //Copy results of poll call into mw_peer_poll_fds
        for( int i = 0; i < MAX_THREAD_COUNT; i++ )
        {
            mw_peer_poll_fds[i].poll_fd.fd      = peer_poll_fds[i].fd;
            mw_peer_poll_fds[i].poll_fd.revents = peer_poll_fds[i].revents;
            mw_peer_poll_fds[i].poll_fd.events  = peer_poll_fds[i].events;
        }

        //first check for sockets that we already know have data
        for( int i=0; i < MAX_THREAD_COUNT; i++ )
        {
            last_idx = last_idx % MAX_THREAD_COUNT;

            if( mw_peer_poll_fds[last_idx].poll_fd.fd == -1 )
            {
                last_idx++;
                continue;
            }

            rc = xe_net_check_idle_socket( &mw_peer_poll_fds[last_idx] );
            
            if( rc == 1 )
            {
                rc = mw_peer_poll_fds[last_idx].poll_fd.fd;

                xe_net_unset_sock_nonblock( mw_peer_poll_fds[last_idx].poll_fd.fd );

                mw_peer_poll_fds[last_idx].poll_fd.revents = 0;
                mw_peer_poll_fds[last_idx].poll_fd.fd      = -1;

                last_idx++;

                goto ErrorExit;
            }

            if( rc == 0 )
            {
                
                DEBUG_PRINT("Defer accept peer disconnect detected, local fd: %d\n",
                            mw_peer_poll_fds[last_idx].poll_fd.fd );
                
                //Peer has disconnected
                close( mw_peer_poll_fds[last_idx].poll_fd.fd );
                
                mw_peer_poll_fds[last_idx].poll_fd.fd = -1;
                mw_peer_poll_fds[last_idx].poll_fd.revents = 0;

                last_idx++;

                continue;
            }

            if( mw_peer_poll_fds[last_idx].poll_fd.revents &
                ( POLLNVAL | POLLERR | POLLHUP ) )
            {
                close( mw_peer_poll_fds[last_idx].poll_fd.fd );
                
                DEBUG_PRINT( "Error: Closing socket %d revents: %x\n",
                             mw_peer_poll_fds[last_idx].poll_fd.fd,
                             mw_peer_poll_fds[last_idx].poll_fd.revents );

                mw_peer_poll_fds[last_idx].poll_fd.fd      = -1;
                mw_peer_poll_fds[last_idx].poll_fd.revents = 0;

                last_idx++;

                continue;
            }
            
            last_idx++;
        }
        
        xe_net_defer_accept_wait();

    } while( true );

ErrorExit:
    DEBUG_PRINT( "Defer accept returning with value: %d\n", rc );
    return rc;
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

    DEBUG_PRINT ( "Worker thread %d (socket %lx/%d) is accepting.\n",
                  WorkerThread->idx,
                  WorkerThread->public_fd, WorkerThread->local_fd );

    // NetBSD does not implement accept4. Therefore the flags are
    // ignored here. However, they are copied into the response for
    // PVM usage.
    Response->flags = 0;
    if ( Request->flags )
    {
        DEBUG_PRINT( "Not observing PVM flags 0x%x in accept()\n", Request->flags );
        Response->flags = Request->flags;
    }

    if( WorkerThread->defer_accept )
    {
        sockfd = xe_net_defer_accept_socket( WorkerThread->local_fd,
                                             (struct sockaddr *) &sockaddr,
                                             (socklen_t *) &addrlen );
    }
    else
    {
        sockfd = accept( WorkerThread->local_fd,
                         (struct sockaddr *) &sockaddr,
                         (socklen_t *) &addrlen );
    }
    if ( sockfd < 0 )
    {
        Response->base.status = XE_GET_NEG_ERRNO();
        perror( "accept" );
        // N.B. Response->base.sockfd is set by xe_net_set_base_response()
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


        // Init from global config
        rc = xe_net_init_socket( sockfd );
        if ( rc )
        {
            goto ErrorExit;
        }

        // Caller must fix up the socket assignments
        Response->base.status = sockfd;

        ++g_state.network_stats_socket_ct;

        populate_mt_sockaddr_in( &Response->sockaddr, &sockaddr );

        DEBUG_PRINT ( "Worker thread %d (socket %lx / %d) accepted from %s:%d\n",
                      WorkerThread->idx,
                      WorkerThread->public_fd, WorkerThread->local_fd,
                      inet_ntoa( sockaddr.sin_addr ), ntohs(sockaddr.sin_port) );

    }

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
    ssize_t         callrc = 0;
    bool            polled = false;    
    int              flags = xe_net_translate_msg_flags( Request->flags );

    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );
    MYASSERT( WorkerThread->public_fd == Request->base.sockfd );
    
    Response->count       = 0;
    Response->base.status = 0;

    DEBUG_PRINT ( "Worker thread %d (socket %lx / %d) is recvfrom() 0x%x bytes\n",
                  WorkerThread->idx,
                  WorkerThread->public_fd, WorkerThread->local_fd,
                  Request->requested );

    while( true )
    {
        do
        {
            callrc = recvfrom( WorkerThread->local_fd,
                               (void *) Response->bytes,
                               Request->requested,
                               flags,
                               ( struct sockaddr * ) &src_addr,
                               &addrlen );
        } while( callrc < 0 && EINTR == errno );

        // recvfrom() returned without being interrupted

        if ( callrc < 0 )
        {
            // recvfrom() failed
            Response->base.status = XE_GET_NEG_ERRNO();
            Response->count       = 0;
            break;
        }

        // Success
        Response->base.status = 0;
        Response->count       = callrc;

        if ( Response->count > 0 ) break;

        // recvfrom() returned 0. Check for remote close.

        // Check for events from previous loop: there was supposed
        // to be data, but we didn't read any. The connection was
        // closed on the other end.
        if ( events & (POLLIN | POLLRDNORM) )
        {
            WorkerThread->state_flags |= _MT_FLAGS_REMOTE_CLOSED;
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
        
        // poll() has not been invoked yet. Invoke it and check for
        // results again. A failure here counts as an internal error.
        rc = xe_pollset_query_one( WorkerThread->local_fd, &events );
        if ( rc ) goto ErrorExit;

        polled = true;
    } // while

    DEBUG_PRINT( "recvfrom() got total of 0x%x bytes, status=%d\n",
                 (int)Response->count, Response->base.status );

    populate_mt_sockaddr_in( &Response->src_addr, &src_addr );

    Response->addrlen  = addrlen;

    g_state.network_stats_bytes_recv += Response->count;

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
    ssize_t         callrc = 0;
    bool            polled = false;    
    int              flags = xe_net_translate_msg_flags( Request->flags );
    
    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );
    MYASSERT( NULL != WorkerThread );

    Response->count       = 0;
    Response->base.status = 0;

    MYASSERT( WorkerThread->public_fd == Request->base.sockfd );

    DEBUG_PRINT ( "Worker thread %d (socket %lx / %d) is receiving 0x%x bytes\n",
                  WorkerThread->idx,
                  WorkerThread->public_fd, WorkerThread->local_fd,
                  Request->requested );

    while ( true )
    {
        do
        {
            callrc = recv( WorkerThread->local_fd,
                           &Response->bytes[ Response->count ],
                           Request->requested - Response->count,
                           flags );
        } while( callrc < 0 && EINTR == errno );

        // recv() returned without being interrupted

        if ( callrc < 0 )
        {
            // recv() failed
            Response->base.status = XE_GET_NEG_ERRNO();
            Response->count       = 0;
            break;
        }

        // Success
        Response->base.status = 0;
        Response->count       = callrc;

        if ( Response->count > 0 ) break;

        // recv() returned 0. Check for remote close.
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
        
        // poll() has not been invoked yet. Invoke it and check for
        // results again. A failure here counts as an internal error.
        rc = xe_pollset_query_one( WorkerThread->local_fd, &events );
        if ( rc ) goto ErrorExit;

        polled = true;
    } // while

    DEBUG_PRINT( "recv() got total of 0x%x bytes, status=%d\n",
                 (int)Response->count, Response->base.status );

    g_state.network_stats_bytes_recv += Response->count;

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
        DEBUG_PRINT ( "Worker thread %d (socket %lx/%d) is closing\n",
                      WorkerThread->idx,
                      WorkerThread->public_fd, WorkerThread->local_fd );

        --g_state.network_stats_socket_ct;

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
    if ( 0 != WorkerThread->bound_port_num )
    {
        WorkerThread->bound_port_num = 0;
        g_state.pending_port_change = true;
    }
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

    DEBUG_PRINT ( "Worker thread %d (socket %lx / %d) is sending %ld bytes\n",
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

    g_state.network_stats_bytes_sent += Response->count;

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


#if MODIFY_NETWORK_PARAMETERS
static int
xe_net_set_net_param_int( const char * Name,
                          int          NewVal,
                          bool         Set   )
{
    int rc = 0;
    int oldval = 0;
    size_t oldlen = sizeof(oldval);
    size_t newlen = sizeof(NewVal);

    if ( !Set )
    {
        NewVal = 0;
        newlen = 0;
    }

    rc = sysctlbyname( Name, &oldval, &oldlen, &NewVal, newlen );
    if ( rc )
    {
        DEBUG_PRINT( "sysctlbyname failed on %s\n", Name );
        MYASSERT( !"sysctlbyname" );
        goto ErrorExit;
    }

    if ( Set )
    {
        FORCE_PRINT( "%*s: curr val 0x%x, prev val 0x%x\n",
                     XE_NET_NETWORK_PARAM_WIDTH, Name, NewVal, oldval );
    }
    else
    {
        FORCE_PRINT( "%*s: curr val 0x%x\n",
                     XE_NET_NETWORK_PARAM_WIDTH, Name, oldval );
    }

ErrorExit:
    return rc;
}


static int
xe_net_set_net_param_str( const char * Name,
                          const char * NewVal,
                          bool         Set   )
{
    int rc = 0;
    char oldval[64] = {0};
    size_t oldlen = sizeof( oldval );
    size_t newlen = 0;

    if ( !Set )
    {
        NewVal = NULL;
        newlen = 0;
    }
    else if ( NewVal )
    {
        newlen = strlen( NewVal ) + 1;
    }

    rc = sysctlbyname( Name, oldval, &oldlen, NewVal, newlen );
    if ( rc )
    {
        DEBUG_PRINT( "sysctlbyname failed on %s\n", Name );
        MYASSERT( !"sysctlbyname" );
        goto ErrorExit;
    }

    if ( Set )
    {
        FORCE_PRINT( "%*s: curr val %s, prev val %s\n",
                     XE_NET_NETWORK_PARAM_WIDTH, Name, NewVal, oldval );
    }
    else
    {
        FORCE_PRINT( "%*s: curr val %s\n",
                     XE_NET_NETWORK_PARAM_WIDTH, Name, oldval );
    }

ErrorExit:
    return rc;
}


static void
xe_net_get_options( void )
{
    for ( int i = 0; i < NUMBER_OF( g_net_params ); ++i )
    {
        if ( g_net_params[i].base )
        {
            (void) xe_net_set_net_param_int( g_net_params[i].path, 0, false );
        }
        else
        {
            (void) xe_net_set_net_param_str( g_net_params[i].path, NULL, false );
        }
    }
}
#endif // MODIFY_NETWORK_PARAMETERS


int
xe_net_init( void )
{
    int rc = 0;

#if MODIFY_NETWORK_PARAMETERS
    char params[ INS_SOCK_PARAMS_MAX_LEN ] = {0};
    int tries = 0;

    xe_net_get_options();

    // XXXX: wait and try again, or just fail?
    //DEBUG_BREAK();
    do
    {
        struct timespec ts = {0, 500}; // 0 seconds, N nanoseconds

        rc = ioctl( g_state.input_fd, INS_GET_SOCK_PARAMS_IOCTL, params );
        if ( 0 == rc ) { break; }

        (void) nanosleep( &ts, &ts );
    } while ( tries++ < 10 );
    
    if ( 0 != rc )
    {
        MYASSERT( !"Failed to get socket parameters" );
        rc = 0; // not fatal
        goto ErrorExit;
    }

    char * pparams = params;
    char * toptok_sav = NULL;
    while( true )
    {
        // Split apart by spaces
        char * toptok = strtok_r( pparams, " ", &toptok_sav );
        if ( NULL == toptok ) { break; }

        // Copy of the top token that we'll destroy
        char param[ INS_SOCK_PARAMS_MAX_LEN ];
        char * pparam = param;

        // Further calls to strtok should use NULL
        pparams = NULL;

        strncpy( param, toptok, sizeof(param) );
        char * name = strsep( &pparam, ":" );
        char * strval = strsep( &pparam, ":" );

        bool found = false;
        for ( int i = 0; i < NUMBER_OF( g_net_params ); ++i )
        {
            if ( 0 != strcmp( g_net_params[i].key, name ) ) { continue; }

            found = true;
            if ( g_net_params[i].base )
            {
                int val = strtol( strval, NULL, g_net_params[i].base );
                rc = xe_net_set_net_param_int( g_net_params[i].path, val, true );
            }
            else
            {
                rc = xe_net_set_net_param_str( g_net_params[i].path, strval, true );
            }
        }

        if ( !found )
        {
            rc = ENOENT;
            MYASSERT( !"Network parameter not found" );
            goto ErrorExit;
        }
    }

ErrorExit:
#endif
    MYASSERT( 0 == rc );
    return rc;
}
