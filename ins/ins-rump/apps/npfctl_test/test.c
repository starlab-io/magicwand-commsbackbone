#include <stdlib.h>
#include <unistd.h>


#include "npfctl.h"

int main(int argc, char **argv)
{

#if 0
    //initialize my_memcount
    
    char * mychar = malloc( 2 * sizeof( char ) );
    char * mychar2 = emalloc( 2 * sizeof( char ) );
    char * mychar3 = calloc( 2 * sizeof( char ), 0 );
    char * mychar4 = ecalloc( 2 * sizeof( char), 0 );

    printf( "malloc called 4 times, memcount: %d\n", memcount() );
    fflush(stdout);
    sleep(1);

    free( mychar );
    free( mychar2 );
    free( mychar3 );
    free( mychar4 );

    printf( "all mem freed memcount: %d\n", memcount() );
    fflush(stdout);
    sleep(1);

    exit(0);
#endif
    
    FILE *conf_file = NULL;
    int rc = 0;
    
    conf_file = fopen("/etc/npf.conf", "w+" );
    if( ! conf_file )
    {   
        printf( "Could not create npf.cfg\n" );
        exit(1);
    }   

    rc = fprintf( conf_file, "group default {\n\truleset \"test-set\"\n\tpass all\n}" );
    if( ! rc )
    {   
        perror("fprintf");
        exit(1);
    }   

	fflush( conf_file );
	fclose( conf_file );

    
    printf("Welcome to rumprun npfctl\n");
    fflush(stdout);
    fflush(stderr);
    sleep(1);

#if 0
    printf("Entering rumpctrl mode\n");
    while(1)
        sleep(1);
#endif
    
    char *block_cmd[256] = {
        "npfctl",
        "table",
        "blacklist",
        "add",
        "10.30.30.99",
    };

    char *reload_cmd[256] = {
        "npfctl",
        "reload",
        "/data/npf.conf"
    };
    
    /*
    char *load_cmd[256] = {
        "npfctl",
        "load",
        "/etc/npf.conf"
    };
    
    
    char *show_cmd[256] = {
        "npfctl",
        "show",
    };
    */
    
    
    char *start_cmd[256] = {
        "npfctl",
        "start"
    };

    char *stop_cmd[256] = {
        "npfctl",
        "stop"
    };


    do_npfctl_command( 3, reload_cmd );
    do_npfctl_command( 2, start_cmd );
    do_npfctl_command( 5, block_cmd );
    do_npfctl_command( 2, stop_cmd );
    do_npfctl_command( 2, start_cmd );

#if 1
    printf( "giving you time to test ping from deku\n" );
    for( int i = 0; i < 20; i++ )
    {
        sleep(1);
    }
#endif
}
