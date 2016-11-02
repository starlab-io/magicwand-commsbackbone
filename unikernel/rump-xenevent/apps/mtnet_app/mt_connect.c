
#include <stdio.h> 
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 

#include <sys/fcntl.h>
#include <unistd.h>
#include <string.h>

#include <errno.h>

#include <sched.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/time.h>


#include "app_common.h"

// Observe differing behaviors when this is or is not defined
//#define SLEEP 1

//////////////////////////////////////////////////////////
// UPDATE THESE VALUES
//
// There must be a listener on the IP/port pair given,
// e.g. run "nc -l 21845" on the host.
//////////////////////////////////////////////////////////

// Update run.sh so the unikernel and the listener are on the same
// subnet.
const char * g_remote_host = "192.168.0.12";
const char * g_remote_port = "21845";

#define TEST_MESSAGE "Hello from multi-threaded Rump app!\n"

static int g_connect_returned = 0;

static void
local_yield( void )
{
#ifdef SLEEP
    // Sleep for 1 ns
    struct timespec ts = { 0, 1 };
    
    // Call nanosleep() until the sleep has occured for the required duration
    while ( ts.tv_nsec > 0 )
    {
        (void) nanosleep( &ts, &ts );
    }
#else
    // Yield the current thread
    sched_yield();
#endif // SLEEP
}

static void *
worker_thread_connect( void * Arg )
{
    int rc = 0;
    int sockfd = (int) Arg;

    struct addrinfo serverHints = {0};
    struct addrinfo * serverInfo = NULL;
    struct addrinfo * serverIter = NULL;

    serverHints.ai_family    = AF_INET;
    serverHints.ai_socktype  = SOCK_STREAM;

    rc = getaddrinfo( g_remote_host, g_remote_port, &serverHints, &serverInfo );
    if ( 0 != rc )
    {
        DEBUG_PRINT( "getaddrinfo failed: %s\n", gai_strerror(rc) );
        MYASSERT( !"getaddrinfo" );
        goto ErrorExit;
    }

    // Loop through all the results and connect to the first we can
    for (serverIter = serverInfo; serverIter != NULL; serverIter = serverIter->ai_next)
    {
        DEBUG_BREAK();
        DEBUG_PRINT( "calling connect()\n" );
        rc = connect( sockfd, serverIter->ai_addr, serverIter->ai_addrlen);
        g_connect_returned = 1;
        DEBUG_PRINT( "connect() ==> %d\n", rc );
        DEBUG_BREAK();
        if ( rc < 0 )
        {
            continue; // Silently continue
        }

        // If we get here, we must have connected successfully
        break; 
    }
    
    if (serverIter == NULL)
    {
        // Looped off the end of the list with no connection.
        DEBUG_PRINT( "Couldn't connect() to %s:%s\n", g_remote_host, g_remote_port );
        DEBUG_BREAK();
        rc = EINVAL;
        goto ErrorExit;
    }

    // Send test message
    (void) write( sockfd, TEST_MESSAGE, strlen(TEST_MESSAGE) );

    DEBUG_PRINT( "Wrote message to socket\n" );

    close( sockfd );
    
ErrorExit:
    pthread_exit( &rc );
}

int main(void)
{
    int rc = 0;
    int sockfd = 0;
    pthread_t threads[4];
    
    // 1. Create a socket
    sockfd = socket( AF_INET, SOCK_STREAM, 0 );
    MYASSERT( sockfd > 0 );

    // 2. Create a thread that connects using that socket
    rc = pthread_create( &threads[0],
                         NULL,
                         worker_thread_connect,
                         (void *) sockfd );
    MYASSERT( 0 == rc );
    DEBUG_PRINT( "Worker thread spawned\n" );

    // Allow other thread(s) to run. They might not run, depending on
    // how we implement this.
    local_yield();

    if ( g_connect_returned )
    {
        DEBUG_PRINT( "OK: Connect completed before join()\n" );
    } else {
        DEBUG_PRINT( "Bad: Connect has NOT completed before join()\n" );
    }
    
    for ( int i = 0; i < NUMBER_OF(threads); i++ )
    {
        pthread_join( threads[i], (void *)&rc );
        DEBUG_PRINT( "Worker thread joined\n" );
    }

    return rc;
}
