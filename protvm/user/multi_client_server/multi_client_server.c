/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

/**
 * @file    test_server.c
 * @author  Matt Leinhos
 * @date    Oct 2017
 * @version 0.1
 * @brief   A simple test server to test comms. Keeps accepting connections on one port.
 */

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#define MAX_PORT 0xffff

#define MSG_SIZE 15

int                 server_sockfd = -1;
int                 client_sockfd = -1;


int main(int argc , char *argv[])
{

    int                 client_addrlen = 0;
    int                 read_size = 0;
    struct sockaddr_in  server_sockaddr, client_sockaddr;
    int                 port = 0;
    char                client_message[MSG_SIZE];

    if ( argc != 2 )
    {
        printf( "Usage: %s <port number>\n", argv[0] );
        goto ErrorExit;
    }

    port = atoi( argv[1] );
    if ( port < 0 || port > MAX_PORT )
    {
        printf( "Invalid port number given: %d\n", port );
        goto ErrorExit;
    }
    
    bzero( client_message, sizeof(client_message) );
    bzero( &server_sockaddr, sizeof(server_sockaddr) );
    bzero( &client_sockaddr, sizeof(client_sockaddr) );

    // Create Socket
    server_sockfd = socket(AF_INET , SOCK_STREAM , 0);
    if (server_sockfd == -1)
    {
        printf("Could not create socket\n");
        goto ErrorExit;

    } 

    printf("Socket Number: %d\n", server_sockfd);

    // Prepare sockaddr_in struct with needed values
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = INADDR_ANY;
    server_sockaddr.sin_port = htons( port );
    
    // Bind to socket
    if( bind(server_sockfd, (struct sockaddr *)&server_sockaddr, sizeof(server_sockaddr)) != 0 )
    {
        printf("bind failed\n");
        goto ErrorExit;
    }

    printf("Bind succeeeded\n");

    // Listen
    if ( listen(server_sockfd, 3) != 0)
    {
        printf("Listen failed\n");
        goto ErrorExit;
    }
    printf("waiting for connections on port %d\n", port);
    
    client_addrlen = sizeof(struct sockaddr_in);
    
    // Ok, so the cast is to a general sockaddr type
    // HOWEVER length passed is the size of sockaddr_in... strange

    // I figured it out, the sockaddr is cast as a struct_sockaddr on
    // the other side based on the sin_family value

    while( 1 )
    {
        client_sockfd = accept(server_sockfd,
                               (struct sockaddr *)&client_sockaddr,
                               (socklen_t *) &client_addrlen);
        if( client_sockfd < 0 )
        {
            printf("Accept failed\n");
            goto ErrorExit;
        }

        printf("connection accepted\n");

        int i = 0;
        ssize_t recv_size = 0;

        // Recieve a message from the client
        while ( ( recv_size = recv( client_sockfd, client_message, MSG_SIZE, 0 ) ) > 0 )
        {
            printf("Received data: %d\n", i);
            printf("Received message: %s\n\n", client_message);

            // Send message back to client
            send(client_sockfd, client_message , recv_size, 0);

            // Clear messge buffer
            memset( &client_message, 0, sizeof(client_message) );

            printf("Sent message back to client\n");
            i++;
        }

        // Done with conversation
        if( read_size == 0 )
        {
            printf("Client disconnected\n");
        }
        else if( read_size == -1 )
        {
            printf("recieve failed!!!\n");
        }

        close( client_sockfd );
    } // while( 1 )

ErrorExit:
    printf("Error exit called\n");
    if ( server_sockfd > 0 )
    {
        close( server_sockfd );
    }

    return 1;
}
