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
 * @brief   A shared library pre-loaded by target TCP/IP apps that intercepts 
 *          network calls.
 */

#define _GNU_SOURCE // dladdr()
#include <dlfcn.h>
#include <elf.h>
#include <link.h> // ^^^ for introspection

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


#include <pthread.h>
#include <signal.h>

#include <sys/uio.h>

#include <message_types.h>
#include <translate.h>

#include <mwcomms-ioctls.h>

#define DEBUG_FILE_STREAM g_log_file
#include <user_common.h>

#define DEV_FILE "/dev/mwcomms"

//
// 0 - wrap operations and use native sockets only
// 1 - wrap operations and use mwcomms driver for TCP traffic
//
#define USE_MWCOMMS 1


//
// Which send() implementation should we use? This choice has big
// implications for performance.
//

//#define SEND_ALLSYNC
//#define SEND_NOSYNC
//#define SEND_FINAL_SYNC
#define SEND_BATCH

// Should we wait if we encounter EAGAIN?
#define EAGAIN_TRIGGERS_SLEEP 1

//
//Prepare the log file for writing
//

static FILE *g_log_file = NULL;

static int devfd = -1; // FD to MW device

static void * g_dlh_libc = NULL;

static int
(*libc_socket)(int domain, int type, int protocol) = NULL;

static int
(*libc_bind)(int fd, const struct sockaddr * addr, socklen_t addrlen ) = NULL;

static int
(*libc_listen)(int fd, int backlog ) = NULL;

static int
(*libc_accept)(int fd, struct sockaddr * sockaddr, socklen_t * socklen) = NULL;

static int
(*libc_connect)(int fd, const struct sockaddr * addr, socklen_t addrlen ) = NULL;

static int
(*libc_read)(int fd, void *buf, size_t count) = NULL;

static ssize_t
(*libc_readv)(int fd, const struct iovec *iov, int iovcnt) = NULL;

static int
(*libc_write)(int fd, const void *buf, size_t count) = NULL;

static ssize_t
(*libc_writev)(int fd, const struct iovec *iov, int iovcnt) = NULL;

static int
(*libc_close)(int fd) = NULL;

static int
(*libc_shutdown)(int fd, int how) = NULL;

static ssize_t
(*libc_send)(int sockfd, const void* buf, size_t len, int flags) = NULL;

static ssize_t
(*libc_sendto)(int sockfd,
               const void* buf,
               size_t len,
               int flags,
               const struct sockaddr* dest_addr,
               socklen_t addrlen) = NULL;

static ssize_t
(*libc_recv)(int sockfd,
             void* buf,
             size_t len,
             int flags) = NULL;

static ssize_t
(*libc_recvfrom)(int sockfd,
                 void* buf,
                 size_t len,
                 int flags,
                 struct sockaddr* src_addr,
                 socklen_t* addrlen) = NULL;

static int 
(*libc_getsockopt)( int Fd,
                    int Level,
                    int OptName,
                    void * OptVal,
                    socklen_t  * OptLen ) = NULL;

static int
(*libc_setsockopt)( int Fd,
                    int Level,
                    int OptName,
                    const void * OptVal,
                    socklen_t OptLen ) = NULL;

static int
(*libc_getsockname)(int sockfd, struct sockaddr *addr, socklen_t *addrlen) = NULL;

static int
(*libc_getpeername)(int sockfd, struct sockaddr *addr, socklen_t *addrlen) = NULL;

static int
(*libc_fcntl)(int fd, int cmd, ... ) = NULL;


static void
wrapper_error( const char * Err )
{
    //print to log file AND standard error
    if( NULL != stderr )
    {
        fprintf( stderr,     "%s - %s\n", Err, strerror( errno ) );
        fflush( stderr );
    }

    if( NULL != g_log_file )
    {
        fprintf( g_log_file, "%s - %s\n", Err, strerror( errno ) );
        fflush( g_log_file );
    }
}


static void *
get_libc_symbol( void ** Addr, const char * Symbol )
{
    dlerror();
    *Addr = dlsym( g_dlh_libc, Symbol );

    if ( NULL == *Addr )
    {
        exit(1);
    }

    DEBUG_PRINT( "libc: %s ==> %p\n", Symbol, *Addr );

    return *Addr;
}

static bool
mwcomms_is_mwsocket( IN int Fd )
{
    bool answer = false;

#if (!USE_MWCOMMS)
    goto ErrorExit;
#else

    if ( Fd < 0 )
    {
        goto ErrorExit;
    }

    mwsocket_verify_args_t verify = { .fd = Fd, };

    int rc = ioctl( devfd, MW_IOCTL_IS_MWSOCKET, &verify );
    if ( rc )
    {
        wrapper_error( "ioctl" );
        goto ErrorExit;
    }

    answer = verify.is_mwsocket;
#endif

ErrorExit:
    return answer;
}


static int
mwcomms_write_request( IN  int                     MwFd,
                       IN  bool                    ReadResponse,
                       IN  mt_request_generic_t  * Request,
                       OUT mt_response_generic_t * Response )
{
    int rc = 0;
    ssize_t ct = 0;
    struct timespec remain_time = { .tv_sec = 0, .tv_nsec = 1 };
    int err = 0;

#if (!USE_MWCOMMS)
    // no processing at all
    goto ErrorExit;
#endif

#ifdef MYDEBUG
    if ( !mwcomms_is_mwsocket( MwFd ) )
    {
        MYASSERT( !"Called MW comms API on invalid socket" );
        rc = EINVAL;
        goto ErrorExit;
    }
#endif // MYDEBUG

    DEBUG_PRINT( "Processing request type %x fd %d and %s wait on response\n",
                 Request->base.type, MwFd,
                 ReadResponse ? "will" : "won't" );

    // Will we wait for the response?
    if ( ReadResponse )
    {
        MT_REQUEST_SET_CALLER_WAITS( Request );
    }

    // Write the request directly to the MW socket. If the ring buffer
    // is full, wait and try again.
    do
    {
        ct = libc_write( MwFd, Request, Request->base.size );
        if ( !( ct < 0 &&
                ( EINTR == errno || EAGAIN == errno ) ) )
        {
            break;
        }

        // rc == -1, errno == EAGAIN or errno == EINTR. So, try again...
        DEBUG_PRINT( "write() failed, trying again.\n" );
#if EAGAIN_TRIGGERS_SLEEP
        // The cost of this failing is negligible, so ignore return code.
        (void) nanosleep( &remain_time, &remain_time );
#endif // EAGAIN_TRIGGERS_SLEEP

    } while ( true );

    err = errno;
    MYASSERT( 0 == err );

    if ( ct < 0 )
    {
        rc = -1;
        DEBUG_PRINT( "MWSocket %d type 0x%x: write failed: %d\n",
                     MwFd, Request->base.type, err );
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

    DEBUG_PRINT( "Received response: fd %d ID %lx\n",
                 MwFd,
                 (unsigned long)Response->base.id );

    if ( ct < MT_RESPONSE_BASE_SIZE )
    {
        DEBUG_PRINT( "MWSocket %d: read failed or returned too few bytes: "
                     "rc=%d, errno=%d\n",
                     MwFd, (int)ct, err );
        DEBUG_PRINT( "Underflow: returned size less than minimum.\n" );
        rc = -1;
        //errno = ( ct < 0 ? err : EIO );
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
    errno = err;
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
    Request->base.flags  = 0;
}


#ifdef MYDEBUG // do NOT use the DEBUG_PRINT() macro here

// From
// http://stackoverflow.com/questions/7775991/how-to-get-hexdump-of-a-structure-data
static void
hex_dump( const char *desc, void *addr, int len )
{
    int i;
    unsigned char buff[17];
    unsigned char *pc = (unsigned char*)addr;

    // Output description if given.
    if (desc != NULL)
        printf ("%s:\n", desc);

    if (len == 0) {
        printf("  ZERO LENGTH\n");
        return;
    }
    if (len < 0) {
        printf("  NEGATIVE LENGTH: %i\n",len);
        return;
    }

    // Process every byte in the data.
    for (i = 0; i < len; i++) {
        // Multiple of 16 means new line (with line offset).

        if ((i % 16) == 0) {
            // Just don't print ASCII for the zeroth line.
            if (i != 0)
                printf ("  %s\n", buff);

            // Output the offset.
            printf ("  %04x ", i);
        }

        // Now the hex code for the specific character.
        printf (" %02x", pc[i]);

        // And store a printable ASCII character for later.
        if ((pc[i] < 0x20) || (pc[i] > 0x7e))
            buff[i % 16] = '.';
        else
            buff[i % 16] = pc[i];
        buff[(i % 16) + 1] = '\0';
    }

    // Pad out last line if not exactly 16 characters.
    while ((i % 16) != 0) {
        printf ("   ");
        i++;
    }

    // And print the final ASCII bit.
    printf ("  %s\n", buff);
}
#endif // MYDEBUG


int 
socket( int Domain, 
        int Type, 
        int Protocol )
{
    int rc = 0;
    int err = 0;

    // See socket(2); N.B. Type can be OR-ed with specific flags
    if ( AF_INET != Domain
         || ( 0 == (SOCK_STREAM & Type ) )
         || (!USE_MWCOMMS) )
    {
        rc = libc_socket( Domain, Type, Protocol );
        err = errno;
        DEBUG_PRINT( "libc_socket( %d, 0x%x, %d ) ==> %d\n",
                     Domain, Type, Protocol, rc );
        goto ErrorExit;
    }

#if USE_MWCOMMS
    mwsocket_create_args_t create;

    create.domain   = xe_net_get_mt_protocol_family( Domain );
    create.type     = MT_ST_STREAM;
    create.protocol = Protocol;
    create.outfd = -1;

    rc = ioctl( devfd, MW_IOCTL_CREATE_SOCKET, &create );
    if ( rc < 0 )
    {
        err = errno;
        MYASSERT( !"ioctl" );
        goto ErrorExit;
    }

    DEBUG_PRINT( "mwsocket( %d, 0x%x, %d ) ==> %d\n",
                 Domain, Type, Protocol, create.outfd );

    rc = create.outfd;
#endif

ErrorExit:
    errno = err;
    return rc;
}


int
close( int Fd )
{
    DEBUG_PRINT( "close(%d)\n", Fd );
    return libc_close( Fd );
}


int
bind( int                     SockFd,
      const struct sockaddr * SockAddr, 
      socklen_t               AddrLen )
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
        wrapper_error("Only AF_INET is supported at this time\n");
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
    DEBUG_PRINT( "bind(%d, ...) ==> %d\n", SockFd, (int)rc );
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
        rc = libc_listen( SockFd, BackLog );
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
    DEBUG_PRINT( "listen(%d, ...) ==> %d\n", SockFd, (int)rc );
    return rc;
}


int
accept( int               SockFd,
        struct sockaddr * SockAddr, 
        socklen_t       * SockLen)
{
    mt_request_generic_t  request;
    mt_response_generic_t response = {0};
    ssize_t rc = 0;
    int e = 0;

    if ( !mwcomms_is_mwsocket( SockFd ) )
    {
        rc = libc_accept( SockFd, SockAddr, SockLen );
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

    if ( (int) response.base.status < 0 )
    {
        goto ErrorExit;
    }

    rc = response.base.sockfd; // new socket
    populate_sockaddr_in( (struct sockaddr_in *)SockAddr,
                          &response.socket_accept.sockaddr );

ErrorExit:
    e = errno;
    DEBUG_PRINT( "accept(%d, ...) ==> %d\n", SockFd, (int)rc );
    errno = e;
    return rc;
}


int
accept4( int               SockFd,
         struct sockaddr * SockAddr,
         socklen_t       * SockLen,
         int               Flags )
{
    // XXXX: Drop flags for now, could be O_NONBLOCK or CLOEXEC

//    MYASSERT( 0 == Flags );
    return accept( SockFd, SockAddr, SockLen );
}


ssize_t
recvfrom( int               SockFd,
          void            * Buf,
          size_t            Len,
          int               Flags,
          struct sockaddr * SrcAddr,
          socklen_t       * AddrLen )
{
    mt_request_generic_t request;
    mt_response_generic_t response = {0};
    mt_size_t * received = NULL;
    int err = 0;
    ssize_t rc = 0;

    // We write an mt_request_socket_recv_t, but could get back either
    // a mt_response_socket_recv_t or mt_response_socket_recvfrom_t.
    
    bzero( &response, sizeof(response) );

    if ( !mwcomms_is_mwsocket(SockFd) )
    {
        rc = libc_recvfrom( SockFd, Buf, Len, Flags, SrcAddr, AddrLen );
        err = errno;
        goto ErrorExit;
    }

    DEBUG_PRINT( "recvfrom(%d, buf, %d, %d, ... )\n", SockFd, (int)Len, Flags );
    mwcomms_init_request( &request,
                          MtRequestSocketRecv,
                          MT_REQUEST_SOCKET_RECV_SIZE,
                          SockFd );
    received = &response.socket_recv.count;
    
    request.socket_recv.flags = Flags;
    request.socket_recv.requested = MIN( MESSAGE_TYPE_MAX_PAYLOAD_LEN, Len );

    if ( NULL != SrcAddr )
    {
        // RecvFrom
        request.base.type = MtRequestSocketRecvFrom;
        received = &response.socket_recvfrom.count;
    }

    DEBUG_PRINT( "Receiving %d bytes\n", request.socket_recv.requested );

    rc = (ssize_t)mwcomms_write_request( SockFd, true, &request, &response );
    err = errno;
    if ( rc )
    {
        if ( MW_ERROR_REMOTE_SURPRISE_CLOSE == err )
        {
            DEBUG_PRINT("Detected remote close\n" );
            err = 0;
            rc = 0;
        }
        goto ErrorExit;
    }

    //DEBUG_PRINT( "Receiving done, status %d, size %d\n",
    //             response.base.status, response.socket_recvfrom.count );
/*
    if ( response.base.flags & _MT_RESPONSE_FLAG_REMOTE_CLOSED )
    {
        // remote side closed
        errno = 0;
        rc = 0;
        goto ErrorExit;
    }
*/
    // Failure: rc = -1, errno set
    if ( response.base.status < 0 )
    {
        err = -response.base.status;
        rc = -1;
        goto ErrorExit;
    }
    else if ( 0 == *received )
    {
        rc = 0;
        err = 0;
        goto ErrorExit;
    }
    
    if ( *received > 0 )
    {
        memcpy( Buf, response.socket_recv.bytes, *received );
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

    // Success: rc = byte count
    rc = *received;

ErrorExit:
    DEBUG_PRINT( "recvfrom(%d, buf, %d, %d, ... ) ==> %d / %d\n",
                 SockFd, (int)Len, Flags, (int)rc, err );
    //hex_dump( "Received data", response.socket_recv.bytes, *received );

    errno = err;
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
read( int Fd, void *Buf, size_t Count )
{
    int rc = 0;
    int err = 0;

    if ( !mwcomms_is_mwsocket( Fd ) )
    {
        if ( ( rc = libc_read( Fd, Buf, Count ) ) < 0 )
        {
            err = errno;
            DEBUG_PRINT("libc_read failed: %d \n", errno);
            errno = err;
        }
        goto ErrorExit;
    }
    
    rc = recvfrom( Fd, Buf, Count, 0,  NULL, NULL );
    err = errno;

ErrorExit:
    DEBUG_PRINT( "read(%d, buf, %d ) ==> %d / %d\n", Fd, (int)Count, rc, err );
    errno = err;
    return rc;
}


ssize_t
readv( int Fd, const struct iovec * Iov, int IovCt )
{
    ssize_t rc = 0;
    ssize_t tot = 0;
    int err = 0;

    if ( !mwcomms_is_mwsocket( Fd ) )
    {
        rc = libc_readv( Fd, Iov, IovCt );
        err = errno;
        goto ErrorExit;
    }

    // recvfrom() on each buffer
    for ( int i = 0; i < IovCt; ++i )
    {
        rc = recvfrom( Fd, Iov[i].iov_base, Iov[i].iov_len, 0, NULL, NULL );
        if ( rc < 0 )
        {
            err = errno;
            goto ErrorExit;
        }
        tot += rc;
    }

ErrorExit:
    DEBUG_PRINT( "readv(%d, ...)) ==> %d / %d\n", Fd, (int)tot, err );

    errno = err;
    return tot;
}


int 
connect( int                     SockFd, 
         const struct sockaddr * Addr,
         socklen_t               AddrLen )
{
   mt_request_generic_t request;
   mt_response_generic_t response = {0};
   int rc = 0;
   
   if ( !mwcomms_is_mwsocket( SockFd ) )
   {
       rc = libc_connect( SockFd, Addr, AddrLen );
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
    DEBUG_PRINT( "connect(%d,...) ==> %d\n", SockFd, rc );
    return rc;
}


#ifdef SEND_BATCH

ssize_t 
send( int          SockFd, 
      const void * Buf,
      size_t       Len,
      int          Flags )
{
    mt_request_generic_t request;
    mt_response_generic_t response = {0};
    ssize_t rc = 0;
    ssize_t tot_sent = 0;
    uint8_t * pbuf = (uint8_t *)Buf;
    int err = 0;
    bool final = false;
    
    if ( !mwcomms_is_mwsocket( SockFd ) )
    {
        rc = libc_send( SockFd, Buf, Len, Flags );
        err = errno;
        goto ErrorExit;
    }

    mwcomms_init_request( &request,
                          MtRequestSocketSend,
                          MT_REQUEST_SOCKET_SEND_SIZE,
                          SockFd );

    request.socket_send.flags = Flags;

    // We're doing a batch send, which means we synchronize (wait for
    // the response) on the final send()
    request.base.flags = _MT_FLAGS_BATCH_SEND_INIT | _MT_FLAGS_BATCH_SEND;

    while ( tot_sent < Len )
    {
        ssize_t chunksz = MIN( MESSAGE_TYPE_MAX_PAYLOAD_LEN, Len - tot_sent );

        // Which chunk is this? A chunk can be both the first and last.
        if ( tot_sent > 0 )
        {
            request.base.flags &= ~_MT_FLAGS_BATCH_SEND_INIT;
        }
        if ( Len == tot_sent + chunksz )
        {
            final = true;
            request.base.flags |= _MT_FLAGS_BATCH_SEND_FINI;
        }

        request.base.size = MT_REQUEST_SOCKET_SEND_SIZE + chunksz;
        memcpy( request.socket_send.bytes, &pbuf[ tot_sent ], chunksz );

        DEBUG_PRINT( "send: @%d of %d bytes, final: %s\n",
                     (int)tot_sent, (int)Len, final ? "true" : "false" );

        // Mimick the real send(): if the request is successfully
        // written to the ring buffer, then succeed. Wait only for the
        // final response, which was zeroed-out earlier. N.B. The INS
        // will not return EAGAIN anywhere in our batch.
        rc = mwcomms_write_request( SockFd, final, &request, &response );
        if ( rc )
        {
            err = errno;
            if ( MW_ERROR_REMOTE_SURPRISE_CLOSE == err )
            {
                DEBUG_PRINT( "Remote side closed\n" );
                err = EPIPE;
            }

            if ( !final )
            {
                // Assert final and try again. This will give us the
                // total number of bytes written and clear the batch
                // send state in the driver.
                final = true;
                continue;
            }

            // The final request failed. Assume the socket is
            // permantently broken on the INS.
            break;
        }

        tot_sent += chunksz;
    } // while

    // The final request was sent to the INS, and some bytes were
    // successfully sent on the INS's stack. Mask any errors here. (??)
    if ( response.socket_send.count > 0 )
    {
        rc = response.socket_send.count;
        err = 0;
        MYASSERT( tot_sent == rc );
        MYASSERT( Len == tot_sent );
    }
    else if ( 0 == rc )
    {
        err = -response.base.status;
        rc = -1;
    }

ErrorExit:
    DEBUG_PRINT( "send( %d, buf, %d, %x ) ==> %d / %d\n",
                 SockFd, (int)Len, Flags, (int)rc, err );
    errno = err;
    return rc;
}
#endif // SEND_BATCH



#ifdef SEND_FINAL_SYNC
ssize_t 
send( int          SockFd, 
      const void * Buf,
      size_t       Len,
      int          Flags )
{
    mt_request_generic_t request;
    mt_response_generic_t response = {0};
    ssize_t rc = 0;
    ssize_t tot_sent = 0;
    uint8_t * pbuf = (uint8_t *)Buf;
    int err = 0;

    if ( !mwcomms_is_mwsocket( SockFd ) )
    {
        rc = libc_send( SockFd, Buf, Len, Flags );
        err = errno;
        goto ErrorExit;
    }

    mwcomms_init_request( &request,
                          MtRequestSocketSend,
                          MT_REQUEST_SOCKET_SEND_SIZE,
                          SockFd );

    request.socket_send.flags = Flags;
    request.base.flags = 0;

    while ( tot_sent < Len )
    {
        ssize_t chunksz = MIN( MESSAGE_TYPE_MAX_PAYLOAD_LEN, Len - tot_sent );

        // Is this the final chunk?
        bool final = ( tot_sent + chunksz == Len );

        request.base.size = MT_REQUEST_SOCKET_SEND_SIZE + chunksz;
        memcpy( request.socket_send.bytes, &pbuf[ tot_sent ], chunksz );

        DEBUG_PRINT( "send: @%d of %d bytes, final: %s\n",
                     (int)tot_sent, (int)Len, final ? "true" : "false" );

        // Mimick the real send(): if the request is successfully
        // written to the ring buffer, then succeed. Do not wait for
        // the response... except the final one.
        rc = mwcomms_write_request( SockFd, final, &request, &response );
        if ( rc )
        {
            err = errno;
            if ( MW_ERROR_REMOTE_SURPRISE_CLOSE == err )
            {
                DEBUG_PRINT( "Remote side closed\n" );
                err = EPIPE;
            }
            goto ErrorExit;
        }

        tot_sent += final ? response.socket_send.count : chunksz;
    }

    rc = tot_sent;

ErrorExit:
    DEBUG_PRINT( "send( %d, buf, %d, %x ) ==> %d / %d\n",
                 SockFd, (int)Len, Flags, (int)rc, err );
    errno = err;
    return rc;
}
#endif // SEND_FINAL_SYNC


#ifdef SEND_NOSYNC
// @brief Non-synchronizing send
ssize_t 
send( int          SockFd, 
      const void * Buf,
      size_t       Len,
      int          Flags )
{
    mt_request_generic_t request;
    mt_response_generic_t response = {0};
    ssize_t rc = 0;
    ssize_t tot_sent = 0;
    uint8_t * pbuf = (uint8_t *)Buf;
    int err = 0;

    if ( !mwcomms_is_mwsocket( SockFd ) )
    {
        rc = libc_send( SockFd, Buf, Len, Flags );
        err = errno;
        goto ErrorExit;
    }

    mwcomms_init_request( &request,
                          MtRequestSocketSend,
                          MT_REQUEST_SOCKET_SEND_SIZE,
                          SockFd );

    request.socket_send.flags = Flags;
    request.base.flags = 0;

    while ( tot_sent < Len )
    {
        ssize_t chunksz = MIN( MESSAGE_TYPE_MAX_PAYLOAD_LEN, Len - tot_sent );

        request.base.size = MT_REQUEST_SOCKET_SEND_SIZE + chunksz;
        memcpy( request.socket_send.bytes, &pbuf[ tot_sent ], chunksz );

        // Mimick the real send(): if the request is successfully
        // written to the ring buffer, then succeed. Do not wait for
        // the response.
        rc = mwcomms_write_request( SockFd, false, &request, &response );
        if ( rc )
        {
            err = errno;
            if ( MW_ERROR_REMOTE_SURPRISE_CLOSE == err )
            {
                DEBUG_PRINT( "Remote side closed\n" );
                err = EPIPE;
            }
            goto ErrorExit;
        }

        // The response will not be populated, so don't check it
        tot_sent += chunksz;
    }

    rc = tot_sent;

ErrorExit:
    DEBUG_PRINT( "send( %d, buf, %d, %x ) ==> %d / %d\n",
                 SockFd, (int)Len, Flags, (int)rc, err );
    errno = err;
    return rc;
}
#endif // SEND_NOSYNC


#ifdef SEND_ALLSYNC
// @brief All chunks of send() are synchronized
ssize_t 
send( int          SockFd, 
      const void * Buf,
      size_t       Len,
      int          Flags )
{
    mt_request_generic_t request;
    mt_response_generic_t response = {0};
    ssize_t rc = 0;
    ssize_t tot_sent = 0;
    uint8_t * pbuf = (uint8_t *)Buf;
    int err = 0;

    if ( !mwcomms_is_mwsocket( SockFd ) )
    {
        rc = libc_send( SockFd, Buf, Len, Flags );
        err = errno;
        goto ErrorExit;
    }

    DEBUG_PRINT( "send( %d, buf, %d, %x )\n", SockFd, (int)Len, Flags );

    mwcomms_init_request( &request,
                          MtRequestSocketSend,
                          MT_REQUEST_SOCKET_SEND_SIZE,
                          SockFd );

    request.socket_send.flags = Flags;
    request.base.flags = 0;

    while ( tot_sent < Len )
    {
        ssize_t chunksz = MIN( MESSAGE_TYPE_MAX_PAYLOAD_LEN, Len - tot_sent );

        request.base.size = MT_REQUEST_SOCKET_SEND_SIZE + chunksz;
        memcpy( request.socket_send.bytes, &pbuf[ tot_sent ], chunksz );

        // Mimick the real send(): if the request is successfully
        // written to the ring buffer, then succeed. Do not wait for
        // the response.
        rc = mwcomms_write_request( SockFd, true, &request, &response );
        if ( rc )
        {
            err = errno;
            if ( MW_ERROR_REMOTE_SURPRISE_CLOSE == err )
            {
                DEBUG_PRINT( "Remote side closed\n" );
                err = EPIPE;
            }
            goto ErrorExit;
        }

        tot_sent += response.socket_send.count;
    }

    rc = tot_sent;

ErrorExit:
    DEBUG_PRINT( "send( %d, buf, %d, %x ) ==> %d / %d\n",
                 SockFd, (int)Len, Flags, (int)rc, err );
    errno = err;
    return rc;
}
#endif // SEND_ALLSYNC


ssize_t
write( int Fd, const void *Buf, size_t Count )
{
    ssize_t rc = 0;

    if ( !mwcomms_is_mwsocket( Fd ) )
    {
        rc = libc_write( Fd, Buf, Count );
        goto ErrorExit;
    }

    rc = send( Fd, Buf, Count, 0 );

ErrorExit:
    DEBUG_PRINT( "write(%d, buf, %d ) ==> %d\n", Fd, (int)Count, (int)rc );
    return rc;
}


ssize_t
writev( int Fd, const struct iovec * Iov, int IovCt )
{
    ssize_t rc = 0;
    ssize_t tot = 0;
    int err = 0;
    
    for ( int i = 0; i < IovCt; ++i )
    {
        DEBUG_PRINT( "writev ==> write(%d, buf, %d )\n",
                     Fd, (int)Iov[i].iov_len );
    }

    if ( !mwcomms_is_mwsocket( Fd ) )
    {
        tot = libc_writev( Fd, Iov, IovCt );
        err = errno;
        goto ErrorExit;
    }

    // send() on each buffer. The total bytes written is the return value.
    for ( int i = 0; i < IovCt; ++i )
    {
        rc = send( Fd, Iov[i].iov_base, Iov[i].iov_len, 0 );
        if ( rc < 0 )
        {
            err = errno;
            tot = rc;
            goto ErrorExit;
        }
        tot += rc;
    }

ErrorExit:
    DEBUG_PRINT( "writev(%d, ...)) ==> %d / %d\n", Fd, (int)tot, err );
    errno = err;
    return tot;
}


int
shutdown( int SockFd, int How )
{
    mt_request_generic_t request = {0};
    mt_response_generic_t response = {0};
    ssize_t rc = 0;
    int err = 0;

    errno = 0;

    if ( !mwcomms_is_mwsocket( SockFd ) )
    {
        rc = libc_shutdown( SockFd, How );
        err = errno;
        goto ErrorExit;
    }

    DEBUG_PRINT( "shutdown( %d, %d )\n", SockFd, How );

    mwcomms_init_request( &request,
                          MtRequestSocketShutdown,
                          MT_REQUEST_SOCKET_SHUTDOWN_SIZE,
                          SockFd );

    // Values are standardized, translation not needed
    request.socket_shutdown.how = How;

    rc = mwcomms_write_request( SockFd, true, &request, &response );
    if ( rc )
    {
        err = errno;
        goto ErrorExit;
    }

    if ( response.base.status < 0 )
    {
        err = -response.base.status;
        rc = -1;
        goto ErrorExit;
    }

ErrorExit:
    DEBUG_PRINT( "shutdown( %d, %d ) ==> %d / %d\n", SockFd, How, (int)rc, err );
    errno = err;
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
            Attribs->name = MtSockAttribReuseaddr;
            break;
        case SO_KEEPALIVE:
            Attribs->name = MtSockAttribKeepalive;
            break;
        case SO_REUSEPORT:
            Attribs->name = MtSockAttribReuseport;
            break;
        case SO_RCVTIMEO:
            Attribs->name = MtSockAttribRcvTimeo;
            break;
        case SO_SNDTIMEO:
            Attribs->name = MtSockAttribSndTimeo;
            break;
        case SO_RCVLOWAT:
            Attribs->name = MtSockAttribRcvLoWat;
            break;
        case SO_SNDLOWAT:
            Attribs->name = MtSockAttribSndLoWat;
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
            Attribs->name = MtSockAttribDeferAccept;
            break;
        case TCP_NODELAY:
            Attribs->name = MtSockAttribNodelay;
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
getsockopt( int         Fd,
            int         Level,
            int         OptName,
            void      * OptVal,
            socklen_t * OptLen )
{
    int rc = 0;
    int err = 0;
    mwsocket_attrib_t attr = {0};

    if ( !mwcomms_is_mwsocket( Fd ) )
    {
        rc = libc_getsockopt( Fd, Level, OptName, OptVal, OptLen );
        err = errno;
        goto ErrorExit;
    }

    rc = mwcomms_set_sockattr( Level, OptName, &attr );
    if ( rc )
    {
        err = rc;
        rc = -1;
        goto ErrorExit;
    }

    attr.modify = false;

    rc = ioctl( Fd, MW_IOCTL_SOCKET_ATTRIBUTES, &attr );
    if ( rc )
    {
        err = errno;
        DEBUG_PRINT( "ioctl() failed: %d\n", rc );
        goto ErrorExit;
    }

    if ( MtSockAttribSndTimeo == attr.name
         || MtSockAttribRcvTimeo == attr.name )
    {
        MYASSERT( *OptLen >= sizeof(struct timeval) );
        ((struct timeval *) OptVal)->tv_sec  = attr.val.t.s;
        ((struct timeval *) OptVal)->tv_usec = attr.val.t.us;
        *OptLen = sizeof( struct timeval );
    }
    else if ( *OptLen > 0 && *OptLen <= sizeof( attr.val ) )
    {
        memcpy( OptVal, &attr.val, *OptLen );
    }

ErrorExit:
    DEBUG_PRINT( "getsockopt( 0x%x, %d, %d, %p, %p ) => %d\n",
                 Fd, Level, OptName, OptVal, OptLen, rc );
    errno = err;
    return rc;
}


int
setsockopt( int          Fd,
            int          Level,
            int          OptName,
            const void * OptVal,
            socklen_t    OptLen )
{
    int rc = 0;
    mwsocket_attrib_t attr = {0};
    int err = 0;

    if ( !mwcomms_is_mwsocket( Fd ) )
    {
        rc = libc_setsockopt( Fd, Level, OptName, OptVal, OptLen );
        err = errno;
        goto ErrorExit;
    }

    rc = mwcomms_set_sockattr( Level, OptName, &attr );
    if ( rc )
    {
        err = rc;
        rc = -1;
        goto ErrorExit;
    }

    attr.modify = true;
    if ( MtSockAttribSndTimeo == attr.name
         || MtSockAttribRcvTimeo == attr.name )
    {
        MYASSERT( OptLen >= sizeof(struct timeval) );
        attr.val.t.s  = ((struct timeval *) OptVal)->tv_sec;
        attr.val.t.us = ((struct timeval *) OptVal)->tv_usec;
    }
    else if ( OptLen > 0 && OptLen <= sizeof( attr.val ) )
    {
        memcpy( &attr.val, OptVal, OptLen );
        //attr.value.v = (uint64_t) *(uint32_t *) OptVal;
    }
    else
    {
        MYASSERT( !"Unhandled option value" );
    }

    rc = ioctl( Fd, MW_IOCTL_SOCKET_ATTRIBUTES, &attr );
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
getsockname( int SockFd, struct sockaddr * Addr, socklen_t * AddrLen )
{
    int rc = 0;
    mt_request_generic_t request = {0};
    mt_response_generic_t response = {0};
    int err = 0;

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
        err = errno;
        goto ErrorExit;
    }

    if ( (int)response.base.status < 0 )
    {
        DEBUG_PRINT( "Error calling getsockname() on socket %d: error %x (%d)\n",
                     SockFd, (int)response.base.status,
                     (int)response.base.status );
        err = -response.base.status;
        rc = -1;
        goto ErrorExit;
    }

    populate_sockaddr_in( (struct sockaddr_in *) Addr,
                          &response.socket_getname.sockaddr );


ErrorExit:
    DEBUG_PRINT( "getsockname(%d,...) ==> %d / %s:%d\n",
                 SockFd, rc,
                 inet_ntoa( ((struct sockaddr_in *) Addr)->sin_addr ),
                 ntohs( ((struct sockaddr_in *) Addr)->sin_port ) );
    errno = err;
    return rc;
}


int
getpeername(int SockFd, struct sockaddr * Addr, socklen_t * AddrLen)
{
    int rc = 0;
    mt_request_generic_t request = {0};
    mt_response_generic_t response = {0};
    int err = 0;

    DEBUG_PRINT( "getpeername( %x, ... )\n", SockFd );

    if ( !mwcomms_is_mwsocket( SockFd ) )
    {
        rc = libc_getpeername( SockFd, Addr, AddrLen );
        err = errno;
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
        err = errno;
        goto ErrorExit;
    }

   if ( (int)response.base.status < 0 )
   {
       DEBUG_PRINT( "Error calling getpeername() on socket %d: error %x (%d)\n",
                    SockFd, (int)response.base.status, (int)response.base.status );
       err = -response.base.status;
       rc = -1;
       goto ErrorExit;
   }

   populate_sockaddr_in( (struct sockaddr_in *) Addr,
                         &response.socket_getpeer.sockaddr );

ErrorExit:
   DEBUG_PRINT( "getpeername(%d,...) ==> %d / %s:%d\n",
                SockFd, rc,
                inet_ntoa( ((struct sockaddr_in *) Addr)->sin_addr ),
                ntohs( ((struct sockaddr_in *) Addr)->sin_port ) );

   errno = err;
   return rc;
}


int
fcntl(int Fd, int Cmd, ... /* arg */ )
{
    int rc = 0;
    va_list ap;
    void * arg = NULL;
    int err = 0;
    mwsocket_attrib_t attr = {0};
    int oldflags = 0;
    int newflags = 0;

    va_start( ap, Cmd );
    arg = va_arg( ap, void * );
    va_end( ap );

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

    attr.modify  = true;
    attr.name    = MtSockAttribNonblock;
    attr.val.v32 = (uint32_t) (bool) ( newflags & O_NONBLOCK );

    rc = ioctl( Fd, MW_IOCTL_SOCKET_ATTRIBUTES, &attr );
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

#ifdef WRAP_CHECK_FUNCTIONS

//
// Set up function aliases here for reason stated in the Makefile. The
// signature doesn't matter; we're instructing the loader to redirect
// one function call to another. Since only socket-related calls are
// intercepted, just a few functions are aliased.
//

void __read_chk(void)     __attribute__((weak, alias ("read")     ));
void __recv_chk(void)     __attribute__((weak, alias ("recv")     ));
void __recvfrom_chk(void) __attribute__((weak, alias ("recvfrom") ));

#endif // WRAP_CHECK_FUNCTIONS


#ifdef DEBUG
static int
dl_callback(struct dl_phdr_info * Info,
            size_t                Size,
            void                * Data)
{
    char buf[64];

    snprintf( buf, sizeof(buf), "%s => %p\n",
              Info->dlpi_name,
              (void *) Info->dlpi_addr );

    (void) fwrite( buf, strlen(buf), 1, stdout );

    for (int j = 0; j < Info->dlpi_phnum; j++)
    {
        printf( "\t header %2d: address=%16p size=%6lx fl=%lx\n",
                j,
                (void *) (Info->dlpi_addr + Info->dlpi_phdr[j].p_vaddr),
                (unsigned long) Info->dlpi_phdr[j].p_memsz,
                (unsigned long) Info->dlpi_phdr[j].p_flags );
    }

    return 0;
}
#endif // DEBUG


void __attribute__((constructor))
init_wrapper( void )
{
    //
    // Prepare the log file for writing
    // 
    char shim_log[32] = {0};

    snprintf( shim_log, sizeof(shim_log),
              "%s/ins_%d.log", SHIM_LOG_PATH, getpid() );

    g_log_file = fopen( shim_log, "w" );
    if ( NULL == g_log_file )
    {
        fprintf( stderr, "Failed to open log file %s: %s\n",
                 shim_log, strerror( errno ) );
        exit(1);
    }

    DEBUG_PRINT( "Intercept module loaded\n" );

    //
    // Open the kernel module's device (mwcomms)
    //
#if (!USE_MWCOMMS)
    devfd = -1;
#else
    devfd = open( DEV_FILE, O_RDWR );
    if (devfd < 0)
    {
        fprintf( stderr, "Failed to open device %s: %s\n",
                 DEV_FILE, strerror( errno ) );
        exit(1);
    }
#endif

    //
    // Find the TCP/IP functions in libc and save their
    // locations. This module hooks them and forwards their uses in
    // some cases.
    //
    g_dlh_libc = dlopen( "libc.so.6", RTLD_NOW );
    if ( NULL == g_dlh_libc )
    {
        DEBUG_PRINT( "Failure: %s\n", dlerror() );
        exit(1);
    }

    get_libc_symbol( (void **) &libc_write,    "write"    );
    get_libc_symbol( (void **) &libc_read,     "read"     );
    get_libc_symbol( (void **) &libc_readv,    "readv"    );
    get_libc_symbol( (void **) &libc_writev,   "writev"   );
    get_libc_symbol( (void **) &libc_close,    "close"    );
    get_libc_symbol( (void **) &libc_shutdown, "shutdown" );

    get_libc_symbol( (void **) &libc_socket,   "socket"   );
    get_libc_symbol( (void **) &libc_bind,     "bind"     );
    get_libc_symbol( (void **) &libc_listen,   "listen"   );
    get_libc_symbol( (void **) &libc_accept,   "accept"   );
    get_libc_symbol( (void **) &libc_connect,  "connect"  );

    get_libc_symbol( (void **) &libc_send,     "send"     );
    get_libc_symbol( (void **) &libc_sendto,   "sendto"   );
    get_libc_symbol( (void **) &libc_recv,     "recv"     );
    get_libc_symbol( (void **) &libc_recvfrom, "recvfrom" );

    get_libc_symbol( (void **) &libc_getsockopt, "getsockopt" );
    get_libc_symbol( (void **) &libc_setsockopt, "setsockopt" );

    get_libc_symbol( (void **) &libc_getsockname, "getsockname" );
    get_libc_symbol( (void **) &libc_getpeername, "getpeername" );

    get_libc_symbol( (void **) &libc_fcntl,       "fcntl" );

#ifdef DEBUG
    //
    // XXXX: Linux-only function
    //
    dl_iterate_phdr(dl_callback, NULL);
#endif // DEBUG

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
        libc_close( devfd );
    }

    // Don't call DEBUG_PRINT() after g_log_file is closed!
    DEBUG_PRINT( "Intercept module unloaded\n" );

    if ( NULL != g_log_file )
    {
        fclose( g_log_file );
    }
}
