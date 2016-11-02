// Assorted support functions that rely on the NetBSD API. Put them
// here to avoid header file troubles.

// Headers in src-netbsd/sys/sys

#include <sys/cdefs.h>

#if defined(_KERNEL_OPT)
#   include "opt_compat_netbsd.h"
#endif

#include <sys/atomic.h>
#include <sys/kmem.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/systm.h> // printf

#include "xenevent_common.h"
#include "xenevent_netbsd.h"


//
// General utility functions
//

// From
// http://stackoverflow.com/questions/7775991/how-to-get-hexdump-of-a-structure-data
void
hex_dump( const char *desc, void *addr, int len )
{
#ifdef MYDEBUG // do NOT use the DEBUG_PRINT() macro here
    int i;
    unsigned char buff[17];
    unsigned char *pc = (unsigned char*)addr;

    // Output description if given.
    if (desc != NULL)
        printf ("%s:\n", desc);

    if (len == 0) {
        printf("  ZERO LENGTH\n");
        return;
    }
    if (len < 0) {
        printf("  NEGATIVE LENGTH: %i\n",len);
        return;
    }

    // Process every byte in the data.
    for (i = 0; i < len; i++) {
        // Multiple of 16 means new line (with line offset).

        if ((i % 16) == 0) {
            // Just don't print ASCII for the zeroth line.
            if (i != 0)
                printf ("  %s\n", buff);

            // Output the offset.
            printf ("  %04x ", i);
        }

        // Now the hex code for the specific character.
        printf (" %02x", pc[i]);

        // And store a printable ASCII character for later.
        if ((pc[i] < 0x20) || (pc[i] > 0x7e))
            buff[i % 16] = '.';
        else
            buff[i % 16] = pc[i];
        buff[(i % 16) + 1] = '\0';
    }

    // Pad out last line if not exactly 16 characters.
    while ((i % 16) != 0) {
        printf ("   ");
        i++;
    }

    // And print the final ASCII bit.
    printf ("  %s\n", buff);
#endif // MYDEBUG
}


int
xenevent_mutex_init( xenevent_mutex_t* Mutex )
{
    kmutex_t * mutex = NULL;
    int rc = 0;

    mutex = malloc( sizeof(kmutex_t), 0, M_WAITOK | M_ZERO | M_CANFAIL );
    if ( NULL == mutex )
    {
        MYASSERT( !"malloc" );
        rc = BMK_ENOMEM;
        goto ErrorExit;
    }
    
    mutex_init( mutex, MUTEX_DEFAULT, IPL_HIGH);

    *Mutex = (xenevent_mutex_t) mutex;

ErrorExit:
    return rc;
}


void
xenevent_mutex_wait( xenevent_mutex_t Mutex )
{
    mutex_enter( (kmutex_t *) Mutex );
}


void
xenevent_mutex_release( xenevent_mutex_t Mutex )
{
    mutex_exit( (kmutex_t *) Mutex );
}


void
xenevent_mutex_destroy( xenevent_mutex_t * Mutex )
{
    if ( NULL != *Mutex )
    {
        free ( *Mutex, 0 );
        *Mutex = NULL;
    }
}


uint32_t
xenevent_atomic_inc( uint32_t * old )
{
    return atomic_inc_32_nv( old );
}


uint32_t
xenevent_atomic_dec( uint32_t * old )
{
    return atomic_dec_32_nv( old );
}
