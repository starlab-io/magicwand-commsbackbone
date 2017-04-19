#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <stdint.h>
#include "message_types.h"
#include "app_common.h"

#include "config.h"

#define REMOTE_DEST_IP "10.190.2.104"
#define REMOTE_DEST_PORT 5555

#define OUTPUT_TRAFFIC_FILE "incoming_requests.bin"

// Hard-coded sockets
typedef struct _sinfo {
    int sockfd;
    char * desthost;
    int    destport;
} sinfo_t;


#define BASE_SOCKFD 5 // first socket
// Note hard-coded socket values
sinfo_t sinfo[2] =
{
    { .sockfd = 0,
      .desthost = REMOTE_DEST_IP,
      .destport = 5555 },
    { .sockfd = 0,
      .desthost = REMOTE_DEST_IP,
      .destport = 6666 }
};

#define SOCKET_CT 2
//static int sockets[] = {5, 6};

#define  WRITE_REQUESTS 6

static int request_id = 0;

void
build_create_socket( mt_request_generic_t * Request )
{
    mt_request_socket_create_t * create = &(Request->socket_create);
    
    bzero( Request, sizeof(*Request) );

    create->base.sig      = MT_SIGNATURE_REQUEST;
    
    create->base.type     = MtRequestSocketCreate;
    create->base.id       = request_id++;
    create->base.sockfd   = 0;

    create->sock_fam      = MT_PF_INET;
    create->sock_type     = MT_ST_STREAM;
    create->sock_protocol = 0;
}

void
build_connect_socket( mt_request_generic_t * Request,
                      sinfo_t * SockInfo )
{
    mt_request_socket_connect_t * connect = &(Request->socket_connect);
    
    bzero( Request, sizeof(*Request) );

    connect->base.sig  = MT_SIGNATURE_REQUEST;
    
    connect->base.type = MtRequestSocketConnect;
    connect->base.id = request_id++;

    connect->base.sockfd = SockInfo->sockfd;
    
    connect->port = SockInfo->destport;
    strcpy( (char *) connect->hostname, SockInfo->desthost );
    connect->base.size = sizeof( connect->port ) + strlen( REMOTE_DEST_IP ) + 1;
}

void
build_write_socket( mt_request_generic_t * Request, sinfo_t * SockInfo )
{
    mt_request_socket_write_t * wsock = &(Request->socket_write);

    static int msg_num = 1;
    bzero( Request, sizeof(*Request) );

    wsock ->base.sig  = MT_SIGNATURE_REQUEST;
    wsock->base.type = MtRequestSocketWrite;
    wsock->base.id = request_id++;

    wsock->base.sockfd = SockInfo->sockfd;

    snprintf( (char *) wsock->bytes, sizeof(wsock->bytes),
              "Hello from build traffic file: message %d sock %d\n",
              msg_num++, SockInfo->sockfd );

    wsock->base.size = strlen( wsock->bytes ) + 1;
}

void
build_close_socket( mt_request_generic_t * Request, sinfo_t * SockInfo )
{
    mt_request_socket_close_t * csock = &(Request->socket_close);
    
    bzero( Request, sizeof(*Request) );

    csock ->base.sig  = MT_SIGNATURE_REQUEST;
    csock->base.type = MtRequestSocketClose;
    csock->base.id = request_id++;

    csock->base.sockfd = SockInfo->sockfd;
}


void
complex( void )
{
    FILE * fp = NULL;
    mt_request_generic_t request;
    int sockfd = BASE_SOCKFD;
    
    fp = fopen( OUTPUT_TRAFFIC_FILE, "w+b" );
    MYASSERT( fp );

    for ( int k = 0; k < 10; k++ )
    {
        for ( int i = 0; i < NUMBER_OF(sinfo); i++ )
        {
            sinfo[i].sockfd = sockfd + i;
            build_create_socket( &request );
            fwrite( &request, sizeof(request), 1, fp );

            build_connect_socket( &request,  &sinfo[i] );
            fwrite( &request, sizeof(request), 1, fp );
        }

        //
        // Write to all the sockets
        //
        for ( int i = 0; i < NUMBER_OF(sinfo); i++ )
        {
            for( int j = 0; j < WRITE_REQUESTS; j++ )
            {
                build_write_socket( &request, &sinfo[i] );    
                fwrite( &request, sizeof(request), 1, fp );
            }

            build_close_socket( &request, &sinfo[i] );
            fwrite( &request, sizeof(request), 1, fp );
        }
    }
    
    fclose( fp );
}


void
lots_o_sockets( void )
{
    FILE * fp = NULL;
    mt_request_generic_t request;
    int sockfd = BASE_SOCKFD;
    
    fp = fopen( OUTPUT_TRAFFIC_FILE, "w+b" );
    MYASSERT( fp );

    // Open lots of sockets - 1 per thread - don't connect
    for ( int i = 0; i < MAX_THREAD_COUNT+1; i++ )
    {
        //sinfo[0].sockfd = BASE_SOCKFD + i;        
        build_create_socket( &request );
        fwrite( &request, sizeof(request), 1, fp );
    }

    // Close them
    for ( int i = 0; i < MAX_THREAD_COUNT+1; i++ )
    {
        sinfo[0].sockfd = BASE_SOCKFD + i;
        build_close_socket( &request, &sinfo[0] );
        fwrite( &request, sizeof(request), 1, fp );
    }
    
    fclose( fp );
}

void
main( void )
{
    lots_o_sockets();
//    complex();
}
