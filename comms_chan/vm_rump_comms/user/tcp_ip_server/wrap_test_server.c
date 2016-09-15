#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define DEV_FILE "/dev/mwchar"
#define BUF_SZ   1024

static int fd;
 
int socket(int domain, int type, int protocol)
{
   char msg[BUF_SZ];

   memset(msg, 0, BUF_SZ); 

   sprintf(msg, "%s\n", "Hello");

   write(fd, msg, strlen(msg)); 
 
   return 1106; 
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
