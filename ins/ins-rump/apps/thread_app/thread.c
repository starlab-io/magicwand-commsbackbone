#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <pthread.h>
#include "user_common.h"

// Limit is 100. Teardown fails if it's 101.
#define NUM_THREADS     200

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


int
setlimit( void )
{
    int rc = 0;
    int newval;
    int oldval;
    size_t newlen  = sizeof(newval);
    size_t oldlen = sizeof(newval);
#if 0
    struct rlimit lim = { .rlim_cur = 500, .rlim_max = 500 };
    rc = setrlimit( RLIMIT_NTHR, &lim );
    if ( rc )
    {
        perror( "setrlimit" );
    }
    rc = setrlimit( RLIMIT_NPROC, &lim );
    if ( rc )
    {
        perror( "setrlimit" );
    }

    // These are not working....
    rc = sysctlbyname("kern.threads.max_threads_per_proc", &oldval, &oldlen, NULL, 0 );
    if ( rc )
    {
        perror( "sysctlbyname" );
    }
    printf( "max_threads_per_proc: %d\n", oldval );

    rc = sysctlbyname("kern.maxproc", &oldval, &oldlen, NULL, 0 );
    if ( rc )
    {
        perror( "sysctlbyname" );
    }
    printf( "maxproc: %d\n", oldval );

#endif
    return 0;
}

int main ( void )
{
    pthread_t threads[NUM_THREADS];
    int rc;
    long t;
    
    printf( "In main\n" );

    setlimit();
    
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

ErrorExit:
    return 0;
}
