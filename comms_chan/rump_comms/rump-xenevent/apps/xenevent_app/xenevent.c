
#include <stdio.h> 
#include <stdlib.h>
#include <rump/rump_syscalls.h>
//#include <rump/rumpclient.h>
#include <rump/rump.h>
#include <rump/rumpdefs.h>

#include <sys/fcntl.h>
#include <unistd.h>
#include <string.h>

#include "networking.h"
#include "app_common.h"


#define XENEVENT_DEVICE  "/dev/xe"

#define XEN_HOST_ADDR "10.190.2.100"
#define XEN_HOST_PORT 5555
#define TEST_STRING "Hello from Rump unikernel!\n"


// Ideas for future implementation:
//
// This app should have 1 or 2 dedicated threads that processes new
// connections and/or UDP packets.
//
// When a request for a new connection arrives, it should eventually
// result in a new thread in this app. That way, a thread can be used
// to block on network traffic and/or Xen exchanges.
//
// How to handle non-blocking IO from remote side? Do we care?
//
// The NetBSD handles fragmentation and packet reordering for us. Do
// we need to do anything to maintain ordering on the Xen side?


int main(void)
{
    size_t size = 0;
    const char data[] = "abcdefghi";
    uint64_t rdata = 0;
    socket_t sock = 0;
    int rc = 0;
    
    // Use open/write/read/close. Do not use fopen,etc. They don't work right.
#define HANDLE_CT 1
    int fds[ HANDLE_CT ] = {0};

    int i = 0;

    for ( i = 0; i < HANDLE_CT; i++ )
    {
        printf( "Opening device %s\n", XENEVENT_DEVICE );
        fds[i] = open( XENEVENT_DEVICE, O_RDWR );
        
        if ( fds[i] < 0 )
        {
            printf( "Failed to open file " XENEVENT_DEVICE "\n" );
            goto ErrorExit;
            }
    }

    for ( i = 0; i < HANDLE_CT; i++ )
    {
        printf( "Writing %d bytes to FD %d\n", (int)sizeof(data), fds[i] );
        size = write( fds[i], data, sizeof(data) );
        printf( "Wrote %d bytes\n", (int)size );
    }

    //for ( i = 0; i < HANDLE_CT; i++ )
    for ( i = 0; i < 5; i++ )
    {
        printf( "Reading %d bytes to FD %d\n", (int)sizeof(rdata), fds[0] );

        // Simulation: we read a xen event from the protected
        // domain. Send a string over the network.
        size = read( fds[0], &rdata, sizeof(rdata) );
        printf( "Read %d bytes\n", (int)size );

        rc = xe_net_establish_connection( 1, AF_INET, XEN_HOST_ADDR, XEN_HOST_PORT, &sock );
        MYASSERT( 0 == rc );

        rc = xe_net_write_data( sock, TEST_STRING, strlen(TEST_STRING) );
        MYASSERT( 0 == rc );

        xe_net_close_connection( sock );
    }

ErrorExit:
    for ( i = 0; i < HANDLE_CT; i++ )
    {
        if ( fds[i] >= 0 )
        {
            close(fds[i] );
        }
    }
  return 0;
}
