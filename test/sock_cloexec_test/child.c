/*
This source file taken from the stackoverflow question:

https://stackoverflow.com/questions/22323975/how-to-test-the-functionality-of-sock-cloexec-o-colexec-close-on-execution

*/

/*
  gcc -g -Wall -Wextra -std=c99 child.c -o child
*/
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

int main (int argc, char *argv[])
{
    char msg[] = "Hello\n";
    ssize_t written = 0;
    int ad = -1;

    if (argc < 2) {
        printf("Too few arguments to child\n");
        exit(EXIT_FAILURE);
    }
    ad = atoi(argv[1]); /* atoi() does not detect errors properly */
    printf("Child attempting to write to fd %d\n", ad);
    fflush(stdout);

    written = write(ad, msg, sizeof(msg));
    if (written != sizeof(msg)) {
        perror("write");
        exit(EXIT_FAILURE);
    }

    printf("Child successfully wrote to fd %d\n", ad );
    fflush(stdout);

    close(ad);
    exit(EXIT_SUCCESS);
}
