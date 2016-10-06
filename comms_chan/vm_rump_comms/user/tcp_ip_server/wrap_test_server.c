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
    int sockfd;
    char * desthost;
    int    destport;
} sinfo_t;

void
build_create_socket( mt_request_generic_t * Request )
{
    mt_request_socket_create_t * create = &(Request->socket_create);
    
    bzero( Request, sizeof(*Request) );

    create->base.type = MtRequestSocketCreate;
    create->base.id = request_id++;
    create->base.sockfd = 0;
    create->base.sig = MT_SIGNATURE_REQUEST;

    create->base.size = MT_REQUEST_SOCKET_CREATE_SIZE;
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
    csock->base.size = MT_REQUEST_SOCKET_CLOSE_SIZE;
    csock->base.id = request_id++;
    csock->base.sockfd = SockInfo->sockfd;
}
 
int 
socket(int domain, int type, int protocol)
{

   int sockfd = 0;
 
   mt_request_generic_t request;
   mt_response_generic_t response;

   build_create_socket( &request );

   printf("\tSize of request base: %lu\n", sizeof(mt_request_base_t));

   write(fd, &request, sizeof(request)); 

   read(fd, &response, sizeof(response));

   sockfd = response.base.sockfd;

   printf("socket() returned\n");
   printf("\tSize of response base: %lu\n", sizeof(mt_response_base_t));

   return sockfd;
}

int
close(int sock_fd)
{
 
   mt_request_generic_t request;
   mt_response_generic_t response;
   sinfo_t sock_info;

   memset(&sock_info,0,sizeof(sinfo_t));
   sock_info.sockfd = sock_fd; 

   build_close_socket( &request, &sock_info );

   write(fd, &request, sizeof(request)); 

   read(fd, &response, sizeof(response));

   printf("close() returned\n");
   printf("\tSize of base: %lu\n", sizeof(mt_request_base_t));

   return 0;
}

void _init(void)
{
    request_id = 0;

    printf("Intercept module loaded\n");

    fd = open("/dev/mwchar", O_RDWR);

    if (fd < 0) {
        perror("Failed to open the device...");
        return;
   }
}
