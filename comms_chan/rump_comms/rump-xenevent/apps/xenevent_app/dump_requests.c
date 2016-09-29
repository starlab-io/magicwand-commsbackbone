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
    mt_request_generic_t request;
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
        size_t bytect = fread( &request, sizeof( request ), 1, fp );
        if ( bytect <= 0 ) break;

        char * stype = "**** <unset> ****";
        mt_request_id_t rtype = request.base.type;

        switch( rtype )
        {
        case MtRequestSocketCreate:
            stype = "create";
            break;
        case MtRequestSocketConnect:
            stype = "connect";
            break;
        case MtRequestSocketClose:
            stype = "close";
            break;
        case MtRequestSocketRead:
            stype = "read";
            break;
        case MtRequestSocketWrite:
            stype = "write";
            break;
        default:
            break;
        }
        printf( "Record %d: %s(%d)\tid 0x%03x size 0x%04x sock %d\n",
                ct++,
                stype,
                (int)rtype,
                (int)request.base.id,
                request.base.size,
                request.base.sockfd );
    }
ErrorExit:
    return;
}
