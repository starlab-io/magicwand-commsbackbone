
#include <mini-os/semaphore.h>
#include <bmk-core/memalloc.h> // bmk_memalloc(size, align, who)
#include <bmk-core/printf.h>

#include "xenevent_minios.h"

#include "xenevent_common.h"

int
xenevent_semaphore_init( xenevent_semaphore_t * Semaphore )
{
    int rc = 0;

    struct semaphore * s = (struct semaphore *)
        bmk_memalloc( sizeof(struct semaphore), 0, BMK_MEMWHO_WIREDBMK );

    if ( NULL == s )
    {
        rc = BMK_ENOMEM;
        MYASSERT( !"bmk_memalloc" );
        goto ErrorExit;
    }

    init_SEMAPHORE( s, 0 );

    *Semaphore = (xenevent_semaphore_t) s;

ErrorExit:
    return rc;
}

void
xenevent_semaphore_destroy( xenevent_semaphore_t * Semaphore )
{
    MYASSERT( Semaphore );

    if ( NULL == *Semaphore )
    {
        goto ErrorExit;
    }

    bmk_memfree( *Semaphore, BMK_MEMWHO_WIREDBMK );

    *Semaphore = NULL;

ErrorExit:
    return;
}

void
xenevent_semaphore_up( xenevent_semaphore_t Semaphore )
{
    up( (struct semaphore *) Semaphore );
}

void
xenevent_semaphore_down( xenevent_semaphore_t Semaphore )
{
    down( (struct semaphore *) Semaphore );
}
