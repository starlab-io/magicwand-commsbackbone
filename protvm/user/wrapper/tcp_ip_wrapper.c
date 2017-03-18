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
#include <stdbool.h>

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

//#include "epoll_wrapper.h"

#include <mwcomms-ioctls.h>

#define DEV_FILE "/dev/mwcomms"

static int devfd = -1; // FD to MW device

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
    if ( ReadResponse )
    {
        MT_REQUEST_SET_CALLER_WAITS( Request );
    }
    
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
    // XXXX: Drop flags for now, could be O_NONBLOCK or CLOEXEC

    return accept( SockFd, SockAddr, SockLen );
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

    DEBUG_PRINT( "Receiving %d bytes\n", request.socket_recv.requested );
    rc = mwcomms_write_request( SockFd, true, &request, &response );
    if ( rc )
    {
        goto ErrorExit;
    }

    DEBUG_PRINT( "Receiving done, status %d, size %d\n",
                 response.base.status, response.base.size );
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
        ssize_t chunksz = MIN( MESSAGE_TYPE_MAX_PAYLOAD_LEN, Len - totSent );

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


static int
mwcomms_set_sockattr( IN int Level,
                      IN int OptName,
                      INOUT mwsocket_attrib_t * Attribs )
{
    int rc = 0;

    switch( Level )
    {
    case SOL_SOCKET:
        switch( OptName )
        {
        case SO_REUSEADDR:
            Attribs->attrib = MtSockAttribReuseaddr;
            break;
        case SO_KEEPALIVE:
            Attribs->attrib = MtSockAttribKeepalive;
            break;
        default:
            DEBUG_PRINT( "Failing on unsupported SOL_SOCKET option %d\n", OptName );
            rc = EINVAL;
            break;
        }
        break;
    case SOL_TCP:
        switch( OptName )
        {
        case TCP_DEFER_ACCEPT:
            // Linux-only option.
            Attribs->attrib = MtSockAttribDeferAccept;
            break;
        case TCP_NODELAY:
            Attribs->attrib = MtSockAttribNodelay;
            break;
        default:
            DEBUG_PRINT( "Failing on unsupported SOL_TCP option %d\n", OptName );
            rc = EINVAL;
            break;
        }
        break;
    default:
        MYASSERT( !"Unrecognized level for setsockopt\n" );
    }

    return rc;
}


int
getsockopt( int Fd,
            int Level,
            int OptName,
            void * OptVal,
            socklen_t  *OptLen )
{
    int rc = 0;
    int err = 0;
    mwsocket_attrib_t attribs = {0};

    DEBUG_PRINT( "getsockopt( 0x%x, %d, %d, %p, %p )\n",
                 Fd, Level, OptName, OptVal, OptLen );

    if ( !mwcomms_is_mwsocket( Fd ) )
    {
        rc = libc_getsockopt( Fd, Level, OptName, OptVal, OptLen );
        err = errno;
        goto ErrorExit;
    }

    rc = mwcomms_set_sockattr( Level, OptName, &attribs );
    if ( rc )
    {
        err = rc;
        rc = -1;
        goto ErrorExit;
    }
    
    attribs.modify = false;

    rc = ioctl( Fd, MW_IOCTL_SOCKET_ATTRIBUTES, &attribs );
    if ( rc )
    {
        err = errno;
        DEBUG_PRINT( "ioctl() failed: %d\n", rc );
        goto ErrorExit;
    }

    if ( OptLen > 0 )
    {
        *(uint32_t *) OptVal = attribs.value;
    }

ErrorExit:
    DEBUG_PRINT( "getsockopt( 0x%x, %d, %d, %p, %p ) => %d\n",
                 Fd, Level, OptName, OptVal, OptLen, rc );
    errno = err;
    return rc;
}


int
setsockopt( int Fd,
            int Level,
            int OptName,
            const void * OptVal,
            socklen_t OptLen )
{
    int rc = 0;
    mwsocket_attrib_t attrib = {0};
    int err = 0;

    DEBUG_PRINT( "setsockopt( 0x%x, %d, %d, %p=%x, %d )\n",
                 Fd, Level, OptName, OptVal, *(uint32_t *)OptVal, OptLen );

    if ( !mwcomms_is_mwsocket( Fd ) )
    {
        rc = libc_setsockopt( Fd, Level, OptName, OptVal, OptLen );
        err = errno;
        goto ErrorExit;
    }

    rc = mwcomms_set_sockattr( Level, OptName, &attrib );
    if ( rc )
    {
        err = rc;
        rc = -1;
        goto ErrorExit;
    }
    
    attrib.modify = true;
    if ( OptLen > 0 )
    {
        attrib.value = *(uint32_t *) OptVal;
    }

    rc = ioctl( Fd, MW_IOCTL_SOCKET_ATTRIBUTES, &attrib );
    if ( rc )
    {
        err = errno;
        DEBUG_PRINT( "ioctl() failed: %d\n", rc );
    }

ErrorExit:
    DEBUG_PRINT( "setsockopt( 0x%x, %d, %d, %p=%x, %d ) => %d\n",
                 Fd, Level, OptName, OptVal, *(uint32_t *)OptVal, OptLen, rc );
    errno = err;
    return rc;
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
    int err = 0;
    mwsocket_attrib_t attrib = {0};
    int oldflags = 0;
    int newflags = 0;

    va_start( ap, Cmd );
    arg = va_arg( ap, void * );
    va_end( ap );

    DEBUG_PRINT( "fcntl( %x, %d, %p )\n",
                 Fd, Cmd, arg );

    // We only handle F_SETFL. Anything else is only passed directly
    // to VFS.
    if ( !mwcomms_is_mwsocket( Fd )   // This is not an mwsocket, or
         || Cmd != F_SETFL          ) // This is not a F_SETFL command 
    {
        rc = libc_fcntl( Fd, Cmd, arg );
        err = errno;
        goto ErrorExit;
    }

    //
    // This is a F_SETFL on an mwsocket:
    // (1) get the old flags, (2) set the new ones, (3) inform
    // mwsocket of new value
    //

    oldflags = libc_fcntl( Fd, F_GETFL );
    newflags = (int) (unsigned long) arg;

    // Set the new flags
    rc = libc_fcntl( Fd, F_SETFL, newflags );
    if ( rc )
    {
        err = errno;
        MYASSERT( !"fcntl()" );
        goto ErrorExit;
    }

    // if the change doesn't involve O_NONBLOCK, we don't care
    if ( (oldflags & O_NONBLOCK) == (newflags & O_NONBLOCK) )
    {
        goto ErrorExit;
    }
        
    attrib.modify = true;
    attrib.attrib = MtSockAttribNonblock;
    attrib.value  = (uint32_t) (bool) ( newflags & O_NONBLOCK );

    rc = ioctl( Fd, MW_IOCTL_SOCKET_ATTRIBUTES, &attrib );
    if ( rc )
    {
        DEBUG_PRINT( "ioctl() failed: %d\n", rc );
        goto ErrorExit;
    }
    
ErrorExit:
    DEBUG_PRINT( "fcntl( %x, %d, %p ) ==> %x\n",
                 Fd, Cmd, arg, rc );
    if ( rc )
    {
        errno = err;
    }
    return rc;
}

void __attribute__((constructor))
init_wrapper( void )
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

#if 0
    int rc = 0;
        
    DEBUG_PRINT( "Creating dummy socket\n" );

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

    int val = 1;
    rc = setsockopt( create.outfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val) );
    MYASSERT( 0 == rc );

    rc = fcntl( create.outfd, F_SETFL, O_NONBLOCK );
    MYASSERT( 0 == rc );
    
    close( create.outfd );
    exit(1);
#endif 
}


void __attribute__((destructor))
fini_wrapper( void )
{
    
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
