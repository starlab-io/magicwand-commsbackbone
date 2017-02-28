/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

/**
 * @file    wrap_test_server.c
 * @author  Mark Mason 
 * @date    10 September 2016
 * @version 0.1
 * @brief   A shared library pre-loaded by test TCP/IP apps that intercepts 
 *          network calls.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdarg.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>

#include <sys/epoll.h>
#include <poll.h>

#include <sys/uio.h>

#include <message_types.h>
#include <translate.h>
#include <user_common.h>

#include "epoll_wrapper.h"



#define DEV_FILE "/dev/mwchar"

static int devfd = -1; // FD to MW device
static int dummy_socket = -1; // socket for get/setsockopt

static void * g_dlh_libc = NULL;

static int
(*libc_socket)(int domain, int type, int protocol);

static int
(*libc_read)(int fd, void *buf, size_t count);

static ssize_t
(*libc_readv)(int fd, const struct iovec *iov, int iovcnt);

static int
(*libc_write)(int fd, const void *buf, size_t count);

static ssize_t
(*libc_writev)(int fd, const struct iovec *iov, int iovcnt);

static int
(*libc_close)(int fd);

static ssize_t
(*libc_send)(int sockfd, const void* buf, size_t len, int flags);

static ssize_t
(*libc_sendto)(int sockfd,
               const void* buf,
               size_t len,
               int flags,
               const struct sockaddr* dest_addr,
               socklen_t addrlen);

static ssize_t
(*libc_recv)(int sockfd,
             void* buf,
             size_t len,
             int flags);

static ssize_t
(*libc_recvfrom)(int sockfd,
                 void* buf,
                 size_t len,
                 int flags,
                 struct sockaddr* src_addr,
                 socklen_t* addrlen);

static int 
(*libc_getsockopt)( int Fd,
                    int Level,
                    int OptName,
                    void * OptVal,
                    socklen_t  * OptLen );

static int
(*libc_setsockopt)( int Fd,
                    int Level,
                    int OptName,
                    const void * OptVal,
                    socklen_t OptLen );

static int
(*libc_getsockname)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

static int
(*libc_getpeername)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);


static int
(*libc_epoll_create)( int Size );

static int
(*libc_epoll_create1)( int Flags );


static int
(*libc_epoll_ctl)( int EpFd,
                   int Op,
                   int Fd,
                   struct epoll_event * Event );

static int
(*libc_epoll_wait)( int EpFd,
                    struct epoll_event * Events,
                    int MaxEvents,
                    int Timeout );

static int
(*libc_epoll_pwait)( int EpFd,
                     struct epoll_event * Events,
                     int MaxEvents,
                     int Timeout,
                     const sigset_t * Sigmask );

static int
(*libc_poll)(struct pollfd *Fds, nfds_t Nfds, int Timeout);


static int
(*libc_fcntl)(int fd, int cmd, ... );


static void *
get_libc_symbol( void ** Addr, const char * Symbol )
{
    dlerror();
    *Addr = dlsym( g_dlh_libc, Symbol );
    if ( NULL == *Addr )
    {
        DEBUG_PRINT( "Failure: %s\n", dlerror() );
    }

    return *Addr;
}


static ssize_t
read_response( mt_response_generic_t * Response )
{
    ssize_t rc = 0;

    while ( 1 )
    {
        rc = libc_read( devfd, Response, sizeof(*Response) );
        if ( rc < 0 && EINTR == errno )
        {
            // Call was interrupted
            DEBUG_PRINT( "*** read() was interrupted. Trying again.\n" );
            continue;
        }

        // Otherwise, give up
        break;
    }

    if ( rc > 0 && IS_CRITICAL_ERROR( Response->base.status ) )
    {
        DEBUG_PRINT( "Remote side encountered critical error %x, ID=%lx FD=%x\n",
                     (int)Response->base.status,
                     (unsigned long)Response->base.id,
                     Response->base.sockfd );
        rc = -1;
        Response->base.status = -EIO;
    }

    return rc;
}


void
build_create_socket( mt_request_generic_t * Request )
{
    mt_request_socket_create_t * create = &(Request->socket_create);
    
    bzero( Request, sizeof(*Request) );

    create->base.sig = MT_SIGNATURE_REQUEST;
    create->base.type = MtRequestSocketCreate;
    create->base.size = MT_REQUEST_SOCKET_CREATE_SIZE;
    create->base.id = MT_ID_UNSET_VALUE;
    create->base.sockfd = 0;

    create->sock_fam = MT_PF_INET;
    create->sock_type = MT_ST_STREAM;
    create->sock_protocol = 0;
}

void
build_close_socket( mt_request_generic_t * Request, 
                    int                    SockFd )
{
    mt_request_socket_close_t * csock = &(Request->socket_close);

    bzero( Request, sizeof(*Request) );

    csock->base.sig  = MT_SIGNATURE_REQUEST;
    csock->base.type = MtRequestSocketClose;
    csock->base.size = MT_REQUEST_SOCKET_CLOSE_SIZE; 
    csock->base.id = MT_ID_UNSET_VALUE;
    csock->base.sockfd = SockFd;
}



void
build_bind_socket( mt_request_generic_t * Request, 
                   int                    SockFd, 
                   struct sockaddr_in   * SockAddr, 
                   socklen_t              Addrlen )
{

    mt_request_socket_bind_t * bind = &(Request->socket_bind);

    bzero( Request, sizeof(*Request) );

    populate_mt_sockaddr_in( &bind->sockaddr, SockAddr );

    bind->base.sig  = MT_SIGNATURE_REQUEST;
    bind->base.type = MtRequestSocketBind;
    bind->base.id = MT_ID_UNSET_VALUE;
    bind->base.sockfd = SockFd;

    bind->base.size = MT_REQUEST_SOCKET_BIND_SIZE; 
}


void
build_listen_socket( mt_request_generic_t * Request,
                     int                    SockFd,
                     int                  * backlog)
{
    mt_request_socket_listen_t * listen = &(Request->socket_listen);
    
    bzero( Request, sizeof(*Request) );

    listen->backlog = *backlog;
    listen->base.size = MT_REQUEST_SOCKET_LISTEN_SIZE;

    listen->base.sig = MT_SIGNATURE_REQUEST;
    listen->base.type = MtRequestSocketListen;
    listen->base.id = MT_ID_UNSET_VALUE;
    listen->base.sockfd = SockFd;

    listen->base.size = MT_REQUEST_SOCKET_LISTEN_SIZE;
}

void build_accept_socket( mt_request_generic_t * Request,
                          int                    SockFd)
{
    mt_request_socket_accept_t * accept = &(Request->socket_accept);

    bzero( Request, sizeof(*Request) );

    accept->base.sig = MT_SIGNATURE_REQUEST;
    accept->base.type = MtRequestSocketAccept;
    accept->base.id = MT_ID_UNSET_VALUE;
    accept->base.sockfd = SockFd;

    accept->base.size = MT_REQUEST_SOCKET_ACCEPT_SIZE;
}


void
build_connect_socket( mt_request_generic_t * Request, 
                      int SockFd,
                      struct sockaddr_in *SockAddr )
{
    mt_request_socket_connect_t * connect = &(Request->socket_connect);

    bzero( Request, sizeof(*Request) );

    connect->base.sig  = MT_SIGNATURE_REQUEST;
    connect->base.type = MtRequestSocketConnect;
    connect->base.id = MT_ID_UNSET_VALUE;
    connect->base.sockfd = SockFd;

    populate_mt_sockaddr_in( &Request->socket_connect.sockaddr, SockAddr );
    
    connect->base.size = MT_REQUEST_SOCKET_CONNECT_SIZE;
}

void
build_send_socket( mt_request_generic_t * Request, 
                   int                    SockFd,
                   const void           * Bytes,
                   size_t                 Len )
{
    mt_request_socket_send_t * send = &(Request->socket_send);
    size_t actual_len = Len;

    bzero( Request, sizeof(*Request) );

    send->base.sig  = MT_SIGNATURE_REQUEST;
    send->base.type = MtRequestSocketSend;
    send->base.id = MT_ID_UNSET_VALUE;
    send->base.sockfd = SockFd;
    
    if( Len > MESSAGE_TYPE_MAX_PAYLOAD_LEN )
    {
        actual_len = MESSAGE_TYPE_MAX_PAYLOAD_LEN;
    }

    memcpy(send->bytes, Bytes, actual_len);

    send->base.size = MT_REQUEST_SOCKET_SEND_SIZE + actual_len;
}


void
build_poll_create( mt_request_poll_create_t * Request )
{
    bzero( Request, sizeof(*Request) );

    Request->base.sig  = MT_SIGNATURE_REQUEST;
    Request->base.type = MtRequestPollCreate;
    Request->base.id   = MT_ID_UNSET_VALUE;
    Request->base.sockfd = MT_INVALID_FD;
    Request->base.size  = MT_REQUEST_POLL_CREATE_SIZE;
}

void
build_poll_close( mt_request_poll_close_t * Request,
                  mw_fd_t PollFd )
{
    bzero( Request, sizeof(*Request) );

    Request->base.sig  = MT_SIGNATURE_REQUEST;
    Request->base.type = MtRequestPollClose;
    Request->base.id   = MT_ID_UNSET_VALUE;
    Request->base.sockfd = PollFd;
    Request->base.size  = MT_REQUEST_POLL_CLOSE_SIZE;
}


void
build_poll_wait( mt_request_poll_wait_t * Request,
                 epoll_request_t        * Epoll,
                 int                      Timeout)
{
    MYASSERT( Epoll->fdct > 0 );

    bzero( Request, sizeof(*Request) );

    Request->base.sig  = MT_SIGNATURE_REQUEST;
    Request->base.type = MtRequestPollWait;
    Request->base.id   = MT_ID_UNSET_VALUE;

    // Use the sockfd we already have from our call to epoll_create()
    Request->base.sockfd = Epoll->pseudofd;
    Request->timeout = Timeout;
    
    for ( int i = 0; i < MAX_POLL_FD_COUNT; ++i )
    {
        uint32_t * reqflags = &Request->pollinfo[ i ].events;
        *reqflags = 0;

        // Skip deleted items
        if ( Epoll->fds[i] == MT_INVALID_SOCKET_FD )
        {
            continue;
        }

        Request->pollinfo[ i ].sockfd = Epoll->fds[ i ];
        ++Request->count;

        // Ignore events[i].data; that's user-specified data

        if ( Epoll->events[i].events & EPOLLIN )     *reqflags |= MW_POLLIN;
        if ( Epoll->events[i].events & EPOLLPRI )    *reqflags |= MW_POLLPRI;
        if ( Epoll->events[i].events & EPOLLOUT )    *reqflags |= MW_POLLOUT;
        if ( Epoll->events[i].events & EPOLLRDNORM ) *reqflags |= MW_POLLRDNORM;
        if ( Epoll->events[i].events & EPOLLWRNORM ) *reqflags |= MW_POLLWRNORM;
        if ( Epoll->events[i].events & EPOLLRDBAND ) *reqflags |= MW_POLLRDBAND;
        if ( Epoll->events[i].events & EPOLLWRBAND ) *reqflags |= MW_POLLWRBAND;
        if ( Epoll->events[i].events & EPOLLERR )    *reqflags |= MW_POLLERR;
        if ( Epoll->events[i].events & EPOLLHUP )    *reqflags |= MW_POLLHUP;
//        if ( Epoll->events[i].events & EPOLLNVAL )   *reqflags |= MW_POLLNVAL;
    }

    Request->base.size = MT_REQUEST_POLL_WAIT_SIZE
        + Request->count * MT_POLL_INFO_SIZE;
}

int
populate_epoll_results( IN  mt_response_poll_wait_t * Response,
                        IN  epoll_request_t         * Epoll,
                        OUT struct epoll_event      * Events )
{
    int ct = 0;
    mt_response_poll_wait_t * r = Response; // alias for readability

    //
    // For each item, see if any events were reported. If so, add its
    // entry to the Events array.
    //
    for ( int i = 0; i < r->count; ++i )
    {
        uint32_t * destflags = &Events[ct].events;
        *destflags = 0;

        //
        // In cases where events are available, Events[i] is populated
        // with the original data from Epoll
        //
        if ( 0 == r->pollinfo[i].events )
        {
            // No events to report
            continue;
        }

        // Events are available. Copy the user data and translate the flags.
        Events[ct].data = Epoll->events[ i ].data;
        
        if ( r->pollinfo[i].events & MW_POLLIN )     *destflags |= EPOLLIN;
        if ( r->pollinfo[i].events & MW_POLLPRI )    *destflags |= EPOLLPRI;
        if ( r->pollinfo[i].events & MW_POLLOUT )    *destflags |= EPOLLOUT;
        if ( r->pollinfo[i].events & MW_POLLRDNORM ) *destflags |= EPOLLRDNORM;
        if ( r->pollinfo[i].events & MW_POLLWRNORM ) *destflags |= EPOLLWRNORM;
        if ( r->pollinfo[i].events & MW_POLLRDBAND ) *destflags |= EPOLLRDBAND;
        if ( r->pollinfo[i].events & MW_POLLWRBAND ) *destflags |= EPOLLWRBAND;
        if ( r->pollinfo[i].events & MW_POLLERR )    *destflags |= EPOLLERR;
        if ( r->pollinfo[i].events & MW_POLLHUP )    *destflags |= EPOLLHUP;
//        if ( r->pollinfo[i].events & MW_POLLNVAL )   *destflags |= EPOLLNVAL;

        // Record the result
        ++ct;
    }

    MYASSERT( r->base.status < 0
              || ct == r->base.status );
    return ct;
}


// Perform steps up to epoll_wait as described in Request. Only the
// local system is modified; no MW requests are generated.
static int
do_epoll( epoll_request_t * Epoll,
          struct epoll_event * Events,
          int MaxEvents,
          int Timeout )
{
    int fd = -1;
    int count = 0;
    
    fd = libc_epoll_create(1);
    if ( fd < 0 )
    {
        count = -1;
        goto ErrorExit;
    }

    for ( int i = 0; i < Epoll->fdct; ++i )
    {
        // Struct copy
        Events[i] = Epoll->events[i];
    }        
    count = libc_epoll_wait( fd, Events, MaxEvents, Timeout );
    
ErrorExit:
    if ( fd > 0 )
    {
        libc_close( fd );
    }
    return count;        
}

int 
socket( int domain, 
        int type, 
        int protocol )
{
   mt_request_generic_t  request;
   mt_response_generic_t response;
   ssize_t rc = 0;

   if( AF_INET != domain )
   {
       rc = libc_socket( domain, type, protocol );
       goto ErrorExit;
   }
   
   // XXXX: args ignored
   build_create_socket( &request );

   DEBUG_PRINT("Sending socket-create request\n");

#ifndef NODEVICE

   if ( ( rc = libc_write( devfd, &request, sizeof( request ) ) ) < 0 )
   {
       goto ErrorExit;
   }

   if ( ( rc = read_response( (mt_response_generic_t *) &response ) ) < 0 )
   {
       goto ErrorExit;
   }
    
#endif

   if ( (int)response.base.status < 0 )
   {
       DEBUG_PRINT( "Error creating socket. Error Number: %x (%d)\n",
                    (int)response.base.status, (int)response.base.status );
       errno = -response.base.status;

       rc = -1;
       goto ErrorExit;
   }

   DEBUG_PRINT( "Returning socket 0x%x\n", response.base.sockfd );
   
    rc = (int)response.base.sockfd;

ErrorExit:

   return rc;
}

int
close( int Fd )
{
    mt_request_generic_t  request;
    mt_response_generic_t response;
    ssize_t rc = 0;

    if ( MW_SOCKET_IS_FD( Fd ) )
    {
        build_close_socket( &request, Fd );
        DEBUG_PRINT( "Closing MW Socket %x\n", Fd );
    
#ifndef NODEVICE
        if ( ( rc = libc_write( devfd, &request, sizeof( request ) ) ) < 0 )
        {
            goto ErrorExit;
        }
    
        if ( ( rc = read_response( (mt_response_generic_t *) &response ) ) < 0 )
        {
            goto ErrorExit;
        }
#endif
        if ( response.base.status )
        {
            DEBUG_PRINT( "\t\tError closing socket. Error Number: %lu\n",
                         (long) -response.base.status );
            errno = -response.base.status;
            // Returns -1 on error
            rc = -1;
            goto ErrorExit;
        }

        // Returns 0 on success
        rc = 0;
    }
    else if ( MW_EPOLL_IS_FD( Fd ) )
    {
        DEBUG_PRINT( "Closing epoll FD %x\n", Fd );

        epoll_request_t * req = mw_epoll_find( Fd );
        if ( NULL == req )
        {
            rc = -1;
            goto ErrorExit;
        }

        build_poll_close( (mt_request_poll_close_t *)&request,
                          req->pseudofd );
        mw_epoll_destroy( req );
        
#ifndef NODEVICE
        if ( ( rc = libc_write( devfd, &request, sizeof( request ) ) ) < 0 )
        {
            goto ErrorExit;
        }
    
        if ( ( rc = read_response( (mt_response_generic_t *) &response ) ) < 0 )
        {
            goto ErrorExit;
        }
#endif
        if ( response.base.status )
        {
            DEBUG_PRINT( "\t\tError closing poll FD. Error Number: %lu\n",
                         (long)response.base.status );
            errno = -response.base.status;
            rc = -1;
            goto ErrorExit;
        }
    }
    else
    {
        rc = libc_close( Fd );
        goto ErrorExit;
    }

ErrorExit:
    return rc;
}


int
bind( int SockFd,
      const struct sockaddr * SockAddr, 
      socklen_t addrlen )
{
    mt_request_generic_t request;
    mt_response_generic_t response;
    struct sockaddr_in * sockaddr_in;
    ssize_t rc = 0;
    
    if ( !MW_SOCKET_IS_FD(SockFd) )
    {
        DEBUG_PRINT("Socket file discriptor value invalid\n");
        errno = ENOTSOCK;
        rc = -1;
        goto ErrorExit;
    }

    if ( SockAddr->sa_family != AF_INET
         || addrlen != sizeof(struct sockaddr_in) )
    {
        perror("Only AF_INET is supported at this time\n");
        errno = EINVAL;
        rc =  -1;
        goto ErrorExit;
    }

    sockaddr_in = ( struct sockaddr_in * ) SockAddr;

    build_bind_socket( &request, SockFd, sockaddr_in, addrlen);

#ifndef NODEVICE    
    if ( ( rc = libc_write( devfd, &request, sizeof( request ) ) ) < 0 )
    {
        goto ErrorExit;
    }
    
    if ( ( rc = read_response( (mt_response_generic_t *) &response ) ) < 0 )
    {
        goto ErrorExit;
    }
#endif

    rc = response.base.status;

ErrorExit:
    return rc;
}


int
listen( int SockFd, int backlog )
{
    mt_request_generic_t request;
    mt_response_generic_t response;
    ssize_t rc = 0;
    
    if ( !MW_SOCKET_IS_FD( SockFd ) )
    {
        DEBUG_PRINT("Socket file discriptor value invalid\n");
        errno = ENOTSOCK;
        rc = -1;
        goto ErrorExit;
    }

    build_listen_socket( &request, SockFd, &backlog);

#ifndef NODEVICE
    if ( ( rc = libc_write( devfd, &request, sizeof( request ) ) ) < 0 )
    {
        goto ErrorExit;
    }

    if ( ( rc = read_response( (mt_response_generic_t *) &response ) ) < 0 )
    {
        goto ErrorExit;
    }

#endif

    rc = response.base.status;
    if ( response.base.status < 0 )
    {
        DEBUG_PRINT( "Returning error response with errno=%d\n", -response.base.status );
        errno = -response.base.status;
        rc = -1;
    }

ErrorExit:
    return rc;
}


int
accept( int SockFd, 
        struct sockaddr * SockAddr, 
        socklen_t * SockLen)
{
    mt_request_generic_t request;
    mt_response_generic_t response;
    ssize_t rc = 0;
    
    if ( !MW_SOCKET_IS_FD( SockFd ) )
    {
        DEBUG_PRINT("Socket file discriptor value invalid");
        errno = ENOTSOCK;
        rc = -1;
        goto ErrorExit;
    }
    
    build_accept_socket(&request, SockFd);
    populate_sockaddr_in( (struct sockaddr_in *)SockAddr,
                          &response.socket_accept.sockaddr);
    
#ifndef NODEVICE
    
    if ( ( rc = libc_write( devfd, &request, sizeof( request ) ) ) < 0 )
    {
        goto ErrorExit;
    }
    
    if ( ( rc = read_response( (mt_response_generic_t *) &response ) ) < 0 )
    {
        rc = -1;
        goto ErrorExit;
    }
#endif

    if ( response.base.status < 0 )
    {
        errno = -response.base.status;
        rc = -1;
        goto ErrorExit;
    }

    rc = response.base.status;

ErrorExit:
    return rc;
}


int
accept4( int               SockFd,
         struct sockaddr * SockAddr,
         socklen_t       * SockLen,
         int               Flags )
{
    // Drop flags for now
    return accept( SockFd, SockAddr, SockLen );
}


void
build_recv_socket( mt_request_generic_t * Request, 
                   int SockFd,
                   size_t Len,
                   int Flags,
                   struct sockaddr *SrcAddr,
                   socklen_t *AddrLen )
{
   mt_request_socket_recv_t * recieve = &(Request->socket_recv);

    bzero( Request, sizeof( *Request ) );
    
    recieve->base.sig  = MT_SIGNATURE_REQUEST;
    recieve->base.id = MT_ID_UNSET_VALUE;
    recieve->base.sockfd = SockFd;

    if( NULL == SrcAddr )
    {
        recieve->base.type = MtRequestSocketRecv;
    }
    else
    {
        recieve->base.type = MtRequestSocketRecvFrom;
    }

    if ( Len > MESSAGE_TYPE_MAX_PAYLOAD_LEN )
    {
       Len = MESSAGE_TYPE_MAX_PAYLOAD_LEN;
    } 

    recieve->requested = MIN( Len, MESSAGE_TYPE_MAX_PAYLOAD_LEN );
    recieve->flags     = Flags;
    
    recieve->base.size = MT_REQUEST_SOCKET_RECV_SIZE;
}

ssize_t
recvfrom( int    SockFd,
          void * Buf,
          size_t Len,
          int    Flags,
          struct sockaddr * SrcAddr,
          socklen_t       * AddrLen )
{
    mt_request_generic_t request;
    mt_response_generic_t response;
    
    bzero( &response, sizeof(response) );

    ssize_t rc = 0;

    if ( !MW_SOCKET_IS_FD(SockFd) )
    {
        DEBUG_PRINT("Socket file discriptor value invalid\n");
        errno = -ENOTSOCK;
        rc = -1;
        goto ErrorExit;
    }

    build_recv_socket( &request, SockFd, Len, Flags, SrcAddr, AddrLen );

#ifndef NODEVICE
    if ( ( rc = libc_write( devfd, &request, sizeof( request ) ) ) < 0 )
    {
        goto ErrorExit;
    }
    
    if ( ( rc = read_response( (mt_response_generic_t *) &response ) ) < 0 )
    {
        goto ErrorExit;
    }
#endif

    // Failure: rc = -1, errno set
    if ( response.base.status < 0 )
    {
        errno = -response.base.status;
        rc = -1;
        goto ErrorExit;
    }

    // Success: rc = byte count
    rc = response.base.size - MT_RESPONSE_SOCKET_RECV_SIZE;
    if ( rc > 0 )
    {
        memcpy( Buf, response.socket_recv.bytes, rc );
    }

    if ( MtResponseSocketRecvFrom == response.base.type )
    {
        if ( SrcAddr )
        {
            memcpy( SrcAddr,
                    &response.socket_recvfrom.src_addr,
                    sizeof( struct sockaddr ) );
        }
        if ( AddrLen )
        {
            memcpy( AddrLen,
                    &response.socket_recvfrom.addrlen,
                    sizeof( socklen_t ) );
        }
    }

ErrorExit:
    return rc;
}


ssize_t
recv( int     SockFd,
      void  * Buf,
      size_t  Len,
      int     Flags )
{
    return recvfrom( SockFd, Buf, Len, Flags, NULL, NULL);
}

ssize_t
read( int Fd, void *Buf, size_t count )
{
    int rc = 0;
    if ( !MW_SOCKET_IS_FD( Fd ) )
    {
        if ( ( rc = libc_read( Fd, Buf, count ) ) < 0 )
        {
            DEBUG_PRINT("Read failed in a bad way errno: %d \n", errno);
        }
        return rc;
    }
    
    return recvfrom( Fd, Buf, count, 0,  NULL, NULL );
}

ssize_t
readv( int Fd, const struct iovec * Iov, int IovCt )
{
    ssize_t rc = 0;

    if ( !MW_SOCKET_IS_FD( Fd ) )
    {
        rc = libc_readv( Fd, Iov, IovCt );
        goto ErrorExit;
    }

    // recvfrom() on each buffer
    for ( int i = 0; i < IovCt; ++i )
    {
        rc = recvfrom( Fd, Iov[i].iov_base, Iov[i].iov_len, 0, NULL, NULL );
        if ( rc < 0 )
        {
            goto ErrorExit;
        }
    }

ErrorExit:
    return rc;
}


int 
connect( int SockFd, 
         const struct sockaddr * Addr,
         socklen_t AddrLen )
{
   mt_request_generic_t request;
   mt_response_generic_t response;
   ssize_t rc = 0;
   
   if ( !MW_SOCKET_IS_FD( SockFd ) )
   {
       DEBUG_PRINT("Socket file discriptor value invalid\n");
       errno = ENOTSOCK;
       rc = -1;
       goto ErrorExit;
   }

   build_connect_socket( &request, SockFd, (struct sockaddr_in *) Addr );

   DEBUG_PRINT("\tSize of request base: %lu\n", sizeof(request));
   DEBUG_PRINT("\t\tSize of payload: %d\n", request.base.size);

#ifndef NODEVICE
   if( ( rc = libc_write( devfd, &request, sizeof( request ) ) ) < 0 )
   {
       goto ErrorExit;
   }
   
   if( ( rc = read_response( (mt_response_generic_t *) &response ) ) < 0 )
   {
       goto ErrorExit;
   }

#endif

   DEBUG_PRINT("Connect-socket response returned %d\n",
               (int) response.base.status );
   DEBUG_PRINT("\tSize of response base: %lu\n", sizeof(response));
   DEBUG_PRINT("\t\tSize of payload: %d\n", response.base.size);

   if ( (int)response.base.status < 0 )
   {
       DEBUG_PRINT( "\t\tError connecting. Error Number: %d\n",
               (int)response.base.status );
       errno = -response.base.status;
       rc =  -1;
       goto ErrorExit;
   }

    rc = response.base.status;

ErrorExit:
   return rc;
}

ssize_t 
send( int         SockFd, 
      const void *Buff, 
      size_t      Len,
      int         Flags )
{
   mt_request_generic_t request;
   mt_response_socket_send_t response;
   ssize_t rc = 0;
   ssize_t totSent = 0;
   const uint8_t *buff_ptr = Buff;

   if ( !MW_SOCKET_IS_FD( SockFd ) )
   {
       DEBUG_PRINT( "send() received invalid FD 0x%x\n", SockFd );
       errno = EINVAL;
       rc =  -1;
       goto ErrorExit;
   }

   while ( totSent < Len )
   {
       build_send_socket( &request, 
                          SockFd, 
                          ( buff_ptr + totSent ), 
                          Len - totSent );

       DEBUG_PRINT("Sending write-socket request on socket number: %x\n", SockFd);
       DEBUG_PRINT("\tSize of request base: %lu\n", sizeof(request));
       DEBUG_PRINT("\t\tSize of payload: %d\n", request.base.size);

#ifndef NODEVICE

       if ( ( rc = libc_write( devfd, &request, sizeof( request ) ) ) < 0 )
       {
           rc = -1;
           goto ErrorExit;
       }

       if ( ( rc = read_response( (mt_response_generic_t *) &response ) ) < 0 )
       {
           rc = -1;
           goto ErrorExit;
       }
#endif

       DEBUG_PRINT("Write-socket response returned status %d len %ld\n",
                   (int)response.base.status, rc );
       DEBUG_PRINT("\tSize of response base: %lu\n", sizeof(response));
       DEBUG_PRINT("\t\tSize of payload: %d\n", response.base.size);
       
       if ( (int)response.base.status < 0 )
       {
           errno = -response.base.status;
           rc = -1;
           goto ErrorExit;
       }

       totSent += (int)response.sent;
   }

    rc = totSent;

ErrorExit:
   return rc;
}


ssize_t
write( int Fd, const void *Buf, size_t count )
{
    ssize_t rc = 0;

    if ( !MW_SOCKET_IS_FD( Fd ) )
    {
        rc = libc_write( Fd, Buf, count );
        goto ErrorExit;
    }

    rc = send( Fd, Buf, count, 0 );

ErrorExit:
    return rc;
}

ssize_t
writev( int Fd, const struct iovec * Iov, int IovCt )
{
    ssize_t rc = 0;

    if ( !MW_SOCKET_IS_FD( Fd ) )
    {
        rc = libc_writev( Fd, Iov, IovCt );
        goto ErrorExit;
    }

    // send() on each buffer
    for ( int i = 0; i < IovCt; ++i )
    {
        rc = send( Fd, Iov[i].iov_base, Iov[i].iov_len, 0 );
        if ( rc < 0 )
        {
            goto ErrorExit;
        }
    }

ErrorExit:
    return rc;
}


int
getsockopt( int Fd,
            int Level,
            int OptName,
            void * OptVal,
            socklen_t  *OptLen )
{
    int targetFd = 0;
    
    DEBUG_PRINT( "getsockopt( 0x%x, %d, %d, %p, %p )\n",
                 Fd, Level, OptName, OptVal, OptLen );

    // Never call getsockopt on an mw_sock
    if ( MW_SOCKET_IS_FD( Fd ) )
    {
        targetFd = dummy_socket;
    }
    else
    {
        targetFd = Fd;
    }
    
    return libc_getsockopt( targetFd, Level, OptName, OptVal, OptLen );
}


int
setsockopt( int Fd,
            int Level,
            int OptName,
            const void * OptVal,
            socklen_t OptLen )
{
    int targetFd = 0;

    // XXXX: this drops all socket options on MW sockets, including
    // TCP_DEFER_ACCEPT

    DEBUG_PRINT( "setsockopt( 0x%x, %d, %d, %p=%x, %d )\n",
                 Fd, Level, OptName, OptVal, *(uint32_t *)OptVal, OptLen );

    // Never call getsockopt on an mw_sock
    if ( MW_SOCKET_IS_FD( Fd ) )
    {
        targetFd = dummy_socket;
    }
    else
    {
        targetFd = Fd;
    }
    
    return libc_setsockopt( targetFd, Level, OptName, OptVal, OptLen );
}


int
getsockname(int SockFd, struct sockaddr * Addr, socklen_t * AddrLen)
{
    int rc = 0;
    mt_request_socket_getname_t request = {0};
    mt_response_socket_getname_t response = {0};

    DEBUG_PRINT( "getsockname( %x, ... )\n", SockFd );

    if ( !MW_SOCKET_IS_FD( SockFd ) )
    {
        rc = libc_getpeername( SockFd, Addr, AddrLen );
        goto ErrorExit;
    }

    request.base.sig    = MT_SIGNATURE_REQUEST;
    request.base.type   = MtRequestSocketGetName;
    request.base.size   = MT_REQUEST_SOCKET_GETNAME_SIZE;
    request.base.sockfd = SockFd;
    request.maxlen       = (mt_size_t ) *AddrLen;

#ifndef NODEVICE
   if ( ( rc = libc_write( devfd, &request, sizeof( request ) ) ) < 0 )
   {
       goto ErrorExit;
   }

   if ( ( rc = read_response( (mt_response_generic_t *) &response ) ) < 0 )
   {
       goto ErrorExit;
   }
#endif

   if ( (int)response.base.status < 0 )
   {
       DEBUG_PRINT( "Error calling getsockname() on socket 0x%x: error %x (%d)\n",
                    SockFd, (int)response.base.status, (int)response.base.status );
       errno = -response.base.status;
       rc = -1;
       goto ErrorExit;
   }

   populate_sockaddr_in( (struct sockaddr_in *) Addr, &response.sockaddr );

   DEBUG_PRINT( "Returning %s:%d\n",
                inet_ntoa( ((struct sockaddr_in *) Addr)->sin_addr ),
                ntohs( ((struct sockaddr_in *) Addr)->sin_port ) );

ErrorExit:
    return rc;
}


int
getpeername(int SockFd, struct sockaddr * Addr, socklen_t * AddrLen)
{
    int rc = 0;
    mt_request_socket_getpeer_t request = {0};
    mt_response_socket_getpeer_t response = {0};

    DEBUG_PRINT( "getpeername( %x, ... )\n", SockFd );

    if ( !MW_SOCKET_IS_FD( SockFd ) )
    {
        rc = libc_getpeername( SockFd, Addr, AddrLen );
        goto ErrorExit;
    }

    request.base.sig    = MT_SIGNATURE_REQUEST;
    request.base.type   = MtRequestSocketGetPeer;
    request.base.size   = MT_REQUEST_SOCKET_GETPEER_SIZE;
    request.base.sockfd = SockFd;
    request.maxlen      = (mt_size_t ) *AddrLen;

#ifndef NODEVICE
   if ( ( rc = libc_write( devfd, &request, sizeof( request ) ) ) < 0 )
   {
       goto ErrorExit;
   }

   if ( ( rc = read_response( (mt_response_generic_t *) &response ) ) < 0 )
   {
       goto ErrorExit;
   }
#endif

   if ( (int)response.base.status < 0 )
   {
       DEBUG_PRINT( "Error calling getpeername() on socket 0x%x: error %x (%d)\n",
                    SockFd, (int)response.base.status, (int)response.base.status );
       errno = -response.base.status;
       rc = -1;
       goto ErrorExit;
   }

   populate_sockaddr_in( (struct sockaddr_in *) Addr, &response.sockaddr );

   DEBUG_PRINT( "Returning %s:%d\n",
                inet_ntoa( ((struct sockaddr_in *) Addr)->sin_addr ),
                ntohs( ((struct sockaddr_in *) Addr)->sin_port ) );

ErrorExit:
    return rc;
}


// XXXX: handle Flags & EPOLL_CLOEXEC correctly
int
epoll_create1( int Flags )
{
    int rc = 0;
    mt_request_poll_create_t request;
    mt_response_poll_create_t response;
    
    epoll_request_t * req = mw_epoll_create();
    if ( NULL == req )
    {
        errno = ENOMEM;
        rc = -1;
        goto ErrorExit;
    }

    build_poll_create( &request );

#ifndef NODEVICE
   if ( ( rc = libc_write( devfd, &request, sizeof( request ) ) ) < 0 )
   {
       MYASSERT( !"write" );
       goto ErrorExit;
   }

   if ( ( rc = read_response( (mt_response_generic_t *) &response ) ) < 0 )
   {
       goto ErrorExit;
   }
#endif

   if ( (int)response.base.status < 0 )
   {
       DEBUG_PRINT( "Error creating epoll FD. Error Number: %x (%d)\n",
                    (int)response.base.status, (int)response.base.status );
       errno = -response.base.status;
       rc = -1;
       goto ErrorExit;
   }
    
   req->createflags = Flags;
   rc = req->pseudofd = response.base.sockfd;

   DEBUG_PRINT( "Created epoll FD %x\n", rc );
   
ErrorExit:
    return rc;
}


int
epoll_create( int Size )
{
    return epoll_create1( 0 );
}

int
epoll_ctl( int EpFd,
           int Op,
           int Fd,
           struct epoll_event * Event )
{
    int rc = 0;

    rc = mw_epoll_ctl( EpFd, Op, Fd, Event );

    return rc;
}

int
epoll_wait( int EpFd,
            struct epoll_event * Events,
            int MaxEvents,
            int Timeout )
{
    int rc = 0;
    mt_request_poll_wait_t mreq;
    epoll_request_t * ereq = NULL;

    ereq = mw_epoll_find( EpFd );
    if ( NULL == ereq )
    {
        rc = -1;
        errno = EBADFD;
        goto ErrorExit;
    }

    if ( ereq->is_mw )
    {
        mt_response_poll_wait_t mres;
        
        build_poll_wait( &mreq, ereq, Timeout );

#ifndef NODEVICE
       if ( ( rc = libc_write( devfd, &mreq, sizeof( mreq ) ) ) < 0 )
       {
           rc = -1;
           goto ErrorExit;
       }

       if ( ( rc = read_response( (mt_response_generic_t *) &mres ) ) < 0 )
       {
           rc = -1;
           goto ErrorExit;
       }

       rc = populate_epoll_results( &mres, ereq, Events );
       goto ErrorExit;
#endif
    }

    // Non-MW case
    rc = do_epoll( ereq, Events, MaxEvents, Timeout );
    
ErrorExit:

    return rc;
}

int
epoll_pwait( int EpFd,
             struct epoll_event * Events,
             int MaxEvents,
             int Timeout,
             const sigset_t * Sigmask )
{
    DEBUG_PRINT( "epoll_pwait( %x, %p, %d, %d, %p )\n",
                 EpFd, Events, MaxEvents, Timeout, Sigmask );

    return libc_epoll_pwait( EpFd, Events, MaxEvents, Timeout, Sigmask );
}


int
poll( struct pollfd *Fds, nfds_t Nfds, int Timeout )
{
    int rc = 0;
    bool mwapi = false;
    int epollfd = -1;
    struct epoll_event outevents[ MAX_POLL_FD_COUNT ];

    if ( 0 == Nfds )
    {
        rc = libc_poll( Fds, Nfds, Timeout );
        goto ErrorExit;
    }

    mwapi = MW_SOCKET_IS_FD( Fds[0].fd );

    for ( int i = 1; i < Nfds; ++i )
    {
        if ( MW_SOCKET_IS_FD( Fds[i].fd ) != mwapi )
        {
            MYASSERT( !"Can't combine MW sockets with other FDs" );
            rc = -1;
            errno = EINVAL;
            goto ErrorExit;
        }
    }

    if ( !mwapi )
    {
        rc = libc_poll( Fds, Nfds, Timeout );
        goto ErrorExit;
    }

    // MW case: Use epoll() interface, which will go through our wrappers
    epollfd = epoll_create( Nfds );
    if ( epollfd < 0 )
    {
        goto ErrorExit;
    }

    for ( int i = 1; i < Nfds; ++i )
    {
        struct epoll_event epevt;
        epevt.data.fd = Fds[i].fd;
        rc = epoll_ctl( epollfd, EPOLL_CTL_ADD, Fds[i].fd, &epevt );
        if ( rc < 0 )
        {
            goto ErrorExit;
        }
    }

    rc = epoll_wait( epollfd, outevents, MAX_POLL_FD_COUNT, Timeout );
    if ( rc < 0 )
    {
        goto ErrorExit;
    }

    // Process the output: iterate over both arrays
    for ( int i = 0; i < rc; ++i ) // for all ready FDs
    {
        for ( int j = 0; j < Nfds; ++j ) // for all FDs in set
        {
            if ( outevents[i].data.fd != Fds[i].fd )
            {
                continue;
            }

            // This FD is flagged
            Fds[i].revents = outevents[i].events;
        }
    }

ErrorExit:
    if ( epollfd > 0 )
    {
        (void) close( epollfd );
    }
    return rc;
}


int
fcntl(int Fd, int Cmd, ... /* arg */ )
{
    int rc = 0;
    va_list ap;
    void * arg = NULL;

    mt_request_socket_fcntl_t   request = {0};
    mt_response_socket_fcntl_t response = {0};

    va_start( ap, Cmd );
    arg = va_arg( ap, void * );
    va_end( ap );

    if ( !MW_SOCKET_IS_FD( Fd ) )
    {
        rc = libc_fcntl( Fd, Cmd, arg );
        goto ErrorExit;
    }

    DEBUG_PRINT( "fcntl( %x, %d, %p )\n",
                 Fd, Cmd, arg );

    request.base.sig    = MT_SIGNATURE_REQUEST;
    request.base.type   = MtRequestSocketFcntl;
    request.base.size   = MT_REQUEST_SOCKET_FCNTL_SIZE;
    request.base.sockfd = Fd;

    switch( Cmd )
    {
    case F_GETFL:
        request.modify = 0;
        break;
    case F_SETFL:
        request.modify = 1;
        request.flags[ MT_SOCK_FCNTL_IDX_NONBLOCK ] =
            ( 0 != (((unsigned long)arg) & O_NONBLOCK) );
        break;
    default:
        DEBUG_PRINT( "fnctl() arguments unsupported by MW sockets\n" );
        rc = -1;
        goto ErrorExit;
    }

#ifndef NODEVICE
    if ( ( rc = libc_write( devfd, &request, sizeof( request ) ) ) < 0 )
    {
        rc = -1;
        goto ErrorExit;
    }

    if ( ( rc = read_response( (mt_response_generic_t *) &response ) ) < 0 )
    {
        rc = -1;
        goto ErrorExit;
    }
#endif

    if ( !request.modify ) // F_GETFL
    {
        rc = ( response.flags[MT_SOCK_FCNTL_IDX_NONBLOCK] ? O_NONBLOCK : 0 );
        goto ErrorExit;
    }

    // F_SETFL
    rc = response.base.status;

ErrorExit:
    DEBUG_PRINT( "fcntl( %x, %d, %p ) ==> %x\n",
                 Fd, Cmd, arg, rc );

    return rc;
}

void 
_init( void )
{
    DEBUG_PRINT("Intercept module loaded\n");

#ifdef NODEVICE
    devfd = open("/dev/null", O_RDWR);
#else
    if ( devfd < 0 )
    {
        devfd = open( DEV_FILE, O_RDWR);
    }
#endif

    if (devfd < 0)
    {
        perror("Failed to open the device...");
        exit(1);
    }

    g_dlh_libc = dlopen( "libc.so.6", RTLD_NOW );
    if ( NULL == g_dlh_libc )
    {
        DEBUG_PRINT("Failure: %s\n", dlerror() );
        exit(1);
    }

    get_libc_symbol( (void **) &libc_socket,   "socket"   );
    get_libc_symbol( (void **) &libc_read,     "read"     );
    get_libc_symbol( (void **) &libc_readv,    "readv"    );
    get_libc_symbol( (void **) &libc_write,    "write"    );
    get_libc_symbol( (void **) &libc_writev,   "writev"   );
    get_libc_symbol( (void **) &libc_close,    "close"    );
    get_libc_symbol( (void **) &libc_send,     "send"     );
    get_libc_symbol( (void **) &libc_sendto,   "sendto"   );
    get_libc_symbol( (void **) &libc_recv,     "recv"     );
    get_libc_symbol( (void **) &libc_recvfrom, "recvfrom" );

    get_libc_symbol( (void **) &libc_getsockopt, "getsockopt" );
    get_libc_symbol( (void **) &libc_setsockopt, "setsockopt" );

    get_libc_symbol( (void **) &libc_getsockname, "getsockname" );
    get_libc_symbol( (void **) &libc_getpeername, "getpeername" );

    get_libc_symbol( (void **) &libc_epoll_create, "epoll_create" );
    get_libc_symbol( (void **) &libc_epoll_create1, "epoll_create1" );

    get_libc_symbol( (void **) &libc_epoll_ctl,   "epoll_ctl" );
    get_libc_symbol( (void **) &libc_epoll_wait,  "epoll_wait" );
    get_libc_symbol( (void **) &libc_epoll_pwait, "epoll_pwait" );

    get_libc_symbol( (void **) &libc_poll,        "poll" );

    get_libc_symbol( (void **) &libc_fcntl,       "fcntl" );

    dummy_socket = libc_socket( AF_INET, SOCK_STREAM, 0 );
    if ( dummy_socket < 0 )
    {
        perror("socket");
        exit(1);
    }

    mw_epoll_init();
}


void
_fini( void )
{
    if ( dummy_socket > 0 )
    {
        libc_close( dummy_socket );
    }
    
    if ( g_dlh_libc )
    {
        dlclose( g_dlh_libc );
        g_dlh_libc = NULL;
    }

    DEBUG_PRINT("Intercept module unloaded\n");
}
