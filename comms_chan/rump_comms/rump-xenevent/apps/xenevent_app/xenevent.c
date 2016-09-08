
#include <stdio.h> 
#include <stdlib.h>

#define XENEVENT_DEVICE  "/dev/xenevent"

int main(void)
{
    FILE * fp = NULL;
    size_t size = 0;
    uint32_t data = 55;
    
    fp = fopen( XENEVENT_DEVICE, "w+b" );
    if ( NULL == fp )
    {
        printf( "Failed to open file " XENEVENT_DEVICE "\n" );
        goto ErrorExit;
    }
    printf( "Successfully opened device %s\n", XENEVENT_DEVICE );

    printf( "Writing %d bytes to device\n", sizeof(data) );    
    size = fwrite( &data, sizeof(data), 1, fp );

    printf( "Wrote %d bytes\n", size );
    
ErrorExit:
    if ( NULL != fp )
    {
        fclose( fp );
    }

  return 0;
}
