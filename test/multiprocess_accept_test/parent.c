/*
This source file taken from the stackoverflow question:

https://stackoverflow.com/questions/22323975/how-to-test-the-functionality-of-sock-cloexec-o-colexec-close-on-execution

*/

/*
  gcc -g -Wall -Wextra -std=c99 parent.c
*/
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>


#define NUM_CHILDREN 2


int main (void)
{
    int ld = -1;
    int ret = -1;
    struct sockaddr_in serv;
    int reuse = 1;
    pid_t pid = 0;
    pid_t w = 0;
    int status = 0;
    
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = htonl(INADDR_ANY);
    serv.sin_port = htons(4567);

    ld = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ld < 0 )
        perror("socket");

    ret = setsockopt(ld, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (ret < 0)
        perror("setsockopt");

    ret = bind(ld, (struct sockaddr *) &serv, sizeof(serv));
    if (ret < 0)
    {
        perror("bind");
    }
    else
    {
        printf("Bound to port %d\n", ntohs(serv.sin_port) );
    }
    
    ret = listen(ld, 1);
    if (ret < 0)
        perror("listen");

    int pid_arr[NUM_CHILDREN] = {0};
    
    for( int i = 0; i < NUM_CHILDREN; i++ )
    {
        pid = fork();

        if (pid < 0)
            perror("fork");

        if( pid == 0 )
            break;

        pid_arr[i] = pid;
        
    }


    if (pid == 0) {

        char adbuf[2] = {0};
        char * params [3];

        adbuf[0] = ld + '0'; /* kluge for singe digit numbers */
        params[0] = "./child";
        params[1] = adbuf;
        params[2] = NULL;

        execv("./child", params);
        /* execv() does not return on success */
        perror("execv");
    }

    for( int i = 0; i < NUM_CHILDREN; i++ )
    {
        /* from man waitpid */
        do {
            w = waitpid(pid_arr[i], &status, WUNTRACED | WCONTINUED);
            if (w == -1) {
                perror("waitpid");
                exit(EXIT_FAILURE);
            }

            printf("waitpid returned for process: %d \n",
                   pid_arr[i] );

            if (WIFEXITED(status)) {
                printf("exited, status=%d\n", WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                printf("killed by signal %d\n", WTERMSIG(status));
            } else if (WIFSTOPPED(status)) {
                printf("stopped by signal %d\n", WSTOPSIG(status));
            } else if (WIFCONTINUED(status)) {
                printf("continued\n");
            }
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    close(ld);
    exit(EXIT_SUCCESS);
}
