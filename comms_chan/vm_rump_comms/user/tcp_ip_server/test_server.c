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
#include <stdio.h>

int main(int argc , char *argv[])
{
    int socket_desc;
    int err = 0;

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

    // 2> Call close
    //close(socket_desc);
     
ErrorExit:
    return err;
}
