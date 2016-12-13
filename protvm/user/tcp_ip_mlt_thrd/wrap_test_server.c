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

#define DEV_FILE "/dev/mwchar"
#define BUF_SZ   1024

#define SERVER_NAME "rumprun-echo_server-rumprun.bin"
#define SERVER_IP   "192.168.0.4"
#define SERVER_PORT 21845 

static int fd;
static int request_id;

sinfo_t      sock_info;
struct list *list;

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
build_write_socket( mt_request_generic_t * Request, 
                    sinfo_t * SockInfo )
{
    mt_request_socket_write_t * wsock = &(Request->socket_write);

    static int msg_num = 1;
    bzero( Request, sizeof(*Request) );

    wsock ->base.sig  = MT_SIGNATURE_REQUEST;
    wsock->base.type = MtRequestSocketWrite;
    wsock->base.id = request_id++;
    wsock->base.sockfd = SockInfo->sockfd;

    snprintf( (char *) wsock->bytes, sizeof(wsock->bytes),
              "Hello from build traffic file: message %d sock %d\n",
              msg_num++, SockInfo->sockfd );

    // Fix to handle non-ascii payload
    wsock->base.size = MT_REQUEST_SOCKET_WRITE_SIZE + strlen( (const char *)wsock->bytes ) + 1;
}

int 
socket( int domain, 
        int type, 
        int protocol )
{

   mt_request_generic_t  request;
   mt_response_generic_t response;

   build_create_socket( &request );

   printf("Sending socket-create request\n");
   printf("\tSize of request base: %lu\n", sizeof(request));
   printf("\t\tSize of payload: %d\n", request.base.size);

   write(fd, &request, sizeof(request)); 

   read(fd, &response, sizeof(response));

   sock_info.sockfd = response.base.sockfd;

   // Add Connection to List
   add_sock_info( list, &sock_info );

   printf("Create-socket response returned\n");
   printf("\tSize of response base: %lu\n", sizeof(response));
   printf("\t\tSize of payload: %d\n", response.base.size);

   if ( response.base.status )
   {
      printf( "\t\tError creating socket. Error Number: %u\n", response.base.status );
      // Returns -1 on error
      return -1;
   }

   // Returns socket number on success
   return sock_info.sockfd;
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
      
   build_close_socket( &request, &sock_info );

   printf("Sending close-socket request on socket number: %d\n", sock_info.sockfd);
   printf("\tSize of request base: %lu\n", sizeof(request));
   printf("\t\tSize of payload: %d\n", request.base.size);

   write(fd, &request, sizeof(request)); 

   read(fd, &response, sizeof(response));

   printf("Close-socket response returned\n");
   printf("\tSize of response base: %lu\n", sizeof(response));
   printf("\t\tSize of payload: %d\n", response.base.size);

   if ( response.base.status )
   {
      printf( "\t\tError closing socket. Error Number: %u\n", response.base.status );
      // Returns -1 on error
      return -1;
   }

   // Returns 0 on success
   return response.base.status;
}

int 
connect( int sockfd, 
         const struct sockaddr *addr,
         socklen_t addrlen )
{

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

   write(fd, &request, sizeof(request)); 

   read(fd, &response, sizeof(response));

   printf("Connect-socket response returned\n");
   printf("\tSize of response base: %lu\n", sizeof(response));
   printf("\t\tSize of payload: %d\n", response.base.size);

   if ( response.base.status )
   {
      printf( "\t\tError connecting. Error Number: %u\n", response.base.status );
      // Returns -1 on error
      return -1;
   }

   // Returns 0 on success
   return response.base.status;
}

ssize_t 
send( int         sockfd, 
      const void *buf, 
      size_t      len,
      int         flags )
{
   mt_request_generic_t request;
   mt_response_generic_t response;
   mt_size_t size_sent;

   if (sock_info.sockfd <= 0)
   {
      printf("Socket file descriptor value invalid\n");
      return 1;
   }

   // XXXX: pass the size
   build_write_socket( &request, &sock_info );

   printf("Sending write-socket request on socket number: %d\n", sock_info.sockfd);
   printf("\tSize of request base: %lu\n", sizeof(request));
   printf("\t\tSize of payload: %d\n", request.base.size);

   write(fd, &request, sizeof(request)); 

   read(fd, &response, sizeof(response));

   printf("Write-socket response returned\n");
   printf("\tSize of response base: %lu\n", sizeof(response));
   printf("\t\tSize of payload: %d\n", response.base.size);

   if ( response.base.status == 0 )
   {
      size_sent = request.base.size - MT_REQUEST_SOCKET_WRITE_SIZE;
      printf("\t\tSize sent: %u\n", size_sent);
      return size_sent; 
   }

   // Returns -1 on error
   return -1;
}

void 
_init( void )
{
    request_id = 0;
    memset( &sock_info, 0, sizeof(sinfo_t) );
    

    printf("Intercept module loaded\n");

    fd = open( DEV_FILE, O_RDWR);

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
