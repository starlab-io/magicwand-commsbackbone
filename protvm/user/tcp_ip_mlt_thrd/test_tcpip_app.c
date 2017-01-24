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

#include <pthread.h>
#include <unistd.h>

#include <stdbool.h>
#include <stdlib.h>

#define OPEN_CLOSE_CNT  4 

void *
thread_func_1( void *data )
{
    int  socket_desc;
    int  err = 0;
    int  cnt;

    for(cnt = 0;cnt < OPEN_CLOSE_CNT; ++cnt)
    {

        printf("Thread number %ld\n", pthread_self());
        // 1> Call Socket
        socket_desc = socket( AF_INET, SOCK_STREAM, 0 );
     
        if (socket_desc == -1)
        {
            printf("Could not create socket");
            err = 1;
            goto ErrorExit;

        } else {
            printf("Socket Number: %d\n", socket_desc);
        }
    
        printf("Thread number %ld\n", pthread_self());
        // 2> Call close
        close(socket_desc);
    }

ErrorExit:
    return;
}

void *
thread_func_2( void *data )
{
    int  socket_desc;
    int  err = 0;
    int  cnt;


    for(cnt = 0;cnt < OPEN_CLOSE_CNT; ++cnt)
    {

        printf("Thread number %ld\n", pthread_self());
        // 1> Call Socket
        socket_desc = socket( AF_INET, SOCK_STREAM, 0 );
     
        if (socket_desc == -1)
        {
            printf("Could not create socket");
            err = 1;
            goto ErrorExit;

        } else {
            printf("Socket Number: %d\n", socket_desc);
        }
    
        printf("Thread number %ld\n", pthread_self());
        // 2> Call close
        close(socket_desc);
    }

ErrorExit:
    return;

}

int main(int argc , char *argv[])
{
    int                 err = 0;
    pthread_t           thrd_1;
    pthread_t           thrd_2;


    if ( pthread_create( &thrd_1, NULL, thread_func_1, NULL )  != 0 )
    {
        exit(1);
    }

    if ( pthread_create( &thrd_2, NULL, thread_func_2, NULL )  != 0 )
    {
        exit(1);
    }

    pthread_join( thrd_1, NULL );
    pthread_join( thrd_2, NULL );
     
    return err;
}
