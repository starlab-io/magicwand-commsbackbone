#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define NUM_SOCKS 60

#define BUFF_SIZE 32
#define START_PORT 2000

typedef struct thread_info_t {
    pthread_t thread_id;
    int t_num;
    int port;
} thread_info;


static void *
thread_func( void *arg )
{
    struct sockaddr_in serv_addr, conn_addr = {0};
    int listen_fd, conn_fd = -1;
    socklen_t addrlen;
    char buff[ BUFF_SIZE ] = {0};
    int err = 0;
    thread_info *t_info = ( thread_info *) arg;

    listen_fd = socket( AF_INET, SOCK_STREAM, 0 );
    if( listen_fd < 0 )
    {
        printf("Socket Error: Thread: %d\n", t_info->t_num );
        goto ErrorExit;
    }

    printf( "Created socket for thread %d\n", t_info->t_num);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl( INADDR_ANY );
    serv_addr.sin_port = htons( t_info->port );

    err = bind( listen_fd, ( struct sockaddr* ) &serv_addr, sizeof( serv_addr ) );
    if( err )
    {
        perror("BIND ERROR");
        printf("Errno: %d\n", errno );
        printf( "thread: %d, local_fd: %d\n", t_info->t_num, listen_fd ); 
        goto ErrorExit;
    }
    printf( "Bind succeded for thread %d\n", t_info->t_num );

    err = listen( listen_fd, 3 );
    if( err )
    {
        perror("LISTEN ERROR");
        goto ErrorExit;
    }
    conn_fd = accept( listen_fd, ( struct sockaddr* ) &conn_addr, &addrlen );
    if( conn_fd < 0 )
    {
        perror("ACCEPT ERROR"); 
        goto ErrorExit;
    }

    printf( "Accepted connection from %s \n", inet_ntoa( conn_addr.sin_addr ) );

    while( recv( conn_fd, buff, sizeof( buff ), 0) > 0 )
    {
        printf( "Thread %d Recieved: %s", t_info->t_num, buff);
        memset( ( void* ) &buff, 0, sizeof( buff ) );
    }

    printf("Thread %d Exiting\n", t_info->t_num );

ErrorExit:
    if( listen_fd > 0 )
    {
        close( listen_fd );
    }
    if( conn_fd > 0 )
    {
        close( conn_fd );
    }

    return NULL;
}



int
main()
{
    printf("Listening on multiple ports to test multi-ins functinality\n\n");

    thread_info t_info [ NUM_SOCKS ] = {0};

    for( int i = 0; i < NUM_SOCKS; i++ )
    {
        t_info[i].t_num = i;
        t_info[i].port = i + START_PORT;
        pthread_create( &t_info[i].thread_id, NULL,
                        &thread_func, (void *) &t_info[i] );
        sleep(1);
    }

    for( int i = 0; i < NUM_SOCKS; i++ )
    {
        pthread_join( t_info[i].thread_id, NULL );
    }

    printf("Program finished\n");
}


