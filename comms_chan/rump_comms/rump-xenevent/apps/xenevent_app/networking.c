#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <errno.h>


#include "app_common.h"
#include "networking.h"


// TODO: manage the open connections

// Does everyone need to know this?
typedef struct _connection
{
    const char * RemoteHost;
    port_t       RemotePort;
    socket_t     Socket;
} connection_t;


int
xe_net_establish_connection( IN connection_id_t Id,
                             IN net_family_t AfFamily,
                             IN const char * HostName,
                             IN port_t       PortNum,
                             OUT socket_t * Socket )
{
    int sockfd, portno, n;
    int rc = 0;
    char portBuf[6] = {0}; // Max: 65536\0

    struct addrinfo serverHints = {0};
    struct addrinfo * serverInfo = NULL;
    struct addrinfo * serverIter = NULL;

    serverHints.ai_socktype  = SOCK_STREAM;     // TCP
    serverHints.ai_family    = AfFamily;

    if ( snprintf( portBuf, sizeof(portBuf), "%d", PortNum ) <= 0 )
    {
        MYASSERT( !"snprintf failed to extract port number" );
        rc = EINVAL;
        goto ErrorExit;
    }

    rc = getaddrinfo( HostName, portBuf, &serverHints, &serverInfo );
    if ( 0 != rc )
    {
        DEBUG_PRINT( "getaddrinfo failed: %s\n", gai_strerror(rc) );
        MYASSERT( !"getaddrinfo" );
        rc = EADDRNOTAVAIL;
        goto ErrorExit;
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        MYASSERT( !"Failed to create socket" );
        rc = ENOMEM;
        goto ErrorExit;
    }
    
    // Loop through all the results and connect to the first we can
    for (serverIter = serverInfo; serverIter != NULL; serverIter = serverIter->ai_next)
    {
        if ( serverIter->ai_family != AF_INET ) continue;

        rc = connect(sockfd, serverIter->ai_addr, serverIter->ai_addrlen);
        if ( rc < 0 )
        {
            // Silently continue
            continue;
        }

        // If we get here, we must have connected successfully
        break; 
    }
    
    if (serverIter == NULL)
    {
        // Looped off the end of the list with no connection.
        DEBUG_PRINT( "Couldn't connect() to any known address for %s\n", HostName );
        DEBUG_BREAK();
        rc = EADDRNOTAVAIL;
        goto ErrorExit;
    }

ErrorExit:
    if ( 0 != rc )
    {
        if ( sockfd > 0 )
        {
            close( sockfd );
        }
    }
    else
    {
        *Socket = sockfd;
    }
    return rc;
}



//
// xe_net_read_data
//
// Reads the data onto the socket. Blocks until all the data is
// read or an error occurs.
//
int
xe_net_read_data( IN socket_t Socket,
                  IN uint8_t * Buffer,
                  IN size_t    BufferLen )
{
    size_t byteCt = 0;
    size_t totRead = 0;

    int rc = 0;

    MYASSERT( BufferLen > 0 );
    
    while ( totRead < BufferLen )
    {
        byteCt = read( Socket, &Buffer[totRead], BufferLen - totRead );
        if ( byteCt < 0 )
        {
            MYASSERT( !"read" );
            rc = ENETDOWN;
            goto ErrorExit;
        }
        totRead += byteCt;
    }
    
ErrorExit:
    return rc;
}

//
// xe_net_write_data
//
// Writes the data onto the socket. Blocks until all the data is
// written or an error occurs.
//
int
xe_net_write_data( IN socket_t Socket,
                   IN uint8_t * Buffer,
                   IN size_t    BufferLen )
{
    size_t written = 0;
    size_t totWritten = 0;

    int rc = 0;

    MYASSERT( BufferLen > 0 );
    
    while ( totWritten < BufferLen )
    {
        written = write( Socket, &Buffer[totWritten], BufferLen - totWritten );
        if ( written <= 0 )
        {
            // TODO: 0 doesn't always indicate error. Figure it out.
            MYASSERT( !"write" );
            rc = ENETDOWN;
            goto ErrorExit;
        }
        totWritten += written;
    }
    
ErrorExit:
    return rc;
}

void
xe_net_close_connection( IN socket_t Socket )
{
    if ( Socket > 0 )
    {
        close( Socket );
    }
}


