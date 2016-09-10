
#include <stdio.h> 
#include <stdlib.h>
#include "app_common.h"

#define XENEVENT_DEVICE  "/dev/xe"

int main(void)
{
    FILE * fp = NULL;
    size_t size = 0;
    const char data[ 10 ] = "abcdefghi";
//    uint32_t data = ;
    
    fp = fopen( XENEVENT_DEVICE, "w+b" );
    if ( NULL == fp )
    {
        printf( "Failed to open file " XENEVENT_DEVICE "\n" );
        goto ErrorExit;
    }
    printf( "Successfully opened device %s\n", XENEVENT_DEVICE );
    DEBUG_BREAK();
    
    printf( "Writing %d bytes to device\n", (int)sizeof(data) );    
    size = fwrite( data, sizeof(data), 1, fp );

    printf( "Wrote %d bytes\n", (int)size );
    
ErrorExit:
    if ( NULL != fp )
    {
        fclose( fp );
    }

  return 0;
}
