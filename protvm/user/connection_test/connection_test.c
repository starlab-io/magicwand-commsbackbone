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
#define REMOTE_IP "10.30.30.134"

//For plain apache on PVM
//define REMOTE_IP "10.30.30.12"

#define REMOTE_PORT 80

int num_conn = 0;


void
connect_func( int conn_fd )
{
    struct sockaddr_in conn_addr = {0};
    int err = 0;

    MW_TIMER_INIT();
    
    conn_addr.sin_family = AF_INET;
    conn_addr.sin_addr.s_addr = inet_addr( REMOTE_IP );
    conn_addr.sin_port = htons( REMOTE_PORT );

    MW_START_TIMER();
    err = connect( conn_fd, ( struct sockaddr* ) &conn_addr, sizeof( conn_addr ) );
    MW_END_TIMER( "connect" );
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


