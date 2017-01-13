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
#include <pthread.h>

#include "sock_info_list.h"
#include <assert.h>
#include <message_types.h>
#include <translate.h>

#define DEV_FILE "/dev/mwchar"
#define BUF_SZ   1024

#define SERVER_NAME "rumprun-echo_server-rumprun.bin"
#define SERVER_IP   "10.0.2.15"
#define SERVER_PORT 21845 

static int fd;
static int request_id;

sinfo_t          sock_info;
struct list     *list;

pthread_mutex_t  create_lock;
pthread_mutex_t  close_lock;
pthread_mutex_t  connect_lock;
pthread_mutex_t  send_lock;
pthread_mutex_t  bind_lock;
pthread_mutex_t  accept_lock;
pthread_mutex_t  listen_lock;
pthread_mutex_t  accept_lock;
pthread_mutex_t  recv_lock;


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
                    sinfo_t * SockInfo )
{
    mt_request_socket_close_t * csock = &(Request->socket_close);

    bzero( Request, sizeof(*Request) );

    csock->base.sig  = MT_SIGNATURE_REQUEST;
    csock->base.type = MtRequestSocketClose;
    csock->base.size = MT_REQUEST_SOCKET_CLOSE_SIZE; 
    csock->base.id = request_id++;
    csock->base.sockfd = SockInfo->sockfd;
}



void
build_bind_socket( 
        mt_request_generic_t * Request, 
        int sockfd, 
        struct sockaddr_in * SockAddr, 
        socklen_t Addrlen 
        )
{

    mt_request_socket_bind_t * bind = &(Request->socket_bind);

    bzero( Request, sizeof(*Request) );

    populate_mt_sockaddr_in( &bind->sockaddr, SockAddr );

    bind->base.sig  = MT_SIGNATURE_REQUEST;
    bind->base.type = MtRequestSocketBind;
    bind->base.id = request_id++;
    bind->base.sockfd = sockfd;

    bind->base.size = MT_REQUEST_SOCKET_BIND_SIZE; 
}


void
build_listen_socket( mt_request_generic_t * Request,
                     int sockfd,
                     int * backlog)
{
    mt_request_socket_listen_t * listen = &(Request->socket_listen);
    
    bzero( Request, sizeof(*Request) );

    listen->backlog = *backlog;
    listen->base.size = MT_REQUEST_SOCKET_LISTEN_SIZE;

    listen->base.sig = MT_SIGNATURE_REQUEST;
    listen->base.type = MtRequestSocketListen;
    listen->base.id = request_id++;
    listen->base.sockfd = sockfd;

    listen->base.size = MT_REQUEST_SOCKET_LISTEN_SIZE;
}

void build_accept_socket( mt_request_generic_t * Request,
                          int sockfd)
{
    mt_request_socket_accept_t * accept = &(Request->socket_accept);

    bzero( Request, sizeof(*Request) );

    accept->base.sig = MT_SIGNATURE_REQUEST;
    accept->base.type = MtRequestSocketAccept;
    accept->base.id = request_id++;
    accept->base.sockfd = sockfd;

    accept->base.size = MT_REQUEST_SOCKET_ACCEPT_SIZE;
}


void
build_connect_socket( mt_request_generic_t * Request, 
                      sinfo_t * SockInfo )
{
    mt_request_socket_connect_t * connect = &(Request->socket_connect);

    bzero( Request, sizeof(*Request) );

    connect->base.sig  = MT_SIGNATURE_REQUEST;
    connect->base.type = MtRequestSocketConnect;
    connect->base.id = request_id++;
    connect->base.sockfd = SockInfo->sockfd;

    connect->port = SockInfo->destport;

    strcpy( (char *) connect->hostname, SockInfo->desthost );
    connect->base.size = MT_REQUEST_SOCKET_CONNECT_SIZE + strlen( SERVER_IP ) + 1; 
}

void
build_send_socket( mt_request_generic_t * Request, 
                   sinfo_t              * SockInfo,
                   int                  * client_sockfd,
                   const void           * bytes,
                   size_t               len )
{
    mt_request_socket_send_t * send = &(Request->socket_send);
    size_t actual_len = len;

    bzero( Request, sizeof(*Request) );

    send->base.sig  = MT_SIGNATURE_REQUEST;
    send->base.type = MtRequestSocketSend;
    send->base.id = request_id++;
    send->base.sockfd = SockInfo->sockfd;
    
    if( len > MESSAGE_TYPE_MAX_PAYLOAD_LEN )
    {
        actual_len = MESSAGE_TYPE_MAX_PAYLOAD_LEN;
    }

    send->len = actual_len;
    send->client_sockfd = *client_sockfd;

    memcpy(send->bytes, bytes, actual_len);

    // Fix to handle non-ascii payload
    send->base.size = MT_REQUEST_SOCKET_SEND_SIZE + actual_len;
}

int 
socket( int domain, 
        int type, 
        int protocol )
{

   pthread_mutex_lock(&create_lock);

   mt_request_generic_t  request;
   mt_response_generic_t response;

   build_create_socket( &request );

   //printf("Sending socket-create request\n");
   //printf("\tSize of request base: %lu\n", sizeof(request));
   //printf("\t\tSize of payload: %d\n", request.base.size);

#ifndef NODEVICE
   write(fd, &request, sizeof(request)); 

   read(fd, &response, sizeof(response));
#endif
   sock_info.sockfd = response.base.sockfd;

   // Add Connection to List
   add_sock_info( list, &sock_info );

   //printf("Create-socket response returned\n");
   //printf("\tSize of response base: %lu\n", sizeof(response));
   //printf("\t\tSize of payload: %d\n", response.base.size);

   if ( response.base.status )
   {
      printf( "\t\tError creating socket. Error Number: %u\n", response.base.status );
      // Returns -1 on error
      return -1;
   }

   pthread_mutex_unlock(&create_lock);

   // Returns socket number on success
   return sock_info.sockfd;
}

int
close( int sock_fd )
{
 
   pthread_mutex_lock(&close_lock);

   mt_request_generic_t  request;
   mt_response_generic_t response;

   if (sock_info.sockfd <= 0)
   {
      printf("Socket file descriptor value invalid\n");
      return 1;
   }
      
   build_close_socket( &request, &sock_info );

   printf("Sending close-socket request on socket number: %d\n", sock_info.sockfd);
   //printf("\tSize of request base: %lu\n", sizeof(request));
   //printf("\t\tSize of payload: %d\n", request.base.size);

#ifndef NODEVICE
   write(fd, &request, sizeof(request)); 

   read(fd, &response, sizeof(response));
#endif

   //printf("Close-socket response returned\n");
   //printf("\tSize of response base: %lu\n", sizeof(response));
   //printf("\t\tSize of payload: %d\n", response.base.size);

   if ( response.base.status )
   {
      printf( "\t\tError closing socket. Error Number: %u\n", response.base.status );
      // Returns -1 on error
      return -1;
   }

   pthread_mutex_unlock(&close_lock);

   // Returns 0 on success
   return response.base.status;
}


int
bind( int SockFd,
      const struct sockaddr * SockAddr, 
      socklen_t addrlen )
{
    pthread_mutex_lock(&bind_lock);

    mt_request_generic_t request;
    mt_response_generic_t response;
    struct sockaddr_in * sockaddr_in;

    if ( SockFd <= 0 )
    {
        printf("Socket file discriptor value invalid\n");
        return 1;
    }   

    if ( SockAddr->sa_family != AF_INET || addrlen != sizeof(struct sockaddr_in) )
    {
        perror("Only AF_INET is supported at this time\n");
        return 1;
    }

    sockaddr_in = ( struct sockaddr_in * ) SockAddr;

    build_bind_socket( &request, SockFd, sockaddr_in, addrlen);

#ifndef NODEVICE    
    write( fd, &request, sizeof(request) );

    read( fd, &response, sizeof(response) );
#endif

    pthread_mutex_lock(&bind_lock);

    return response.base.status;
}




int
listen( int sockfd, int backlog )
{
    pthread_mutex_lock(&listen_lock);
    
    mt_request_generic_t request;
    mt_response_generic_t response;

    if ( sock_info.sockfd <= 0 )
    {
        printf("Socket file discriptor value invalid\n");
        return 1;
    }
    

    build_listen_socket( &request, sockfd, &backlog);

#ifndef NODEVICE
    write( fd, &request, sizeof(request) );

    read( fd, &response, sizeof(response) );
#endif

    pthread_mutex_lock(&listen_lock);

    return response.base.status;

}


int
accept( int sockfd, 
        struct sockaddr * SockAddr, 
        socklen_t * socklen)
{

    pthread_mutex_lock(&accept_lock);

    mt_request_generic_t request;
    mt_response_generic_t response;

    if ( sockfd <= 0 )
    {
        printf("Socket file discriptor value invalid");
        return 1;
    }
    
    build_accept_socket(&request, sockfd);

#ifndef NODEVICE
    write( fd, &request, sizeof(request) );
    
    read( fd, &response, sizeof(response) );
#endif

    populate_sockaddr_in( (struct sockaddr_in *)SockAddr, &response.socket_accept.sockaddr);
    *socklen = sizeof(struct sockaddr_in);

    pthread_mutex_lock(&accept_lock);

    return response.base.status;

}


void
build_recv_socket( sinfo_t * SockInfo,
                   int ClientSockFd,
                   size_t MessageSize,
                   int Flags,
                   mt_request_generic_t * Request )
{
   mt_request_socket_recv_t * recieve = &(Request->socket_recv);

    bzero( Request, sizeof(*Request) );

    recieve->base.sig  = MT_SIGNATURE_REQUEST;
    recieve->base.type = MtRequestSocketRecv;
    recieve->base.id = request_id++;
    recieve->base.sockfd = SockInfo->sockfd;
    
    recieve->requested = MessageSize;
    recieve->flags = Flags;
    recieve->client_sockfd = ClientSockFd;
    
    recieve->base.size = MT_REQUEST_SOCKET_RECV_SIZE;
}


ssize_t
recv(int ClientSockFd, void* Buff, size_t MessageSize, int Flags )
{
    pthread_mutex_lock(&recv_lock);
    
    mt_request_generic_t request;
    mt_response_generic_t response;

    build_recv_socket( &sock_info, ClientSockFd, MessageSize, Flags, &request );
    
    write( fd, &request, sizeof(request) );

    read( fd, &response, sizeof(response) );

    pthread_mutex_unlock(&recv_lock);

    return response.socket_recv.bytes_read;
}


int 
connect( int sockfd, 
         const struct sockaddr *addr,
         socklen_t addrlen )
{
   pthread_mutex_lock(&connect_lock);

   mt_request_generic_t request;
   mt_response_generic_t response;
   sinfo_t   *sock_info_in_list;

   if (sock_info.sockfd <= 0)
   {
      printf("Socket file descriptor value invalid\n");
      return 1;
   }

   sock_info.desthost = SERVER_IP; 
   sock_info.destport = SERVER_PORT; 

   sock_info_in_list = find_sock_info(list, sockfd);
   sock_info_in_list->desthost = SERVER_IP; 
   sock_info_in_list->destport = SERVER_PORT; 

   build_connect_socket( &request, &sock_info );

   printf("Sending connect-socket request on socket number: %d\n", sock_info.sockfd);
   printf("\tSize of request base: %lu\n", sizeof(request));
   printf("\t\tSize of payload: %d\n", request.base.size);

#ifndef NODEVICE
   write(fd, &request, sizeof(request)); 

   read(fd, &response, sizeof(response));
#endif

   printf("Connect-socket response returned\n");
   printf("\tSize of response base: %lu\n", sizeof(response));
   printf("\t\tSize of payload: %d\n", response.base.size);

   if ( response.base.status )
   {
      printf( "\t\tError connecting. Error Number: %u\n", response.base.status );
      // Returns -1 on error
      return -1;
   }

   pthread_mutex_unlock(&connect_lock);

   // Returns 0 on success
   return response.base.status;
}

ssize_t 
send( int         sockfd, 
      const void *buf, 
      size_t      len,
      int         flags )
{
   pthread_mutex_lock(&send_lock);

   mt_request_generic_t request;
   mt_response_generic_t response;
   mt_size_t size_sent;

   if (sock_info.sockfd <= 0)
   {
      printf("Socket file descriptor value invalid\n");
      return 1;
   }

   build_send_socket( &request, &sock_info, &sockfd, buf, len );

   printf("Sending write-socket request on socket number: %d\n", sock_info.sockfd);
   printf("\tSize of request base: %lu\n", sizeof(request));
   printf("\t\tSize of payload: %d\n", request.base.size);

#ifndef NODEVICE
   write(fd, &request, sizeof(request)); 

   read(fd, &response, sizeof(response));
#endif

   printf("Write-socket response returned\n");
   printf("\tSize of response base: %lu\n", sizeof(response));
   printf("\t\tSize of payload: %d\n", response.base.size);

   if ( response.base.status == 0 )
   {
      size_sent = request.base.size - MT_REQUEST_SOCKET_SEND_SIZE;
      printf("\t\tSize sent: %u\n", size_sent);
      return size_sent; 
   }

   pthread_mutex_unlock(&send_lock);

   // Returns -1 on error
   return -1;
}

void 
_init( void )
{
    request_id = 0;
    memset( &sock_info, 0, sizeof(sinfo_t) );


    printf("Intercept module loaded\n");

#ifdef NODEVICE
    fd = open("/dev/null", O_RDWR);
#else
    fd = open( DEV_FILE, O_RDWR);
#endif

    if (fd < 0) {
        perror("Failed to open the device...");
   }

   list = create_sock_info_list();
   assert (list);

   pthread_mutex_init(&create_lock, NULL);
   pthread_mutex_init(&close_lock, NULL);
   pthread_mutex_init(&connect_lock, NULL);
   pthread_mutex_init(&send_lock, NULL);
   pthread_mutex_init(&bind_lock, NULL);
   pthread_mutex_init(&listen_lock, NULL);
   pthread_mutex_init(&accept_lock, NULL);
   pthread_mutex_init(&recv_lock, NULL);
}

void
_fini( void )
{
    
   list_iterator iterator;
   sinfo_t       sock_info_in_list;

   printf ( "\nNumber of sockets in list: %d\n", count_list_members( list ));

   iterator = get_list_iterator(list);

   while (is_another_list_member_available(iterator) ) 
   {
       get_next_sock_info( &iterator, &sock_info_in_list);

       printf( "sock_info_in_list.sockfd: %d\n", sock_info_in_list.sockfd );
       printf( "sock_info_in_list.desthost: %s\n", sock_info_in_list.desthost );
       printf( "sock_info_in_list.destport: %d\n", sock_info_in_list.destport );

   }

   destroy_sock_info( list, sock_info.sockfd );
   destroy_list( &list );

   pthread_mutex_destroy(&create_lock);
   pthread_mutex_destroy(&close_lock);
   pthread_mutex_destroy(&connect_lock);
   pthread_mutex_destroy(&send_lock);
   pthread_mutex_destroy(&bind_lock);
   pthread_mutex_destroy(&listen_lock);
   pthread_mutex_destroy(&accept_lock);
   pthread_mutex_destroy(&recv_lock);

   printf("Intercept module unloaded\n");
}
