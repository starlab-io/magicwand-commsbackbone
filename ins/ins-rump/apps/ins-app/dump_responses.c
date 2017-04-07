#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>


#include <stdint.h>
#include "message_types.h"
#include "app_common.h"

void
main( int argc, char * argv[] )
{
    FILE * fp = NULL;
    mt_response_generic_t response;
    size_t bytect = 0;
    int ct = 0;
    
    if ( 2 != argc )
    {
        printf( "Usage: %s <input traffic file>\n", argv[0] );
        goto ErrorExit;
    }

    fp = fopen( argv[1], "rb" );
    MYASSERT( fp );

    while( true )
    {
        size_t bytect = fread( &response, sizeof( response ), 1, fp );
        if ( bytect <= 0 ) break;

        char * stype = "**** <unset> ****";
        mt_response_id_t rtype = response.base.type;

        switch( rtype )
        {
        case MtResponseSocketCreate:
            stype = "create";
            break;
        case MtResponseSocketConnect:
            stype = "connect";
            break;
        case MtResponseSocketClose:
            stype = "close";
            break;
        case MtResponseSocketRead:
            stype = "read";
            break;
        case MtResponseSocketWrite:
            stype = "write";
            break;
        default:
            break;
        }
        printf( "Record %d: %s(%x)\tid 0x%03x size 0x%04x sock %d status %x\n",
                ct++,
                stype,
                (int)rtype,
                (int)response.base.id,
                response.base.size,
                response.base.sockfd,
                response.base.status );
    }
ErrorExit:
    return;
}
