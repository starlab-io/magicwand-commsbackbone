
#include <sys/types.h>
#include <bmk-rumpuser/rumpuser.h>

#include <mini-os/semaphore.h>
#include <bmk-core/memalloc.h> // bmk_memalloc(size, align, who)
#include <bmk-core/printf.h>
#include <bmk-core/sched.h>

#include "xenevent_minios.h"
#include "xenevent_rump.h"
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
    int nlocks = 0;

    MYASSERT( Semaphore );

    // Here we attempt to down the semaphore. If that fails, then we
    // might have to wait a long time before the semaphore can be
    // acquired. Due to the way the Rump scheduler works, this means
    // that we must unbind this thread from the current CPU; otherwise
    // we could starve other threads.

    if ( trydown( (struct semaphore *) Semaphore ) )
    {
        goto ErrorExit;
    }

    // We're going to have to wait, possibly for a long time
    rumpkern_unsched( &nlocks, NULL );

    // Wait....
    down( (struct semaphore *) Semaphore );

    // The wait is complete and the thread is resuming. Re-bind us to
    // the CPU.
    rumpkern_sched( nlocks, NULL );

ErrorExit:
    return;
}

int
xenevent_semaphore_trydown( xenevent_semaphore_t Semaphore )
{
    return trydown( (struct semaphore *) Semaphore );
}
