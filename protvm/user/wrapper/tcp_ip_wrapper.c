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

//#include <sys/epoll.h>
//#include <poll.h>

#include <sys/uio.h>

#include <message_types.h>
#include <translate.h>
#include <user_common.h>

#include "epoll_wrapper.h"

#include <mwcomms-ioctls.h>

#define DEV_FILE "/dev/mwcomms"

static int devfd = -1; // FD to MW device
static int dummy_socket = -1; // socket for get/setsockopt

static void * g_dlh_libc = NULL;

static int
(*libc_socket)(int domain, int type, int protocol);

static int
(*libc_bind)(int fd, const struct sockaddr * addr, socklen_t addrlen );

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

static bool
mwcomms_is_mwsocket( IN int Fd )
{
    bool answer = false;
    int rc = 0;

#ifdef NODEVICE
    goto ErrorExit;
#else
    mwsocket_verify_args_t verify = { .fd = Fd, };

    rc = ioctl( devfd, MW_IOCTL_IS_MWSOCKET, &verify );
    if ( rc )
    {
        perror( "ioctl" );
        goto ErrorExit;
    }

    answer = verify.is_mwsocket;
#endif // NODEVICE

ErrorExit:
    return answer;
}


static int
mwcomms_write_request( IN  int         MwFd,
                       IN  bool        ReadResponse,
                       IN  mt_request_generic_t  * Request,
                       OUT mt_response_generic_t * Response )
{
    int rc = 0;
    ssize_t ct = 0;

#ifdef NODEVICE
    // no processing at all
    goto ErrorExit;
#endif
    
    int err = 0;

#ifdef MYDEBUG
    if ( !mwcomms_is_mwsocket( MwFd ) )
    {
        MYASSERT( !"Called MW comms API on invalid socket" );
        rc = EINVAL;
        goto ErrorExit;
    }
#endif // MYDEBUG

    DEBUG_PRINT( "Processing request type %x fd %d\n",
                 Request->base.type, MwFd );

    // Will we wait for the response?
    Request->base.pvm_blocked_on_response = (mt_bool_t) ReadResponse;

    // Write the request directly to the MW socket
    do
    {
        ct = libc_write( MwFd, Request, Request->base.size );
    } while ( ct < 0 && EAGAIN == errno );

    err = errno;
    
    if ( ct < 0 )
    {
        rc = -1;
        DEBUG_PRINT( "MWSocket %d: write failed: %d\n", MwFd, err );
        errno = err;
        goto ErrorExit;
    }

    if ( !ReadResponse )
    {
        // Do not await the response. We're done.
        goto ErrorExit;
    }
    
    // Read the response from the MW socket. This may block.
    do
    {
        ct = libc_read( MwFd, Response, sizeof(*Response) );
    } while ( ct < 0 && EINTR == errno );

    err = errno;

    if ( ct < MT_RESPONSE_BASE_SIZE )
    {
        DEBUG_PRINT( "MWSocket %d: read failed or returned too few bytes: "
                     "rc=%d, errno=%d\n",
                     MwFd, (int)ct, err );
        DEBUG_PRINT( "Underflow: returned size less than minimum.\n" );
        rc = -1;
        errno = ( ct < 0 ? err : EIO );
        goto ErrorExit;
    }

    // Error in response
    if ( IS_CRITICAL_ERROR( Response->base.status ) )
    {
        DEBUG_PRINT( "Remote side encountered critical error %x, ID=%lx LFD=%d\n",
                     (int)Response->base.status,
                     (unsigned long)Response->base.id,
                     MwFd );
        rc = -1;
        Response->base.status = EIO;
        goto ErrorExit;
    }

    if ( Response->base.status < 0 )
    {
        DEBUG_PRINT( "Error status in response: type %x status %d\n",
                     Response->base.type, Response->base.status );
        rc = -1;
        errno = -Response->base.status;
        goto ErrorExit;
    }

    MYASSERT( 0 == rc );

ErrorExit:
    return rc;
}


static void
mwcomms_init_request( INOUT mt_request_generic_t * Request,
                      IN mt_request_type_t         Type,
                      IN mt_size_t                 Size,
                      IN mw_socket_fd_t            MwSockFd )
{
    bzero( Request, sizeof(*Request) );

    Request->base.sig    = MT_SIGNATURE_REQUEST;
    Request->base.type   = Type;
    Request->base.size   = Size;
    Request->base.sockfd = MwSockFd;
}

/*
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
                     int                    backlog)
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
*/



int 
socket( int domain, 
        int type, 
        int protocol )
{
    int rc = 0;
    int err = 0;
    mwsocket_create_args_t create;
   
    if ( AF_INET != domain )
    {
        rc = libc_socket( domain, type, protocol );
        goto ErrorExit;
    }

    create.domain   = xe_net_get_mt_protocol_family( domain );
    create.type     = MT_ST_STREAM;
    create.protocol = protocol;
    create.outfd = -1;

#ifndef NODEVICE
    rc = ioctl( devfd, MW_IOCTL_CREATE_SOCKET, &create );
    if ( rc < 0 )
    {
        err = errno;
        MYASSERT( !"ioctl" );
        errno = err;
        goto ErrorExit;
    }

    rc = create.outfd;
    DEBUG_PRINT( "Returning socket %d\n", (int)rc );
#endif

ErrorExit:
    return rc;
}

/*
  int
close( int Fd )
{
    mt_request_generic_t  request;
    mt_response_generic_t response;
    ssize_t rc = 0;

    if ( mwcomms_is_mwsocket( Fd ) )
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
*/

int
bind( int                     SockFd,
      const struct sockaddr * SockAddr, 
      socklen_t                AddrLen )
{
    mt_request_generic_t   request;
    mt_response_generic_t  response = {0};
    ssize_t rc = 0;
    
    if ( !mwcomms_is_mwsocket(SockFd) )
    {
        rc = libc_bind( SockFd, SockAddr, AddrLen );
        goto ErrorExit;
    }

    if ( SockAddr->sa_family != AF_INET
         || AddrLen != sizeof(struct sockaddr_in) )
    {
        perror("Only AF_INET is supported at this time\n");
        errno = EINVAL;
        rc =  -1;
        goto ErrorExit;
    }

    mwcomms_init_request( &request,
                          MtRequestSocketBind,
                          MT_REQUEST_SOCKET_BIND_SIZE,
                          SockFd );

    populate_mt_sockaddr_in( &request.socket_bind.sockaddr,
                             (struct sockaddr_in *)SockAddr );

    rc = mwcomms_write_request( SockFd, true, &request, &response );
    if ( !rc )
    {
        rc = response.base.status;
    }

ErrorExit:
    return rc;
}


int
listen( int SockFd, int BackLog )
{
    mt_request_generic_t   request;
    mt_response_generic_t  response = {0};
    ssize_t rc = 0;
    
    if ( !mwcomms_is_mwsocket( SockFd ) )
    {
        DEBUG_PRINT("Socket file discriptor value invalid\n");
        errno = ENOTSOCK;
        rc = -1;
        goto ErrorExit;
    }

    mwcomms_init_request( &request,
                          MtRequestSocketListen,
                          MT_REQUEST_SOCKET_LISTEN_SIZE,
                          SockFd );

    request.socket_listen.backlog = BackLog;

    rc = mwcomms_write_request( SockFd, true, &request, &response );
    if ( rc )
    {
        goto ErrorExit;
    }

    rc = response.base.status;
    if ( rc < 0 )
    {
        DEBUG_PRINT( "Returning error response with errno=%d\n", (int)-rc );
        errno = -rc;
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
    mt_request_generic_t  request;
    mt_response_generic_t response = {0};
    ssize_t rc = 0;
    
    if ( !mwcomms_is_mwsocket( SockFd ) )
    {
        DEBUG_PRINT("Socket file discriptor value invalid");
        errno = ENOTSOCK;
        rc = -1;
        goto ErrorExit;
    }

    mwcomms_init_request( &request,
                          MtRequestSocketAccept,
                          MT_REQUEST_SOCKET_ACCEPT_SIZE,
                          SockFd );

    rc = mwcomms_write_request( SockFd, true, &request, &response );
    if ( rc )
    {
        goto ErrorExit;
    }

    if ( response.base.status < 0 )
    {
        rc = -1;
        errno = -response.base.status;
        goto ErrorExit;
    }

    DEBUG_PRINT( "accept() returned sockfd %d\n",
                 response.base.status );
    rc = response.base.status; // new socket
    populate_sockaddr_in( (struct sockaddr_in *)SockAddr,
                          &response.socket_accept.sockaddr );

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

/*
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
*/

ssize_t
recvfrom( int    SockFd,
          void * Buf,
          size_t Len,
          int    Flags,
          struct sockaddr * SrcAddr,
          socklen_t       * AddrLen )
{
    mt_request_generic_t request;
    mt_response_generic_t response = {0};
    
    bzero( &response, sizeof(response) );

    ssize_t rc = 0;

    if ( !mwcomms_is_mwsocket(SockFd) )
    {
        DEBUG_PRINT("Socket file discriptor value invalid\n");
        errno = -ENOTSOCK;
        rc = -1;
        goto ErrorExit;
    }

    mwcomms_init_request( &request,
                          MtRequestSocketRecv,
                          MT_REQUEST_SOCKET_RECV_SIZE,
                          SockFd );

    request.socket_recv.flags = Flags;
    request.socket_recv.requested = MIN( MESSAGE_TYPE_MAX_PAYLOAD_LEN, Len );

    if ( NULL != SrcAddr )
    {
        // RecvFrom
        request.base.type = MtRequestSocketRecvFrom;
    }

    rc = mwcomms_write_request( SockFd, true, &request, &response );
    if ( rc )
    {
        goto ErrorExit;
    }
    
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
    if ( !mwcomms_is_mwsocket( Fd ) )
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

    if ( !mwcomms_is_mwsocket( Fd ) )
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
   mt_response_generic_t response = {0};
   ssize_t rc = 0;
   
   if ( !mwcomms_is_mwsocket( SockFd ) )
   {
       DEBUG_PRINT("Socket file discriptor value invalid\n");
       errno = ENOTSOCK;
       rc = -1;
       goto ErrorExit;
   }

   mwcomms_init_request( &request,
                         MtRequestSocketConnect,
                         MT_REQUEST_SOCKET_CONNECT_SIZE,
                         SockFd );

   populate_mt_sockaddr_in( &request.socket_connect.sockaddr,
                            (struct sockaddr_in *) Addr );

   // XXXX: block?
   rc = mwcomms_write_request( SockFd, true, &request, &response );
   if ( rc )
   {
       goto ErrorExit;
   }

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
send( int          SockFd, 
      const void * Buff, 
      size_t       Len,
      int          Flags )
{
    mt_request_generic_t request;
    mt_response_generic_t response = {0};
    ssize_t rc = 0;
    ssize_t totSent = 0;
    const uint8_t *buff_ptr = Buff;

    if ( !mwcomms_is_mwsocket( SockFd ) )
    {
        DEBUG_PRINT( "send() received invalid FD 0x%x\n", SockFd );
        errno = EINVAL;
        rc =  -1;
        goto ErrorExit;
    }

    mwcomms_init_request( &request,
                          MtRequestSocketSend,
                          MT_REQUEST_SOCKET_SEND_SIZE,
                          SockFd );

    while ( totSent < Len )
    {
        ssize_t chunksz = MIN( MESSAGE_TYPE_MAX_PAYLOAD_LEN, Len );

        request.base.size = MT_REQUEST_SOCKET_SEND_SIZE + chunksz;
        memcpy( request.socket_send.bytes, &buff_ptr[ totSent ], chunksz );

        rc = mwcomms_write_request( SockFd, false, &request, &response );
        if ( rc )
        {
            goto ErrorExit;
        }

        if ( response.base.status < 0 )
        {
           errno = -response.base.status;
           rc = -1;
           goto ErrorExit;
        }

        totSent += chunksz;

        DEBUG_PRINT("* Sent %d bytes, tot %d of %d\n",
                    (int)chunksz, (int)totSent, (int)Len );
    }

    rc = totSent;

ErrorExit:
   return rc;
}


ssize_t
write( int Fd, const void *Buf, size_t count )
{
    ssize_t rc = 0;

    if ( !mwcomms_is_mwsocket( Fd ) )
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

    if ( !mwcomms_is_mwsocket( Fd ) )
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
    if ( mwcomms_is_mwsocket( Fd ) )
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
    if ( mwcomms_is_mwsocket( Fd ) )
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
    mt_request_generic_t request = {0};
    mt_response_generic_t response = {0};

    DEBUG_PRINT( "getsockname( %x, ... )\n", SockFd );

    if ( !mwcomms_is_mwsocket( SockFd ) )
    {
        rc = libc_getsockname( SockFd, Addr, AddrLen );
        goto ErrorExit;
    }

    mwcomms_init_request( &request,
                          MtRequestSocketGetName,
                          MT_REQUEST_SOCKET_GETNAME_SIZE,
                          SockFd );
    request.socket_getname.maxlen = (mt_size_t ) *AddrLen;

    rc = mwcomms_write_request( SockFd, true, &request, &response );
    if ( rc )
    {
        goto ErrorExit;
    }

    if ( (int)response.base.status < 0 )
    {
        DEBUG_PRINT( "Error calling getsockname() on socket %d: error %x (%d)\n",
                     SockFd, (int)response.base.status,
                     (int)response.base.status );
        errno = -response.base.status;
        rc = -1;
        goto ErrorExit;
    }

    populate_sockaddr_in( (struct sockaddr_in *) Addr,
                          &response.socket_getname.sockaddr );

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
    mt_request_generic_t request = {0};
    mt_response_generic_t response = {0};

    DEBUG_PRINT( "getpeername( %x, ... )\n", SockFd );

    if ( !mwcomms_is_mwsocket( SockFd ) )
    {
        rc = libc_getpeername( SockFd, Addr, AddrLen );
        goto ErrorExit;
    }

    mwcomms_init_request( &request,
                          MtRequestSocketGetPeer,
                          MT_REQUEST_SOCKET_GETPEER_SIZE,
                          SockFd );
    request.socket_getname.maxlen = (mt_size_t ) *AddrLen;

    rc = mwcomms_write_request( SockFd, true, &request, &response );
    if ( rc )
    {
        goto ErrorExit;
    }

   if ( (int)response.base.status < 0 )
   {
       DEBUG_PRINT( "Error calling getpeername() on socket %d: error %x (%d)\n",
                    SockFd, (int)response.base.status, (int)response.base.status );
       errno = -response.base.status;
       rc = -1;
       goto ErrorExit;
   }

   populate_sockaddr_in( (struct sockaddr_in *) Addr,
                         &response.socket_getpeer.sockaddr );

   DEBUG_PRINT( "Returning %s:%d\n",
                inet_ntoa( ((struct sockaddr_in *) Addr)->sin_addr ),
                ntohs( ((struct sockaddr_in *) Addr)->sin_port ) );

ErrorExit:
    return rc;
}


int
fcntl(int Fd, int Cmd, ... /* arg */ )
{
    int rc = 0;
    va_list ap;
    void * arg = NULL;

    mwsocket_modify_pollset_args_t pollset = {0};

    va_start( ap, Cmd );
    arg = va_arg( ap, void * );
    va_end( ap );

    DEBUG_PRINT( "fcntl( %x, %d, %p )\n",
                 Fd, Cmd, arg );

    rc = libc_fcntl( Fd, Cmd, arg );
    if ( rc < 0 )
    {
        DEBUG_PRINT( "fcntl() failed: %d\n", rc );
        goto ErrorExit;
    }
    
    if ( !mwcomms_is_mwsocket( Fd )
        || F_SETFL != Cmd )
    {
        goto ErrorExit;
    }

    // Handle MW socket: update pollset only
    pollset.fd = Fd;
    pollset.add = (bool) ( ((unsigned long)arg) & O_NONBLOCK );

    rc = ioctl( devfd, MW_IOCTL_MOD_POLLSET, &pollset );
    if ( rc )
    {
        DEBUG_PRINT( "ioctl() failed: %d\n", rc );
        goto ErrorExit;
    }
ErrorExit:
    DEBUG_PRINT( "fcntl( %x, %d, %p ) ==> %x\n",
                 Fd, Cmd, arg, rc );
    return rc;
}

void __attribute__((constructor))
init_wrapper( void )
{
    int rc = 0;

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
    get_libc_symbol( (void **) &libc_bind,     "bind"     );
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

    get_libc_symbol( (void **) &libc_fcntl,       "fcntl" );

    DEBUG_PRINT( "Creating dummy socket\n" );
    dummy_socket = libc_socket( AF_INET, SOCK_STREAM, 0 );
    if ( dummy_socket < 0 )
    {
        perror("socket");
        exit(1);
    }
    DEBUG_PRINT( "Got dummy socket FD %d\n", dummy_socket );

    //mw_epoll_init();

    // TEST TEST
    mwsocket_create_args_t create = {
        .domain   = MT_PF_INET,
        .type     = MT_ST_STREAM,
        .protocol = 0,
    };

    rc = ioctl( devfd, MW_IOCTL_CREATE_SOCKET, &create );
    if ( rc )
    {
        perror( "ioctl" );
        exit(1);
    }

    DEBUG_PRINT( "Got socket FD %d\n", create.outfd );
    close( create.outfd );
}


void __attribute__((destructor))
fini_wrapper( void )
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

    if ( devfd > 0 )
    {
        close( devfd );
    }

    DEBUG_PRINT("Intercept module unloaded\n");
}
