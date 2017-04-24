#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <pthread.h>
#include "user_common.h"

// Limit is 100. Teardown fails if it's 101.
#define NUM_THREADS     100

void error(const char *msg)
{
    perror(msg);
    exit(0);
}

// start_routine
void *PrintHello(void *threadid)
{
    long tid;
    tid = (long)threadid;
    printf("Hello World! It's me, thread #%ld!\n", tid);
    pthread_exit(NULL);
}

int main ( void )
{
    pthread_t threads[NUM_THREADS];
    int rc;
    long t;

    printf( "In main\n" );

    for ( t=0; t<NUM_THREADS; t++ )
    {
        printf("In main: creating thread %ld\n", t);
        rc = pthread_create(&threads[t], NULL, PrintHello, (void *)t);
        if (rc)
        {
            printf("ERROR; return code from pthread_create() is %d\n", rc);
            exit(-1);
        }
        sched_yield();
    }

    // Last thing that main() should do
    for ( t=0; t<NUM_THREADS; t++ )
    {
        pthread_join( threads[t], NULL );
    }
    
    return 0;

}
