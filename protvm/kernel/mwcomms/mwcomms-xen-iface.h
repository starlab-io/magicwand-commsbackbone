/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

#ifndef mwcomms_xen_iface_h
#define mwcomms_xen_iface_h

#include "mwcomms-common.h"

#include <xen/grant_table.h>
#include <xen/page.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/interface/callback.h>
#include <xen/interface/io/ring.h>

#include <message_types.h>
#include <xen_keystore_defs.h>


typedef int
mw_xen_init_complete_cb_t( domid_t Domid );

typedef void
mw_xen_event_handler_cb_t( void );


typedef int
mw_xen_per_ins_cb_t( IN domid_t Domid, IN void * Arg );

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
mw_xen_init( mw_xen_init_complete_cb_t CompletionCallback,
             mw_xen_event_handler_cb_t EventCallback );


// @brief Cleans up the xen subsystem
void
mw_xen_fini( void );


// @brief Gets the domid of the VM we're running in
domid_t
mw_xen_get_local_domid( void );

/*
// @brief Sends an event on the common event channel.
void
mw_xen_send_event( void * Handle );
*/

int
mw_xen_for_each_live_ins( IN mw_xen_per_ins_cb_t Callback,
                          IN void *              Arg );


int
mw_xen_write_to_key( const char * Dir, const char * Node, const char * Value );

int
mw_xen_get_next_request_slot( IN  bool                    WaitForRing,
                              IN  domid_t                 DomId,
                              OUT mt_request_generic_t ** Dest,
                              OUT void                 ** Handle );

int
mw_xen_dispatch_request( void * Handle );

int
mw_xen_get_next_response( OUT mt_response_generic_t ** Response,
                          IN  void                   * Handle );

int
mw_xen_mark_response_consumed( void * Handle );

bool
mw_xen_iface_ready( void );

int
mw_xen_read_old_ins( void );


bool
mw_xen_response_available( void ** Handle );

int
mw_xen_get_active_ins_domids( domid_t Domids[] );

int
mw_xen_reap_dead_ins( void );

#endif // mwcomms_xen_iface_h
