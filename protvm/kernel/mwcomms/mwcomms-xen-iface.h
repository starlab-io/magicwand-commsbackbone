/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

#ifndef mwcomms_xen_iface_h
#define mwcomms_xen_iface_h

#include <xen/grant_table.h>
#include <xen/page.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/interface/callback.h>
#include <xen/interface/io/ring.h>

#include "mwcomms-common.h"


typedef int
mw_xen_init_complete_cb_t( domid_t ClientId );

typedef void
mw_xen_event_handler_cb_t( void );



/// @brief Initializes the Xen subsystem and initiates handshake with client
///
/// When CompletionCallback is invoked, the handshake has completed
/// but the ring buffer has not yet been initialized.
///
/// When events arrive, the EventCallback is invoked.
///
/// @param
/// @param
/// @param Function that is invoked when handshake is complete.
int
mw_xen_init( mw_region_t * SharedMem,
             mw_xen_init_complete_cb_t CompletionCallback,
             mw_xen_event_handler_cb_t EventCallback );


// @brief Cleans up the xen subsystem
void
mw_xen_fini( void );


// @brief Gets the domid of the VM we're running in
domid_t
mw_xen_get_local_domid( void );


// @brief Sends an event on the common event channel.
void
mw_xen_send_event( void );


#endif // mwcomms_xen_iface_h
