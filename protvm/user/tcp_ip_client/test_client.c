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
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MSG_LEN 50

int main(int argc , char *argv[])
{
    int                socket_desc;
    struct sockaddr_in server;
    int                err = 0;

    char               client_message[MSG_LEN];
    char              *hello = "This is a client message\n";

    int i = 0;

    memset( client_message, 0, MSG_LEN);
    strcpy(client_message, hello);

    // Get Time 1

    // 1> Call Socket
    socket_desc = socket(AF_INET , SOCK_STREAM , 0);
     
    if (socket_desc == -1)
    {
        printf("Could not create socket");
        err = 1;
        goto ErrorExit;

    } else {
        printf("Socket Number: %d\n", socket_desc);
    }


    // 2> Call connect

//    server.sin_addr.s_addr = inet_addr("10.0.2.15");
    server.sin_addr.s_addr = inet_addr("10.15.32.101");
    server.sin_family = AF_INET;
    server.sin_port = htons(21845);

    if (connect(socket_desc, (struct sockaddr *)&server , sizeof(server)) != 0)
    {
        printf("connect failed. Error\n");
        return 1;
    }

    printf("Connected\n");


    for( i = 0; i < 2; i++ )
    {
        // 3> Call write
        send(socket_desc, client_message , strlen(client_message) + 1, 0);
    }

    // 4> Call close
    close(socket_desc);

    // Get Time 2
    // Diff Times
     
ErrorExit:
    return err;
}
