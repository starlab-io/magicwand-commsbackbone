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

#include <message_types.h>

#define DEV_FILE "/dev/mwchar"
#define BUF_SZ   1024

static int fd;
static int request_id;

typedef struct _sinfo {
    int    sockfd;
    char * desthost;
    int    destport;
} sinfo_t;

sinfo_t sock_info;

void
build_create_socket( mt_request_generic_t * Request )
{
    mt_request_socket_create_t * create = &(Request->socket_create);
    
    bzero( Request, sizeof(*Request) );

    create->base.sig = MT_SIGNATURE_REQUEST;
    create->base.type = MtRequestSocketCreate;
    create->base.size = 0; 
    create->base.id = request_id++;
    create->base.sockfd = 0;

    create->sock_fam = MT_PF_INET;
    create->sock_type = MT_ST_STREAM;
    create->sock_protocol = 0;
}

void
build_close_socket( mt_request_generic_t * Request, sinfo_t * SockInfo )
{
    mt_request_socket_close_t * csock = &(Request->socket_close);

    bzero( Request, sizeof(*Request) );

    csock->base.sig  = MT_SIGNATURE_REQUEST;
    csock->base.type = MtRequestSocketClose;
    csock->base.size = 0; 
    csock->base.id = request_id++;
    csock->base.sockfd = SockInfo->sockfd;
}
 
int 
socket(int domain, int type, int protocol)
{

   //int sockfd = 0;
 
   mt_request_generic_t request;
   mt_response_generic_t response;

   build_create_socket( &request );

   printf("Sending socket-create request\n");
   printf("\tSize of request base: %lu\n", sizeof(request));
   printf("\t\tSize of payload: %d\n", request.base.size);

   write(fd, &request, sizeof(request)); 

   read(fd, &response, sizeof(response));

   sock_info.sockfd = response.base.sockfd;
   //sockfd = response.base.sockfd;

   printf("Create-socket response returned\n");
   printf("\tSize of response base: %lu\n", sizeof(response));
   printf("\t\tSize of payload: %d\n", response.base.size);

   //return sockfd;
   return sock_info.sockfd;
}

int
close(int sock_fd)
{
 
   mt_request_generic_t request;
   mt_response_generic_t response;

   //sinfo_t sock_info;
   //memset(&sock_info, 0, sizeof(sinfo_t));
   //sock_info.sockfd = sock_fd; 

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

   return 0;
}

void _init(void)
{
    request_id = 0;
    memset(&sock_info, 0, sizeof(sinfo_t));

    printf("Intercept module loaded\n");

    fd = open("/dev/mwchar", O_RDWR);

    if (fd < 0) {
        perror("Failed to open the device...");
        return;
   }
}
