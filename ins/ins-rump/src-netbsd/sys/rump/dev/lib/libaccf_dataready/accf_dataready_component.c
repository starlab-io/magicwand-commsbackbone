


#include <sys/cdefs.h>
//__KERNEL_RCSID(0, "$NetBSD: accf_data component albis" );

#include <sys/param.h>

#include <rump-sys/kern.h>
#include <rump-sys/dev.h>

#include "accf_data.h"
#include "ioconf.h"

RUMP_COMPONENT(RUMP_COMPONENT_DEV)
{
    int error;
    error = accf_dataready_modcmd( MODULE_CMD_INIT, NULL );
    if( error != 0 )
    {
        panic( "Could not load accf_data device" );
    }
}
