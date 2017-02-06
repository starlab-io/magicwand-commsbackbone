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

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

#include <dlfcn.h>

#include <message_types.h>
#include <translate.h>
#include <app_common.h>


#define DEV_FILE "/dev/mwchar"
#define BUF_SZ   1024



static int devfd = -1; // FD to MW device

static void * g_dlh_libc = NULL;

static int
(*libc_read)(int fd, void *buf, size_t count);

static int
(*libc_write)(int fd, const void *buf, size_t count);

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


//
// Track the sockets we have opened from MW
//
#define MAX_SOCKETS 1024
static mw_socket_fd_t g_open_sockets[ MAX_SOCKETS ];

static void
init_open_sockets( void )
{
    for( int i = 0; i < MAX_SOCKETS; ++i )
    {
        g_open_sockets[ i ] = MT_INVALID_SOCKET_FD;
    }
}

static void
fini_open_sockets( void )
{
    for( int i = 0; i < MAX_SOCKETS; ++i )
    {
        if (  MT_INVALID_SOCKET_FD == g_open_sockets[ i ] )
        {
            continue;
        }

        // XXXX: twice the work necessary, since close does this work too
        close( g_open_sockets[ i ] );
        g_open_sockets[ i ] = MT_INVALID_SOCKET_FD;
    }
}


static int
insert_open_socket( mw_socket_fd_t NewSock )
{
    ssize_t rc = 0;

    for( int i = 0; i < MAX_SOCKETS; ++i )
    {
        if (  MT_INVALID_SOCKET_FD == g_open_sockets[ i ] )
        {
            g_open_sockets[ i ] = NewSock;
            goto ErrorExit;
        }
    }

    rc = ENOMEM;
    
ErrorExit:
    return rc;
}


static void
remove_open_socket( mw_socket_fd_t SockFd )
{
    for( int i = 0; i < MAX_SOCKETS; ++i )
    {
        if ( SockFd == g_open_sockets[ i ] )
        {
            g_open_sockets[ i ] = MT_INVALID_SOCKET_FD;
            break;
        }
    }
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

    // Fix to handle non-ascii payload
    send->base.size = MT_REQUEST_SOCKET_SEND_SIZE + actual_len;
}

int 
socket( int domain, 
        int type, 
        int protocol )
{
   mt_request_generic_t  request;
   mt_response_generic_t response;
   ssize_t rc = 0;
   
   // XXXX: args ignored
   build_create_socket( &request );

   DEBUG_PRINT("Sending socket-create request\n");
   //DEBUG_PRINT("\tSize of request base: %lu\n", sizeof(request));
   //DEBUG_PRINT("\t\tSize of payload: %d\n", request.base.size);

#ifndef NODEVICE

   rc = libc_write( devfd, &request, sizeof(request)); 
   MYASSERT( rc > 0 );

   rc = read_response( (mt_response_generic_t *) &response );
   MYASSERT( rc > 0 );
    
#endif

   //DEBUG_PRINT("Create-socket response returned\n");
   //DEBUG_PRINT("\tSize of response base: %lu\n", sizeof(response));
   //DEBUG_PRINT("\t\tSize of payload: %d\n", response.base.size);
   if ( (int)response.base.status < 0 )
   {
       DEBUG_PRINT( "Error creating socket. Error Number: %x (%d)\n",
                    (int)response.base.status, (int)response.base.status );
       errno = -response.base.status;
       BARE_DEBUG_BREAK();
      // Returns -1 on error
      return -1;
   }

   // Returns socket number on success
   if ( insert_open_socket( response.base.sockfd ) )
   {
       DEBUG_PRINT( "Failure: cannot track this socket\n" );
       close( response.base.sockfd );
       response.base.sockfd = MT_INVALID_SOCKET_FD;
   }
   
   DEBUG_PRINT( "Returning socket 0x%x\n", response.base.sockfd );
   return (int)response.base.sockfd;
}

int
close( int SockFd )
{
    mt_request_generic_t  request;
    mt_response_generic_t response;
    ssize_t rc = 0;
    
    if ( !MW_SOCKET_IS_FD( SockFd ) )
    {
       return libc_close( SockFd );
    }
    
    build_close_socket( &request, SockFd );

    DEBUG_PRINT( "Closing socket %x\n", SockFd );
    
    //DEBUG_PRINT("\tSize of request base: %lu\n", sizeof(request));
    //DEBUG_PRINT("\t\tSize of payload: %d\n", request.base.size);

#ifndef NODEVICE

    rc = libc_write( devfd, &request, sizeof(request)); 
    MYASSERT( rc > 0 );
    
    rc = read_response( (mt_response_generic_t *) &response );
    MYASSERT( rc > 0 );

#endif

    //DEBUG_PRINT("Close-socket response returned %d\n",
    //(int)response.base.status );
    //DEBUG_PRINT("\tSize of response base: %lu\n", sizeof(response));
    //DEBUG_PRINT("\t\tSize of payload: %d\n", response.base.size);

    remove_open_socket( SockFd );
    
    if ( response.base.status )
    {
        DEBUG_PRINT( "\t\tError closing socket. Error Number: %lu\n", response.base.status );
        errno = -response.base.status;
        // Returns -1 on error
        return -1;
    }

    // Returns 0 on success
    return response.base.status;
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
        return -1;
    }

    if ( SockAddr->sa_family != AF_INET || addrlen != sizeof(struct sockaddr_in) )
    {
        perror("Only AF_INET is supported at this time\n");
        errno = EINVAL;
        return -1;
    }

    sockaddr_in = ( struct sockaddr_in * ) SockAddr;

    build_bind_socket( &request, SockFd, sockaddr_in, addrlen);

#ifndef NODEVICE    
    
    rc = libc_write( devfd, &request, sizeof(request) );
    MYASSERT( rc > 0 );
    
    rc = read_response( (mt_response_generic_t *) &response );
    MYASSERT( rc > 0 );

#endif

    return response.base.status;
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
        return -1;
    }

    build_listen_socket( &request, SockFd, &backlog);

#ifndef NODEVICE

    rc = libc_write( devfd, &request, sizeof(request) );
    MYASSERT( rc > 0 );

    rc = read_response( (mt_response_generic_t *) &response );
    MYASSERT( rc > 0 );

#endif

    if ( response.base.status < 0 )
    {
        errno = -response.base.status;
    }

    return ( response.base.status < 0 ? -1 : response.base.status );
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
        return -1;
    }
    
    build_accept_socket(&request, SockFd);
    populate_sockaddr_in( (struct sockaddr_in *)SockAddr,
                          &response.socket_accept.sockaddr);
    
#ifndef NODEVICE
    
    rc = libc_write( devfd, &request, sizeof(request) );
    MYASSERT( rc > 0 );
    
    rc = read_response( (mt_response_generic_t *) &response );
    MYASSERT( rc > 0 );

#endif

    if ( response.base.status < 0 )
    {
        errno = -response.base.status;
        return -1;
    }
    
    return response.base.status;
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

    recieve->requested = Len;
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
        return -1;
    }

    build_recv_socket( &request, SockFd, Len, Flags, SrcAddr, AddrLen );

#ifndef NODEVICE

    rc = libc_write( devfd, &request, sizeof(request) );
    MYASSERT( rc > 0 );
    
    rc = read_response( (mt_response_generic_t *) &response );
    MYASSERT( rc > 0 );

#endif
    
    rc = response.base.size - MT_RESPONSE_SOCKET_RECV_SIZE;
    if ( rc > 0 )
    {
        memcpy( Buf, response.socket_recv.bytes, rc );
    }

    if ( response.base.status < 0 )
    {
        errno = -response.base.status;
    }

    if ( response.base.type == MtResponseSocketRecvFrom )
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
    return ( response.base.status < 0 ? -1 : rc );
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
    if ( !MW_SOCKET_IS_FD( Fd ) )
    {
        return libc_read( Fd, Buf, count );
    }
    
    return recvfrom( Fd, Buf, count, 0,  NULL, NULL );
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
       return -1;
   }

   build_connect_socket( &request, SockFd, (struct sockaddr_in *) Addr );

   DEBUG_PRINT("\tSize of request base: %lu\n", sizeof(request));
   DEBUG_PRINT("\t\tSize of payload: %d\n", request.base.size);

#ifndef NODEVICE

   rc = libc_write( devfd, &request, sizeof(request)); 
   MYASSERT( rc > 0 );
   
   rc = read_response( (mt_response_generic_t *) &response );
   MYASSERT( rc > 0 );

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
       return -1;
   }       

   return response.base.status;
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
   
   if ( !MW_SOCKET_IS_FD( SockFd ) )
   {
       DEBUG_PRINT( "send() received invalid FD 0x%x\n", SockFd );
       errno = EINVAL;
       return -1;
   }
   
   build_send_socket( &request, SockFd, Buff, Len );

   DEBUG_PRINT("Sending write-socket request on socket number: %x\n", SockFd);
   DEBUG_PRINT("\tSize of request base: %lu\n", sizeof(request));
   DEBUG_PRINT("\t\tSize of payload: %d\n", request.base.size);

#ifndef NODEVICE

   rc = libc_write( devfd, &request, sizeof(request) ); 
   MYASSERT( rc > 0 );

   rc = read_response( (mt_response_generic_t *) &response );
   MYASSERT( rc > 0 );

#endif

   DEBUG_PRINT("Write-socket response returned status %d len %d\n",
               (int)response.base.status, rc );
   DEBUG_PRINT("\tSize of response base: %lu\n", sizeof(response));
   DEBUG_PRINT("\t\tSize of payload: %d\n", response.base.size);
   
   if ( (int)response.base.status < 0 )
   {
       errno = -response.base.status;
   }
   return ( response.base.status < 0 ? -1 : response.sent );
}


ssize_t
write( int Fd, const void *Buf, size_t count )
{
    if( !MW_SOCKET_IS_FD( Fd ) )
    {
        return libc_write( Fd, Buf, count );
    }
    
    return send( Fd, Buf, count, 0 );
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

    init_open_sockets();
    
    get_libc_symbol( (void **) &libc_read,     "read"     );
    get_libc_symbol( (void **) &libc_write,    "write"    );
    get_libc_symbol( (void **) &libc_close,    "close"    );
    get_libc_symbol( (void **) &libc_send,     "send"     );
    get_libc_symbol( (void **) &libc_sendto,   "sendto"   );
    get_libc_symbol( (void **) &libc_recv,     "recv"     );
    get_libc_symbol( (void **) &libc_recvfrom, "recvfrom" );
}

void
_fini( void )
{
    // In case of process crash, the kernel will close all the open
    // FDs for us. However, the FDs from Rump are not registered with
    // the kernel. We should have an easy way to do this, e.g. tell
    // Rump to close all sockets associated with this PID.

    fini_open_sockets();
    
    if ( g_dlh_libc )
    {
        dlclose( g_dlh_libc );
        g_dlh_libc = NULL;
    }

   DEBUG_PRINT("Intercept module unloaded\n");
}
