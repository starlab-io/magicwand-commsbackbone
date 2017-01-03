/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

/**
 * @file    test_server.c
 * @author  Mark Mason 
 * @date    10 September 2016
 * @version 0.1
 * @brief   A simple test server to test comms.
 */

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MSG_SIZE 1500

int                 server_sockfd = -1;
int                 client_sockfd = -1;


int main(int argc , char *argv[])
{

    int                 client_addrlen = 0;
    int                 read_size = 0;
    struct sockaddr_in  server_sockaddr, client_sockaddr;

    char                client_message[MSG_SIZE];
    char                *hello = "Hello\n";


    memset( client_message, 0, 20);
    bzero( &server_sockaddr, sizeof(server_sockaddr) );
    bzero( &client_sockaddr, sizeof(client_sockaddr) );
    strcpy(client_message, hello);
    

    //Create Socket
    server_sockfd = socket(AF_INET , SOCK_STREAM , 0);
     
    if (server_sockfd == -1)
    {
        printf("Could not create socket\n");
        goto ErrorExit;

    } else {
        printf("Socket Number: %d\n", server_sockfd);
    }


    //Prepare sockaddr_in struct with needed values
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = INADDR_ANY;
    server_sockaddr.sin_port = htons(21845);

    
    //Bind to socket
    if( bind(server_sockfd, (struct sockaddr *)&server_sockaddr, sizeof(server_sockaddr)) != 0 )
    {
        printf("bind failed\n");
        goto ErrorExit;
    }else
    {
        printf("Bind succeeeded\n");
    }


    //Listen
    if ( listen(server_sockfd, 3) == 0)
    {
        printf("waiting for connections\n");
    }else
    {
        printf("Listen failed\n");
        goto ErrorExit;
    }

    client_addrlen = sizeof(struct sockaddr_in);
    
    //Ok, so the cast is to a general sockaddr type
    //HOWEVER length passed is the size of sockaddr_in... strange
    //I figured it out, the sockaddr is cast as a struct_sockaddr on the other side based on the
    //sin_family value
    client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_sockaddr, (socklen_t *) &client_addrlen);


    if( client_sockfd != 0 )
    {
        printf("ACCEPT SUCCESS\n");
    }

    goto UnderConstruction;
    
    if( client_sockfd < 0 )
    {
        printf("Accept failed\n");
        goto ErrorExit;
    }
    else
    {
        printf("connection accepted\n");
    }

    int i = 0;

    //Recieve a message from the client
    while( (read_size = recv( client_sockfd, client_message, MSG_SIZE, 0 ) > 0 ) )
    {
        printf("Received data: %d\n", i);
        printf("Received message: %s\n\n", client_message);

        //Send message back to client
//        write( client_sockfd, client_message, strlen(client_message));
    
        //Clear messge buffer
        memset( &client_message, 0, sizeof(client_message) );

//        printf("Sent message back to client\n");
        i++;
    }

    if( read_size == 0 )
    {
        printf("Client disconnected\n");
    }
    else if( read_size == -1 )
    {
        printf("recieve failed!!!\n");
    }

    close(server_sockfd);
    close(client_sockfd);
    
UnderConstruction:

    printf("Exiting Program early, Closing sockets\n");

    if(server_sockfd != -1)
        close(server_sockfd);

    if(client_sockfd != -1 )
        close(client_sockfd);

    return 0;

     
ErrorExit:
    printf("Error exit called\n");
    close(server_sockfd);
    close(client_sockfd);
    return 1;
}
