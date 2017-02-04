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
#include <arpa/inet.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <pthread.h>
#include <unistd.h>

#include <stdbool.h>
#include <stdlib.h>

#include "message_types.h" // real program should not use this!!


#define THREAD_COUNT    15
#define OPEN_CLOSE_CNT  4 
#define MESSAGE_COUNT   5

// These are provided by the environment
static char              *server_ip   = NULL;
static int                server_port_num = 0;


int count = 0;
int get_counter()
{
    return __sync_fetch_and_add( &count, 1 );
}


static void *
thread_open_write_close( void * Ignored )
{
    int fd = 0;
    struct sockaddr_in server = {0};
    char               msg[ MESSAGE_TYPE_MAX_PAYLOAD_LEN ];


    server.sin_addr.s_addr = inet_addr( server_ip );
    server.sin_family      = AF_INET;
    server.sin_port        = htons( server_port_num );

    for ( int i = 0; i < OPEN_CLOSE_CNT; i++ )
    {
        printf( "Thread number %lx\n", pthread_self() );

        fd = socket( AF_INET, SOCK_STREAM, 0 );
        if ( fd < 0 )
        {
            printf( "socket failed: %d\n", errno );
            goto ErrorExit;
        }

        if ( connect( fd, (struct sockaddr *) &server, sizeof(server) ) )
        {
            printf( "socket failed: %d\n", errno );
            goto ErrorExit;
        }

        snprintf( msg, sizeof(msg),
                  "Hello from thread %lx message %d\n",
                  pthread_self(), i );

        if ( send( fd, msg, strlen(msg) + 1, 0 ) < 0 )
        {
            printf( "send failed: %d\n", errno );
            goto ErrorExit;
        }

        close( fd );
        fd = -1;
        
    } // for

ErrorExit:
    if ( fd >= 0 )
    {
        close( fd );
    }

    return NULL;
}


void *
thread_write( void * SockFd )
{
    int fd = *(int *)SockFd;
    char msg[ MESSAGE_TYPE_MAX_PAYLOAD_LEN ];

    //printf( "Hello from thread %lx PID %d\n",
    //pthread_self(), getpid() );
    
    for ( int i = 0; i < MESSAGE_COUNT; i++ )
    {
        snprintf( msg, sizeof(msg),
                  "Hello from thread %lx message %d #%x\n",
                  pthread_self(), i, get_counter() );

        if ( send( fd, msg, strlen(msg) + 1, 0 ) < 0 )
        {
            printf( "send failed: %d\n", errno );
            goto ErrorExit;
        }
    }
ErrorExit:
    return NULL;
}


void *
thread_write_echo( void * SockFd )
{
    int fd = *(int *)SockFd;
    char sendmsg[ MESSAGE_TYPE_MAX_PAYLOAD_LEN ];
    char recvmsg[ MESSAGE_TYPE_MAX_PAYLOAD_LEN ];

    //printf( "Hello from thread %lx PID %d\n",
    //pthread_self(), getpid() );
    
    for ( int i = 0; i < MESSAGE_COUNT; i++ )
    {
        snprintf( sendmsg, sizeof(sendmsg),
                  "Hello from thread %lx message %d #%x\n",
                  pthread_self(), i, get_counter() );

        //printf( "Sending message: %s\n", msg );

        if ( send( fd, sendmsg, strlen(sendmsg) + 1, 0 ) < 0 )
        {
            printf( "send failed: %d\n", errno );
            goto ErrorExit;
        }

        if ( recv( fd, recvmsg, sizeof(recvmsg), 0 ) < 0 )
        {
            printf( "recv failed: %d\n", errno );
            goto ErrorExit;
        }

        if ( 0 != strcmp( sendmsg, recvmsg ) )
        {
            printf( "Mismatch: %s AND %s\n", sendmsg, recvmsg );
            goto ErrorExit;
        }
    }
    
ErrorExit:
    return NULL;
}



int
run_writer_threads()
{
    int rc = 0;
    int fd = 0;
    struct sockaddr_in server = {0};
    pthread_t threads[ THREAD_COUNT ];
    int i = 0;

    server.sin_addr.s_addr = inet_addr( server_ip );
    server.sin_family      = AF_INET;
    server.sin_port        = htons( server_port_num );

    fd = socket( AF_INET, SOCK_STREAM, 0 );
    if ( fd < 0 )
    {
        printf( "socket failed: %d\n", errno );
        rc = errno;
        goto ErrorExit;
    }

    if ( connect( fd, (struct sockaddr *) &server, sizeof(server) ) )
    {
        printf( "socket failed: %d\n", errno );
        rc = errno;
        goto ErrorExit;
    }
    
    for( i = 0; i < THREAD_COUNT; ++i )
    {
        if ( pthread_create( &threads[i], NULL, thread_write, &fd ) )
//        if ( pthread_create( &threads[i], NULL, thread_write_echo, &fd ) )
        {
            exit( 1 );
        }
    }

    for( i = 0; i < THREAD_COUNT; ++i )
    {
        pthread_join( threads[ i ], NULL );
    }

ErrorExit:
    return rc;
}


void *
thread_func_1( void *data )
{
    int  socket_desc;
    int  cnt;
    
    for(cnt = 0;cnt < OPEN_CLOSE_CNT; ++cnt)
    {

        printf("Thread number %lx\n", pthread_self());
        // 1> Call Socket
        socket_desc = socket( AF_INET, SOCK_STREAM, 0 );
     
        if (socket_desc == -1)
        {
            printf("Could not create socket");
            goto ErrorExit;

        } else {
            printf("Socket Number: %d\n", socket_desc);
        }
    
        printf("Thread number %ld\n", pthread_self());
        // 2> Call close
        close(socket_desc);
    }

ErrorExit:
    return NULL;
}


void *
thread_func_2( void *data )
{
    int  socket_desc;
    int  cnt;

    for(cnt = 0;cnt < OPEN_CLOSE_CNT; ++cnt)
    {
        printf("Thread number %ld\n", pthread_self());
        // 1> Call Socket
        socket_desc = socket( AF_INET, SOCK_STREAM, 0 );
     
        if (socket_desc == -1)
        {
            printf("Could not create socket");
            goto ErrorExit;

        } else {
            printf("Socket Number: %d\n", socket_desc);
        }
    
        printf("Thread number %ld\n", pthread_self());
        // 2> Call close
        close(socket_desc);
    }

ErrorExit:
    return NULL;

}


int
run_open_write_close_threads()
{
    pthread_t threads[ THREAD_COUNT ];
    int i = 0;
    
    for( i = 0; i < THREAD_COUNT; ++i )
    {
        if ( pthread_create( &threads[i], NULL, thread_open_write_close, NULL ) )
        {
            exit( 1 );
        }
    }

    for( i = 0; i < THREAD_COUNT; ++i )
    {
        pthread_join( threads[ i ], NULL );
    }

    return 0;
}


int main(int argc , char *argv[])
{
    int err = 0;
    char * server_port = NULL;

    // Get the remote IP and port from the commmand line if provided;
    // otherwise get from the environment
    if ( argc == 3 )
    {
        server_ip = argv[1];
        server_port_num = atoi( argv[2] );
    }
    else
    {
        server_ip = getenv( "SERVER_IP" );
        if ( !server_ip )
        {
            printf( "SERVER_IP env var must be set to the server's IP\n" );
            err = 1;
            goto ErrorExit;
        }
        server_port = getenv( "SERVER_PORT" );
        if ( !server_port )
        {
            printf( "SERVER_PORT env var must be set to the server's port\n" );
            err = 1;
            goto ErrorExit;
        }
        server_port_num = atoi( server_port );
    }
    printf( "Server is %s:%d\n", server_ip, server_port_num );
        
    run_writer_threads();
    // run_open_write_close_threads();

ErrorExit:
    return err;
}
