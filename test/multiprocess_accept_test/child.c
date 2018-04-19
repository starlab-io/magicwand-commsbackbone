/*
This source file taken from the stackoverflow question:

https://stackoverflow.com/questions/22323975/how-to-test-the-functionality-of-sock-cloexec-o-colexec-close-on-execution

*/

/*
  gcc -g -Wall -Wextra -std=c99 -D _GNU_SOURCE child.c -o child
*/
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MSG_LEN 1024

int main (int argc, char *argv[])
{
    char msg[MSG_LEN] = {0};
    ssize_t written = 0;
    int ld, ad = -1;
    
    
    if (argc < 2) {
        printf("Too few arguments to child\n");
        exit(EXIT_FAILURE);
    }
    ld = atoi(argv[1]); /* atoi() does not detect errors properly */
    
    ad = accept4( ld, NULL, NULL, SOCK_CLOEXEC );
    if( ad < 0 )
    {
        perror("Child accept");
        exit( EXIT_FAILURE );
    }

    snprintf( (char*)&msg, MSG_LEN, "Hello from pid: %d\n", getpid() );
    
    written = write(ad, msg, sizeof(msg));
    if (written != sizeof(msg)) {
        perror("write");
        exit(EXIT_FAILURE);
    }

    close(ad);
    close(ld);
    exit(EXIT_SUCCESS);
}
