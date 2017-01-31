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

#include "sock_info_list.h"
#include <assert.h>
#include <message_types.h>
#include <translate.h>

#define DEV_FILE "/dev/mwchar"
#define BUF_SZ   1024


static int fd;
static int request_id;

sinfo_t          sock_info;
struct list     *list;


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
                    int SockFd )
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
                      int SockFd,
                      struct sockaddr_in *SockAddr )
{
    mt_request_socket_connect_t * connect = &(Request->socket_connect);

    bzero( Request, sizeof(*Request) );

    connect->base.sig  = MT_SIGNATURE_REQUEST;
    connect->base.type = MtRequestSocketConnect;
    connect->base.id = request_id++;
    connect->base.sockfd = SockFd;

    populate_mt_sockaddr_in( &Request->socket_connect.sockaddr, SockAddr );
    
    connect->base.size = MT_REQUEST_SOCKET_CONNECT_SIZE;
}

void
build_send_socket( mt_request_generic_t * Request, 
                   int                  SockFd,
                   const void           * Bytes,
                   size_t               Len )
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

   build_create_socket( &request );

   //printf("Sending socket-create request\n");
   //printf("\tSize of request base: %lu\n", sizeof(request));
   //printf("\t\tSize of payload: %d\n", request.base.size);

#ifndef NODEVICE
   write(fd, &request, sizeof(request)); 

   read(fd, &response, sizeof(response));
#endif

   sock_info.sockfd = response.base.status;


   //printf("Create-socket response returned\n");
   //printf("\tSize of response base: %lu\n", sizeof(response));
   //printf("\t\tSize of payload: %d\n", response.base.size);

   if ( response.base.status < 0)
   {
      printf( "\t\tError creating socket. Error Number: %ld\n", response.base.status );
      // Returns -1 on error
      return -1;
   }

   // Add Connection to List
   add_sock_info( list, &sock_info );


   // Returns socket number on success
   return response.base.status;
}

int
close( int sock_fd )
{
 

   mt_request_generic_t  request;
   mt_response_generic_t response;

   if (sock_info.sockfd <= 0)
   {
      printf("Socket file descriptor value invalid\n");
      return 1;
   }
      
   build_close_socket( &request, sock_fd );

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


    return response.base.status;
}




int
listen( int sockfd, int backlog )
{
    
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


    return response.base.status;

}


int
accept( int SockFd, 
        struct sockaddr * SockAddr, 
        socklen_t * SockLen)
{


//    sinfo_t sock_info;

    mt_request_generic_t request;
    mt_response_generic_t response;

    if ( SockFd <= 0 )
    {
        printf("Socket file discriptor value invalid");
        return 1;
    }
    
    build_accept_socket(&request, SockFd);

#ifndef NODEVICE
    write( fd, &request, sizeof(request) );
    
    read( fd, &response, sizeof(response) );
#endif

    populate_sockaddr_in( (struct sockaddr_in *)SockAddr, &response.socket_accept.sockaddr);

//    sock_info.sockfd = response.base.status;
//    add_sock_info( list, &sock_info );


    return response.base.status;

}


void
build_recv_socket( int SockFd,
                   size_t Len,
                   int Flags,
                   struct sockaddr *Src_Addr,
                   socklen_t *AddrLen,
                   mt_request_generic_t * Request )
{
   mt_request_socket_recv_t * recieve = &(Request->socket_recv);

    bzero( Request, sizeof(*Request) );
    
    recieve->base.sig  = MT_SIGNATURE_REQUEST;
    recieve->base.id = request_id++;
    recieve->base.sockfd = SockFd;

    if( NULL == Src_Addr )
    {
        recieve->base.type = MtRequestSocketRecv;

    }else{

        recieve->base.type = MtRequestSocketRecvFrom;
    }

    if( Len > MESSAGE_TYPE_MAX_PAYLOAD_LEN )
    {
       Len = MESSAGE_TYPE_MAX_PAYLOAD_LEN;
    } 

    recieve->len = Len;
    recieve->flags = Flags;
    
    recieve->base.size = MT_REQUEST_SOCKET_RECV_SIZE;
}

ssize_t recvfrom( int SockFd, void *Buf, size_t Len, int Flags,
                  struct sockaddr *Src_Addr, socklen_t *AddrLen )
{
    mt_request_generic_t request;
    mt_response_generic_t response;

    build_recv_socket( SockFd, Len, Flags, Src_Addr, AddrLen, &request );
    
    write( fd, &request, sizeof( request ) );

    read( fd, &response, sizeof( response ) );
    
    memcpy( Buf, response.socket_recv.buf, response.base.status );
    
    if( response.base.type == MtResponseSocketRecvFrom )
    {
        memcpy( Src_Addr, &response.socket_recvfrom.src_addr, sizeof( struct sockaddr ) );
        memcpy( AddrLen, &response.socket_recvfrom.addrlen, sizeof( socklen_t ) );
    }

    return ( ssize_t ) response.base.status;
}


ssize_t
recv(int SockFd, void* Buf, size_t Len, int Flags )
{
    return recvfrom( SockFd, Buf, Len, Flags, NULL, NULL);
}


int 
connect( int sockfd, 
         const struct sockaddr *sockaddr,
         socklen_t addrlen )
{

   mt_request_generic_t request;
   mt_response_generic_t response;
//   sinfo_t   *sock_info_in_list;

   if ( sockfd <= 0)
   {
      printf("Socket file descriptor value invalid\n");
      return 1;
   }

//   sock_info_in_list = find_sock_info(list, sockfd);


   build_connect_socket( &request, sockfd, (struct sockaddr_in *) sockaddr );

   printf("Sending connect-socket request on socket number: %d\n", sock_info.sockfd);
   printf("\tSize of request base: %lu\n", sizeof(request));
   printf("\t\tSize of payload: %d\n", request.base.size);

#ifndef NODEVICE
   write(fd, &request, sizeof( request ) ); 

   read(fd, &response, sizeof( response ) );
#endif

   printf("Connect-socket response returned\n");
   printf("\tSize of response base: %lu\n", sizeof(response));
   printf("\t\tSize of payload: %d\n", response.base.size);

   if ( response.base.status < 0 )
   {
      printf( "\t\tError connecting. Error Number: %lu\n", response.base.status );
      // Returns -1 on error
      return -1;
   }


   // Returns 0 on success
   return response.base.status;
}

ssize_t 
send( int         SockFd, 
      const void *Buff, 
      size_t      Len,
      int         Flags )
{

   mt_request_generic_t request;
   mt_response_generic_t response;

   if (sock_info.sockfd <= 0)
   {
      printf("Socket file descriptor value invalid\n");
      return 1;
   }

   build_send_socket( &request, SockFd, Buff, Len );

   printf("Sending write-socket request on socket number: %d\n", SockFd);
   printf("\tSize of request base: %lu\n", sizeof(request));
   printf("\t\tSize of payload: %d\n", request.base.size);

#ifndef NODEVICE
   write(fd, &request, sizeof(request)); 

   read(fd, &response, sizeof(response));
#endif

   printf("Write-socket response returned\n");
   printf("\tSize of response base: %lu\n", sizeof(response));
   printf("\t\tSize of payload: %d\n", response.base.size);

   

   return response.base.status;

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


   printf("Intercept module unloaded\n");
}
