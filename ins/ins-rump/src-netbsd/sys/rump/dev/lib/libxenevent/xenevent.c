/**************************************************************************
 * STAR LAB PROPRIETARY & CONFIDENTIAL
 * Copyright (C) 2018, Star Lab — All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited.
 **************************************************************************/


//
// Calls the initialization code for Rump/NetBSD
//

// BEGIN headers netbsd/sys/rump/dev/lib/librnd/rnd_component.c
#include <sys/cdefs.h>

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/rnd.h>
#include <sys/stat.h>

#include <rump-sys/kern.h>
#include <rump-sys/dev.h>
#include <rump-sys/vfs.h>

#include "ioconf.h"

// END headers netbsd/sys/rump/dev/lib/librnd/rnd_component.c

#include "xenevent_common.h"
#include "xenevent_device.h"

/////////////////////////////////////////////

RUMP_COMPONENT(RUMP_COMPONENT_DEV)
{
    int error;

    error = xe_dev_init();
    if ( 0 != error )
    {
        DEBUG_BREAK();
    }
}
