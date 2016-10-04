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

 
int socket(int domain, int type, int protocol)
{

   int sockfd = 0;
 
   mt_request_generic_t request;
   mt_response_generic_t response;

   build_create_socket( &request );

   write(fd, &request, sizeof(request)); 

   read(fd, &response, sizeof(response));

   sockfd = response.base.sockfd;

   printf("Size of base: %lu\n", sizeof(mt_request_base_t));

   return sockfd;
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
