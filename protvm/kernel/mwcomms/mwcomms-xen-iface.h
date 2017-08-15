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
#include <message_types.h>
#include <xen_keystore_defs.h>


typedef int
mw_xen_init_complete_cb_t( void );

typedef void
mw_xen_event_handler_cb_t( void );


// Defines:
// union mwevent_sring_entry
// struct mwevent_sring_t
// struct mwevent_front_ring_t
// struct mwevent_back_ring_t
DEFINE_RING_TYPES( mwevent, mt_request_generic_t, mt_response_generic_t );


// Per-INS data
typedef struct _mwcomms_ins_data
{
    atomic64_t    in_use;
    domid_t       domid;

    // Ring memory descriptions
    // Is there a reason to keep a ring and an sring
    // reference anymore?
    mw_region_t   ring;
    struct mwevent_sring * sring;
    struct mwevent_front_ring front_ring;

    bool is_ring_ready;

    //Grant refs shared with this INS
    grant_ref_t   grant_refs[ XENEVENT_GRANT_REF_COUNT ];

    int           common_evtchn;
    int           irq;

     // Time last seen, in jiffies
    unsigned long last_seen_time;

    // count: how many heartbeats have been missed?
    int           missed_heartbeats;

} mwcomms_ins_data_t;

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

int
mw_xen_write_to_key( const char * Dir, const char * Node, const char * Value );

int
mw_xen_get_next_request_slot( bool WaitForRing, uint8_t **dest );

int
mw_xen_dispatch_request( mt_request_generic_t      * Request,
                         uint8_t                   * dest);    

int
mw_xen_get_next_response( OUT mt_response_generic_t     **Response,
                          IN  mwcomms_ins_data_t        *Ins );

int
mw_xen_mark_response_consumed( mwcomms_ins_data_t *Ins );

bool
mw_xen_iface_ready( void );

int
mw_xen_read_old_ins( void );

bool
mw_xen_response_available( mwcomms_ins_data_t **Ins );


#endif // mwcomms_xen_iface_h
