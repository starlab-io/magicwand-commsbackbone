#include "xen_comms.h"

#include <bmk-rumpuser/core_types.h>

#include <mini-os/types.h>
#include <mini-os/hypervisor.h>
#include <mini-os/kernel.h>

#include <mini-os/xenbus.h> // xenbus_transaction_start
#include <xen/xen.h>


int
xen_comms_init( void )
{
    int rc = 0;

    return rc;
}

int
xen_comms_register_callback( xen_event_callback_t Callback )
{
    int rc = 0;

    return rc;

}


int
xen_comms_fini( void )
{
    int rc = 0;

    return rc;
}
