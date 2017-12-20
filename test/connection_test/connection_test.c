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
#include <poll.h>
#include <stdbool.h>

#include <mw_timer.h>

//For Apache over INS
#define IP_STR_LEN 16

char remote_ip[ IP_STR_LEN ] = {0};

#define REMOTE_PORT 80

int num_conn = 0;
int poll_count = 0;

void
connect_func( struct pollfd *PollFd )
{
    struct sockaddr_in conn_addr = {0};
    int err = 0;
    
    conn_addr.sin_family = AF_INET;
    conn_addr.sin_addr.s_addr = inet_addr( remote_ip );
    conn_addr.sin_port = htons( REMOTE_PORT );

    err = connect( PollFd->fd, ( struct sockaddr* ) &conn_addr, sizeof( conn_addr ) );
    if( err ) 
    {
        PollFd->fd = -1;
        perror("CONNECT ERROR");
        printf("Errno: %d\n", errno );
        goto ErrorExit;
    }

    printf("connect succeeded for fd: %d\n", PollFd->fd );

ErrorExit:
    return;
}

int
do_read( struct pollfd *Fds )
{
    int rc = 0;
    char buf[4096] = {0};
    char s_buf = 0;

    for( int i = 0; i < num_conn; i++ )
    {
        if( Fds[i].fd == -1 ) { continue; }

        rc = send( Fds[i].fd, (void *) &s_buf, sizeof(s_buf), MSG_NOSIGNAL );
        if( ( rc < 0 ) )
        {
            perror("send");
        }
        
        
        rc = recv( Fds[i].fd, (void*) &buf, sizeof(buf) , MSG_DONTWAIT );
        if( ( rc < 0 ) )
        {
            perror("recv");
        }

        //Conection is closed on remote side
        if( rc == 0 )
        {
            printf("Socket %d closed\n", Fds[i].fd);
        }
    }

    return 0;

}
            

int
do_poll( struct pollfd *Fds )
{
    int rc = 0;
    
    for( int i = 0; i < num_conn; i++ )
    {
        Fds[i].revents = 0;
    }
    
    rc = poll( Fds, num_conn, 0 );
    if( rc < 0 )
    {
        perror("poll");
    }

    bool one_left = false;
    
    for( int i = 0; i < num_conn; i++ )
    {   
        if( ( Fds[i].fd == -1 ) ) { continue; }

        one_left = true;
        
        if( ( Fds[i].revents == 0 ) ) { continue; }
        
        printf( "fds[%d]\tfd:\t%d\tevents:\t0x%x\trevents:\t0x%x\n",
                i,
                Fds[i].fd,
                Fds[i].events,
                Fds[i].revents);

        if( ( Fds[i].revents & POLLHUP ) )
        {
            Fds[i].fd = -1;
            Fds[i].revents = 0;
        }
        
    }
    
    return one_left;
}


int
main( int argc, const char* argv[] )
{
    
    struct pollfd *fds = NULL;
    
    if( NULL == argv[1] )
    {
        printf("Please input number of connections\n");
        return -1;
    }
    
    if( NULL == argv[2] )
    {
        printf("Please input ip address\n");
        return -1;
    }
    
    if( NULL == argv[3] )
    {
        printf("Please input the number of seconds you would like to poll");
    }

    strncpy( remote_ip, argv[2], IP_STR_LEN );
    
    num_conn = strtol( argv[1], NULL, 10 );
    poll_count = strtol( argv[3], NULL, 10 );
    
    
    printf("Running %d connections\n", num_conn );
    printf("Running %d poll iterations\n", poll_count );


    fds = (struct pollfd*) malloc( num_conn * sizeof( struct pollfd ) );
        
    //This loop will create all the sockets and init the
    //fds array :) two birds one stone
    for( int i = 0; i < num_conn; i++ )
    {
        //We are only polling
        fds[i].events = POLLIN | POLLHUP | POLLERR | POLLNVAL;
        
        fds[i].fd = socket( AF_INET, SOCK_STREAM, 0 );
        if ( fds[i].fd < 0 )
        {
            fds[i].fd = -1;
            perror("SocketError");   
        }

#ifdef KEEPALIVE
        int optval = 1;
        socklen_t optlen = sizeof( optval );
        if( setsockopt( fds[i].fd, SOL_SOCKET, SO_KEEPALIVE, &optval, optlen ) < 0 )
        {
            perror("setsockopt()");
        }
        
        optval = 0;
        optlen = sizeof( optval );
        if( getsockopt( fds[i].fd, SOL_SOCKET, SO_KEEPALIVE, &optval, &optlen ) < 0 )
        {
            perror("getsockopt()");
        }
#endif
        
    }
    

    for( int i = 0; i < num_conn; i++ )
    {
        connect_func( &fds[i] );
    }

    for( int i = 0; i < poll_count; i++ )
    {
        printf( "\nPoll iteration %d\n", i );

#ifndef KEEPALIVE
        do_read( fds );
#endif
        if( do_poll( fds ) == false )
        {
            printf( "All connections closed\n" );
            break;
        }
        
        sleep(1);
    }

    for( int i = 0; i < num_conn; i++ )
    {
        close( fds[i].fd );
    }
    
    free(fds);

    printf("Program finished\n");

    return 0;
}


