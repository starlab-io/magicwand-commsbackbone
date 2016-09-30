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
 
int socket(int domain, int type, int protocol)
{

   int sockfd;
 
   mt_request_generic_t request;
   mt_response_generic_t response;

   build_create_socket( &request );

   write(fd, &request, sizeof(request)); 

   read(fd, &response, sizeof(response));

   sockfd = response.base.sockfd;

   return sockfd;
}

void _init(void)
{

    printf("Intercept module loaded\n");

    fd = open("/dev/mwchar", O_RDWR);

    if (fd < 0) {
        perror("Failed to open the device...");
        return;
   }
}
