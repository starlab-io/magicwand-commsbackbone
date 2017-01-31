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

#define DEV_FILE "/dev/mwchar"
#define BUF_SZ   1024

#define SERVER_NAME "rumprun-echo_server-rumprun.bin"
#define SERVER_IP   "10.0.2.15"
#define SERVER_PORT 21845 

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
    *Addr = dlsym( g_dlh_libc, Symbol );
    if ( NULL == *Addr )
    {
        printf( "Failure: %s\n", dlerror() );
    }

    return *Addr;
}


void
build_create_socket( mt_request_generic_t * Request )
{
    mt_request_socket_create_t * create = &(Request->socket_create);
    
    bzero( Request, sizeof(*Request) );

    create->base.sig = MT_SIGNATURE_REQUEST;
    create->base.type = MtRequestSocketCreate;
    create->base.size = MT_REQUEST_SOCKET_CREATE_SIZE;
    create->base.id = request_id++;
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
    csock->base.id = request_id++;
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
    bind->base.id = request_id++;
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
    listen->base.id = request_id++;
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
    accept->base.id = request_id++;
    accept->base.sockfd = SockFd;

    accept->base.size = MT_REQUEST_SOCKET_ACCEPT_SIZE;
}


void
build_connect_socket( mt_request_generic_t * Request,
                      const char * SockDestHost,
                      int       SockFd,
                      uint16_t  SockPort )
{
    mt_request_socket_connect_t * connect = &(Request->socket_connect);

    bzero( Request, sizeof(*Request) );

    connect->base.sig  = MT_SIGNATURE_REQUEST;
    connect->base.type = MtRequestSocketConnect;
    connect->base.id   = request_id++;
    connect->base.sockfd = SockFd;

    connect->port = SockPort;

    strncpy( (char *) connect->hostname,
             SockDestHost,
             sizeof( connect->hostname ) );

    connect->base.size = MT_REQUEST_SOCKET_CONNECT_SIZE
        + strlen( SockDestHost ) + 1; 
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
    send->base.id = request_id++;
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

   // XXXX: args ignored
   build_create_socket( &request );

   //printf("Sending socket-create request\n");
   //printf("\tSize of request base: %lu\n", sizeof(request));
   //printf("\t\tSize of payload: %d\n", request.base.size);

#ifndef NODEVICE
   write( devfd, &request, sizeof(request)); 
   read( devfd, &response, sizeof(response));
#endif

   if ( response.base.status )
   {
      printf( "\t\tError creating socket. Error Number: %ld\n", response.base.status );
      // Returns -1 on error
      return -1;
   }

   // Returns socket number on success
   return response.base.sockfd;
}

int
close( int SockFd )
{
    mt_request_generic_t  request;
    mt_response_generic_t response;

    if ( !MW_SOCKET_IS_FD( SockFd ) )
    {
       return libc_close( SockFd );
    }
    
    build_close_socket( &request, SockFd );

    printf("Sending close-socket request on socket number: %d\n", sock_info.sockfd);
    //printf("\tSize of request base: %lu\n", sizeof(request));
    //printf("\t\tSize of payload: %d\n", request.base.size);

#ifndef NODEVICE
    write( devfd, &request, sizeof(request)); 
    read( devfd, &response, sizeof(response));
#endif

    //printf("Close-socket response returned\n");
    //printf("\tSize of response base: %lu\n", sizeof(response));
    //printf("\t\tSize of payload: %d\n", response.base.size);

    if ( response.base.status )
    {
        printf( "\t\tError closing socket. Error Number: %lu\n", response.base.status );
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

    if ( !MW_SOCKET_IS_FD(SockFd) )
    {
        printf("Socket file discriptor value invalid\n");
        errno = ENOTSOCK;
        return -1;
    }

    if ( SockAddr->sa_family != AF_INET || addrlen != sizeof(struct sockaddr_in) )
    {
        perror("Only AF_INET is supported at this time\n");
        errno = ENOSUPP;
        return -1;
    }

    sockaddr_in = ( struct sockaddr_in * ) SockAddr;

    build_bind_socket( &request, SockFd, sockaddr_in, addrlen);

#ifndef NODEVICE    
    write( fd, &request, sizeof(request) );

    read( fd, &response, sizeof(response) );
#endif

    return response.base.status;
}




int
listen( int SockFd, int backlog )
{
    mt_request_generic_t request;
    mt_response_generic_t response;

    if ( !MW_SOCKET_IS_FD( SockFd ) )
    {
        printf("Socket file discriptor value invalid\n");
        errno = ENOTSOCK;
        return -1;
    }

    build_listen_socket( &request, SockFd, &backlog);

#ifndef NODEVICE
    write( fd, &request, sizeof(request) );

    read( fd, &response, sizeof(response) );
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

    if ( !MW_SOCKET_IS_FD( SockFd ) )
    {
        printf("Socket file discriptor value invalid");
        errno = ENOTSOCK;
        return -1;
    }
    
    build_accept_socket(&request, SockFd);

#ifndef NODEVICE
    write( fd, &request, sizeof(request) );
    
    read( fd, &response, sizeof(response) );
#endif

    populate_sockaddr_in( (struct sockaddr_in *)SockAddr, &response.socket_accept.sockaddr);

//    sock_info.sockfd = response.base.status;
//    add_sock_info( list, &sock_info );

    if ( response.base.status < 0 )
    {
        errno = -response.base.status;
        return -1;
    }
    else
    {
        return response.base.status;
    }

}


void
build_recv_socket( int SockFd,
                   size_t Len,
                   int Flags,
                   mt_request_generic_t * Request )
{
   mt_request_socket_recv_t * recieve = &(Request->socket_recv);

    bzero( Request, sizeof(*Request) );

    recieve->base.sig  = MT_SIGNATURE_REQUEST;
    recieve->base.type = MtRequestSocketRecv;
    recieve->base.id = request_id++;
    recieve->base.sockfd = SockFd;
    
    if( Len > MESSAGE_TYPE_MAX_PAYLOAD_LEN )
    {
       Len = MESSAGE_TYPE_MAX_PAYLOAD_LEN;
    } 

    recieve->requested = Len;
    recieve->flags     = Flags;
    
    recieve->base.size = MT_REQUEST_SOCKET_RECV_SIZE;
}


ssize_t
recv(int SockFd, void* Buf, size_t Len, int Flags )
{
    mt_request_generic_t request;
    mt_response_generic_t response;
    ssize_t rc = 0;

    if ( !MW_SOCKET_IS_FD(SockFd) )
    {
        printf("Socket file discriptor value invalid\n");
        errno = -ENOTSOCK;
        return -1;
    }

    build_recv_socket( SockFd, Len, Flags, &request );
    
    write( fd, &request, sizeof(request) );
    read( fd, &response, sizeof(response) );

    rc = response.base.size - MT_RESPONSE_SOCKET_RECV_SIZE;
    if ( rc > 0 )
    {
        memcpy( Buf, response.socket_recv.bytes, rc );
    }

    if ( response.base.status < 0 )
    {
        errno = -response.base.status;
    }

    return ( response.base.status < 0 ? -1 : rc );
}


int 
connect( int SockFd, 
         const struct sockaddr * Addr,
         socklen_t AddrLen )
{
   mt_request_generic_t request;
   mt_response_generic_t response;
   sinfo_t   *sock_info_in_list;

   if ( !MW_SOCKET_IS_FD( SockFd ) )
   {
       printf("Socket file discriptor value invalid\n");
       errno = ENOTSOCK;
       return -1;
   }

   // ???????
   build_connect_socket( &request,
                         Addr->sin_addr.s_addr,
                         Addr->sin_port,
                         SockFd );

   printf("Sending connect-socket request on socket number: %d\n", SockFd );
   printf("\tSize of request base: %lu\n", sizeof(request));
   printf("\t\tSize of payload: %d\n", request.base.size);

#ifndef NODEVICE
   write( devfd, &request, sizeof(request)); 
   read( devfd, &response, sizeof(response));
#endif

   printf("Connect-socket response returned\n");
   printf("\tSize of response base: %lu\n", sizeof(response));
   printf("\t\tSize of payload: %d\n", response.base.size);

   if ( response.base.status )
   {
       printf( "\t\tError connecting. Error Number: %lu\n", response.base.status );
       errno = -response.base.status;
   }       

   return ( response.base.status < 0 ? -1 : response.base.status );
}

ssize_t 
send( int         SockFd, 
      const void *Buff, 
      size_t      Len,
      int         Flags )
{
   mt_request_generic_t request;
   mt_response_socket_send_t response;

   if ( !MW_SOCKET_IS_FD( SockFd ) )
   {
       printf( "send() received invalid FD 0x%x\n", SockFd );
       errno = EINVAL;
       return -1;
   }
   
   build_send_socket( &request, SockFd, Buff, Len );

   printf("Sending write-socket request on socket number: %d\n", SockFd);
   printf("\tSize of request base: %lu\n", sizeof(request));
   printf("\t\tSize of payload: %d\n", request.base.size);

#ifndef NODEVICE
   write( devfd, &request, sizeof(request) ); 

   read( devfd, &response, sizeof(response) );
#endif

   printf("Write-socket response returned\n");
   printf("\tSize of response base: %lu\n", sizeof(response));
   printf("\t\tSize of payload: %d\n", response.base.size);
   
   if ( response.base.status < 0 )
   {
       errno = -response.base.status;
   }
   return ( response.base.status < 0 ? -1 : response.sent );
}

void 
_init( void )
{
    request_id = 0;
    memset( &sock_info, 0, sizeof(sinfo_t) );

    printf("Intercept module loaded\n");

#ifdef NODEVICE
    devfd = open("/dev/null", O_RDWR);
#else
    devfd = open( DEV_FILE, O_RDWR);
#endif

    if (devfd < 0)
    {
        perror("Failed to open the device...");
        exit(1);
    }

    g_dlh_libc = dlopen( "libc.so.6", RTLD_NOW );
    if ( NULL == g_dlh_libc )
    {
        printf("Failure: %s\n", dlerror() );
        exit(1);
    }

    get_libc_symbol( &libc_read,     "read"     );
    get_libc_symbol( &libc_write,    "write"    );
    get_libc_symbol( &libc_close     "close"    );
    get_libc_symbol( &libc_send,     "send"     );
    get_libc_symbol( &libc_sendto,   "sendto"   );
    get_libc_symbol( &libc_recv,     "recv"     );
    get_libc_symbol( &libc_recvfrom, "recvfrom" );
}

void
_fini( void )
{
    if ( g_dlh_libc )
    {
        dlclose( g_dlh_libc );
        g_dlh_libc = NULL;
    }
}
