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

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <string.h>

#include <sys/types.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/socketvar.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <arpa/inet.h>
//#include <sys/kernel.h>



#define ACCEPT_FILTER_MOD
#include <sys/socketvar.h>

#define MAX_PORT 0xffff

#define MSG_SIZE 15

int                 server_sockfd = -1;
int                 client_sockfd = -1;


void get_inaddrs( void )
{

    struct ifaddrs *ifaddr, *ifa;
    int family, s, n;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    /* Walk through linked list, maintaining head pointer so we
       can free list later */

    for (ifa = ifaddr, n = 0; ifa != NULL; ifa = ifa->ifa_next, n++) {
        if (ifa->ifa_addr == NULL)
            continue;

        family = ifa->ifa_addr->sa_family;

        /* Display interface name and family (including symbolic
           form of the latter for the common families) */

        if ( family == AF_INET && !strncmp( ifa->ifa_name, "xenif0", sizeof( "xenif0" )) )
        {
            printf("%-8s %s (%d)\n",
                   ifa->ifa_name,
                   (family == AF_INET) ? "AF_INET" : 0,
                   family);
            /* For an AF_INET* interface address, display the address */

            s = getnameinfo(ifa->ifa_addr,
                            (family == AF_INET) ? sizeof(struct sockaddr_in) :
                            sizeof(struct sockaddr_in6),
                            host, NI_MAXHOST,
                            NULL, 0, NI_NUMERICHOST);
            if (s != 0) {
                printf("getnameinfo() failed: %s\n", gai_strerror(s));
                exit(EXIT_FAILURE);
            }

            printf("\t\taddress: <%s>\n", host);

        } 
    }

    freeifaddrs(ifaddr);
}


int main(int argc , char *argv[])
{

    int                 client_addrlen = 0;
    struct sockaddr_in  server_sockaddr, client_sockaddr;
    int                 port = 0;
    char                client_message[MSG_SIZE];
    
//    struct accept_filter *myfilter;
//    accept_filter_init();
//    myfilter = accept_filt_get( );

    get_inaddrs();

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
    server_sockaddr.sin_port = htons( port );
    
    //Bind to socket
    if( bind(server_sockfd, (struct sockaddr *)&server_sockaddr, sizeof(server_sockaddr)) != 0 )
    {
        printf("bind failed\n");
        goto ErrorExit;
    }
    else
    {
        printf( "Bind succeeeded\n" );
    }


    //Listen
    if ( listen(server_sockfd, 3) == 0)
    {
        printf("waiting for connections on port %d\n", port);
    }
    else
    {
        printf("Listen failed\n");
        goto ErrorExit;
    }

    client_addrlen = sizeof(struct sockaddr_in);

    printf("Waiting for connection without accf_data filter enabled\n");

    client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_sockaddr, (socklen_t *) &client_addrlen);
    if( client_sockfd < 0 )
    {
        printf("Accept failed\n");
        goto ErrorExit;
    }
    else
    {
        printf("connection accepted\n");
    }

    int rc = 0;
    struct accept_filter_arg afa = {0};
    strcpy( afa.af_name, "dataready" );
    rc = setsockopt( server_sockfd, SOL_SOCKET, SO_ACCEPTFILTER, &afa, sizeof(afa) );
    if(rc)
    {
        perror("setsockopt");
    }
    
    printf("Waiting for connection with accf_data filter enabled\n" );

    client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_sockaddr, (socklen_t *) &client_addrlen);
    if( client_sockfd < 0 )
    {
        printf("Accept failed\n");
        goto ErrorExit;
    }
    else
    {
        printf("connection accepted\n");
    }


    
ErrorExit:
    printf("Error exit called\n");
    if ( server_sockfd > 0 ) close( server_sockfd );
    if ( client_sockfd > 0 ) close( client_sockfd );

    return 1;
}
