#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include <mw_timer.h>

//For Apache over INS
#define IP_STR_LEN 16

char remote_ip[ IP_STR_LEN ] = {0};

#define REMOTE_PORT 80

int num_conn = 0;


void
connect_func( int conn_fd )
{
    struct sockaddr_in conn_addr = {0};
    int err = 0;

    MW_TIMER_INIT();
    
    conn_addr.sin_family = AF_INET;
    conn_addr.sin_addr.s_addr = inet_addr( remote_ip );
    conn_addr.sin_port = htons( REMOTE_PORT );

    MW_TIMER_START();
    err = connect( conn_fd, ( struct sockaddr* ) &conn_addr, sizeof( conn_addr ) );
    MW_TIMER_END( "connect fd: %d ", conn_fd  );
    if( err ) 
    { 
        perror("CONNECT ERROR");
        printf("Errno: %d\n", errno );
        goto ErrorExit;
    }

ErrorExit:
    return;
}



int
main( int argc, const char* argv[] )
{
    
    int *fds = NULL;
    
    if( NULL == argv[1] )
    {
        printf(" Please input number of connections\n ");
        return -1;
    }

    if( NULL == argv[2] )
    {
        printf("Please input ip address\n");
        return -1;
    }

    strncpy( remote_ip, argv[2], IP_STR_LEN );

    printf("Running %s connections\n", argv[1] );

    num_conn = strtol( argv[1], NULL, 10 );

    fds = (int*) malloc( num_conn * sizeof(int) );
    
    for( int i = 0; i < num_conn; i++ )
    {
        fds[i] = socket( AF_INET, SOCK_STREAM, 0 );
        if ( !fds[i] )
        {
            perror("SocketError");   
        }
    }
    

    for( int i = 0; i < num_conn; i++ )
    {
        printf("connect: %d ", i);
        connect_func( fds[i] );
    }


    printf("Continue?\n");
    getchar();

    for( int i = 0; i < num_conn; i++ )
    {
        shutdown( fds[i], SHUT_RDWR );
        close( fds[i] );
    }
    
    free(fds);

    printf("Program finished\n");
}


