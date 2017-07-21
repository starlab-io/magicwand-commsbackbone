
#include <dirent.h> 
#include <stdio.h> 


void
iterdir( const char * Dir )
{
    DIR           *d;
    struct dirent *dir;
    char fname[ 128 ];

    printf( "iterdir(%s)\n", Dir );
    d = opendir( Dir );
    if ( NULL == d ) { goto ErrorExit; }

    while ( (dir = readdir(d) ) != NULL)
    {
        //printf( "%s\n", dir->d_name );
        snprintf( fname, sizeof(fname),
                  "%s/%s", Dir, dir->d_name );
        printf( "%s\n", fname );

        if ( DT_DIR == dir->d_type &&
             0 != strcmp( dir->d_name, "." ) &&
             0 != strcmp( dir->d_name, ".." )  )
        {
            iterdir( fname );
        }
    } // while

    closedir( d );

ErrorExit:
    return;
}



int main(void)
{
    //iterdir( "/dev" );
    iterdir( "/" );
    fflush( stdout );
    return(0);
}
