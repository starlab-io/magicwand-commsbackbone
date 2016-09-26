/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

//
// This code relies heavily on minios. Note in particular:
//
// minios must export the symbols we need here. This means that the
// symbols we use must be covered by the prefixes found in
// platform/xen/xen/Makefile in the GLOBAL_PREFIXES variable.
//
// The symbols we use must not be mangled by the Rump build
// system. Look at RUMP_SYM_NORENAME in this library's Makefile for
// details.
//
// Include files come from
// platform/xen/xen/include/mini-os
// platform/xen/xen/include/xen
// include/bmk-core
//


#include <sys/stdint.h>

#include <mini-os/types.h>
#include <xen/xen.h>
#include <xen/io/xs_wire.h>
#include <xen/io/ring.h>

#if defined(__i386__)
#include <mini-os/x86/x86_32/hypercall-x86_32.h>
#elif defined(__x86_64__)
#include <mini-os/x86/x86_64/hypercall-x86_64.h>
#else
#error "Unsupported architecture"
#endif

#include <xen/sched.h>
#include <mini-os/xenbus.h>
#include <mini-os/events.h>
#include <mini-os/os.h>
#include <mini-os/mm.h>
#include <mini-os/gnttab.h>
#include <mini-os/gntmap.h>
#include <mini-os/semaphore.h>

#include <bmk-core/string.h>
#include <bmk-core/printf.h>
#include <bmk-core/memalloc.h>
#include <bmk-core/pgalloc.h>

#include "xenevent_common.h"
#include "xenevent_comms.h"

#include "xenevent_minios.h"
#include "message_types.h"
#include "xen_keystore_defs.h"


// The RING macros use memset
#define memset bmk_memset

// Defines:
// mwevent_sring_t
// mwevent_front_ring_t
// mwevent_back_ring_t
DEFINE_RING_TYPES( mwevent, mt_request_generic_t, mt_response_generic_t );


// Maintain all state here for future flexibility
typedef struct _xen_comm_state
{
    bool comms_established;

    //
    // Main shared memory - prepended with shared ring info, followed
    // by slots for request/response structures
    //
    uint8_t * shared_mem;
    size_t    shared_mem_size;

    // Grant map 
    struct gntmap gntmap_map;

    // One grant ref is required per page
    grant_ref_t    grant_refs[DEFAULT_NMBR_GNT_REF];

    // Ring buffer types. This side implements the "back end" of the
    // ring: requests are taken off the ring and responses are put on
    // it. The "sring" is the shared ring - it points to a contiguous
    // block of shared pages.
    mwevent_sring_t     * shared_ring;
    mwevent_back_ring_t back_ring;

    // Event channel memory
    void * event_channel_mem;
    size_t    event_channel_mem_size;
    
    // Event channel local port
    evtchn_port_t local_event_port;
    
    // Self event channel memory - for creating an unbound event channel.
    // XXXXX remove this
    uint8_t * self_event_channel_mem;

    // Self event channel local port
    evtchn_port_t self_event_port;

    // Semaphore that is signalled once for each message that has arrived
    xenevent_semaphore_t messages_available;

} xen_comm_state_t;

static xen_comm_state_t g_state;

/***************************************************************************
 * Basic utility functions
 ***************************************************************************/

static int
xe_comms_write_int_to_key( const char * Path,
                           const int Value );


static int
xe_comms_write_int_to_key( const char * Path,
                           const int Value )
{
    xenbus_transaction_t    txn;
    int                   retry;
    char                   *err;
    int                     res = 0;
    char buf[MAX_KEY_VAL_WIDTH];
    bool             started = false;

    bmk_memset( buf, 0, sizeof(buf) );
    bmk_snprintf( buf, sizeof(buf), "%u", Value );

    DEBUG_PRINT( "Writing to xenstore: %s <= %s\n", Path, buf );
    
    err = xenbus_transaction_start(&txn);
    if (err)
    {
        MYASSERT( !"xenbus_transaction_start" );
        goto ErrorExit;
    }

    started = true;
    
    err = xenbus_write(txn, Path, buf);
    if (err)
    {
        MYASSERT( !"xenbus_transaction_start" );
        goto ErrorExit;
    } 

ErrorExit:
    if ( err )
    {
        res = 1;
        DEBUG_PRINT( "Failure: %s\n", err );
        bmk_memfree(err, BMK_MEMWHO_WIREDBMK);
    }

    if ( started )
    {
        (void) xenbus_transaction_end(txn, 0, &retry);
    }
    
    return res;
}

static int
xe_comms_read_int_from_key( IN const char *Path,
                            OUT int * OutVal)
{
    xenbus_transaction_t txn;
    char                *val;
    int                retry;
    char                *err;
    int                  res = 0;
    bool             started = false;
    
    *OutVal = 0;
        
    val = NULL;

    err = xenbus_transaction_start(&txn);
    if (err)
    {
        MYASSERT( !"xenbus_transaction_start" );
        goto ErrorExit;
    }

    started = true;
    
    err = xenbus_read(txn, Path, &val);
    if (err)
    {
        MYASSERT( !"xenbus_read" );
        goto ErrorExit;
    }

    *OutVal = bmk_strtoul( val, NULL, 10 );

    DEBUG_PRINT( "Read from xenstore: %s => %s\n", Path, val );
    
ErrorExit:
    if ( err )
    {
        res = 1;
        DEBUG_PRINT( "Failure: %s\n", err );
        bmk_memfree(err, BMK_MEMWHO_WIREDBMK);
    }

    if ( started )
    {
        (void) xenbus_transaction_end(txn, 0, &retry);
    }

    if ( val )
    {
        bmk_memfree( val, BMK_MEMWHO_WIREDBMK );
    }
    
    return res;
}

static int
xe_comms_wait_and_read_int_from_key( IN const char *Path,
                                     OUT int * OutVal)
{
    int     rc = 0;
    bool printed = false;
    struct xenbus_event_queue events;

    bmk_memset( &events, 0, sizeof(events) );
    xenbus_event_queue_init(&events);

    xenbus_watch_path_token(XBT_NIL, Path, Path, &events);

    // Wait until we can read the key and its value is non-zero
    while ( (rc = xe_comms_read_int_from_key( Path, OutVal ) ) != 0
            || (0 == *OutVal) )
    {
        if ( !printed )
        {
            DEBUG_PRINT( "Waiting for key %s to appear. Read value %d\n",
                         Path, *OutVal );
            //printed = true;
        }
        xenbus_wait_for_watch(&events);
    }

    xenbus_unwatch_path_token(XBT_NIL, Path, Path );

    return rc;
}


/***************************************************************************
 * Functions for item read/write via Xen ring buffer
 ***************************************************************************/

static int
send_event(evtchn_port_t port)
{

    int                err;
//    struct evtchn_send send;

    DEBUG_PRINT( "Sending event to (local) port %d\n", port );
    err = minios_notify_remote_via_evtchn( port );
    if ( err )
    {
        MYASSERT( !"Failed to send event" );
    }
#if 0
    
    send.port = port;

    err = HYPERVISOR_event_channel_op(EVTCHNOP_send, &send);

    if (err)
    {
        DEBUG_PRINT("Error: Failed to send event\n");
        DEBUG_PRINT("Port Number: %u\n", send.port);
        MYASSERT(!"Could not send event over event channel\n");
        goto ErrorExit;
    }

    /*
    if (err) {
        DEBUG_PRINT("\tError: Failed to send event\n");
        DEBUG_PRINT("\t\tPort Number: %u\n", send.port);
    } else {
        DEBUG_PRINT("\tSent Event\n");
        DEBUG_PRINT("\t\tPort Number: %u\n", send.port);
    }
    */

ErrorExit:
#endif
    return err;
}


static void
xe_comms_event_callback( evtchn_port_t port,
                         struct pt_regs *regs,
                         void *data )
{
    DEBUG_PRINT("Event Channel %u\n", port );
    DEBUG_BREAK();

    //send_event(g_state.self_event_port);

    //
    // TODO: 
    
    //
    // TODO: Extract the event type and connection ID from shared memory.
    //
    // If this is a new connection, deliver the event to the "new
    // connection" thread.
    //
    // Otherwise, deliver the event to the thread that has registered
    // for the given connection ID.
    //

//    (void) xe_comms_handle_arrived_data( (event_id_t) 1 );

    //
    // Release xe_comms_read_item() to check for another item
    //
    xenevent_semaphore_up( g_state.messages_available );
}

#if 0
// Unused
static void
xe_comms_event_self_chn_callback( evtchn_port_t port,
                                  struct pt_regs *regs,
                                  void *data )
{
    DEBUG_PRINT("Self Event Channel %u\n", port );
    DEBUG_BREAK();

    //
    // TODO: 

    //
    // TODO: Extract the event type and connection ID from shared memory.
    //
    // If this is a new connection, deliver the event to the "new
    // connection" thread.
    //
    // Otherwise, deliver the event to the thread that has registered
    // for the given connection ID.
    //

//    (void) xe_comms_handle_arrived_data( (event_id_t) 1 );
}
#endif

/**
 * xe_comms_read_item
 *
 * Consumes one request from the ring buffer. If none is available, it
 * blocks until one is. Potentially consumes multiple semaphore
 * signals ("up" functions) without an item becoming available.
 */
int
xe_comms_read_item( void * Memory,
                    size_t Size,
                    size_t * BytesRead )
{
    int rc = 0;

    DEBUG_PRINT( "Sending event on port %d\n", g_state.local_event_port );
    send_event( g_state.local_event_port );

#if 0

//    bool available = false;
//    mt_request_generic_t * request = NULL;

    do
    {
        available = RING_HAS_UNCONSUMED_REQUESTS( &g_state.back_ring );

        if ( !available )
        {
            // Nothing was available. Block until event arrives and try again.
            xenevent_semaphore_down( g_state.messages_available );
            continue;
        }
    } while ( !available );

    // Consume the request
    request = (mt_request_generic_t *)
        RING_GET_REQUEST( &g_state.back_ring,
                          g_state.back_ring.req_cons );

    if ( request->base.size > Size )
    {
        rc = BMK_EINVAL;
        MYASSERT( !"Command buffer is too big for destination buffer" );
        goto ErrorExit;
    }

    // Total size: header + payload sizes
    *BytesRead = sizeof(request->base) + request->base.size;
    bmk_memcpy( Memory, request, *BytesRead );

ErrorExit:

    // Advance the counter for the next request
    ++g_state.back_ring.req_cons;
#endif    
    return rc;
}

/**
 * xe_comms_write_item
 *
 * Produces one response and puts it on the ring buffer. Since we
 * produce one reponse per request, we are guaranteed an available
 * slot to put the response.
 */
int
xe_comms_write_item( void * Memory,
                     size_t Size )
{
    int rc = 0;
    bool send_event = false;

    // A response can only be placed in a slot that was used by a
    // request that has been consumed. An overflow here is
    // "impossible".

    // Get the buffer for the next response and populate it
    void * dest = RING_GET_RESPONSE( &g_state.back_ring,
                                     g_state.back_ring.rsp_prod_pvt );

    bmk_memcpy( dest, Memory, Size );
    
    RING_PUSH_RESPONSES_AND_CHECK_NOTIFY( &g_state.back_ring, send_event );

    ++g_state.back_ring.rsp_prod_pvt;

    if ( send_event )
    {
        // send event to remote side
    }

//ErrorExit:
    return rc;
}

/*
int
xe_comms_read_data( event_id_t Id,
                    void * Memory,
                    size_t Size )
{
    int rc = 0;

    //
    // Get an available event. Keep doing it until one is
    // available. TODO: Find a smarter way to do this... mutex?
    //
    do
    {
        rc = xe_comms_get_next_event_struct( &readEvent );
        DEBUG_PRINT( "Found available event structure: %p\n", readEvent );
    } while ( BMK_EBUSY == rc );

    MYASSERT( EVENT_ID_INVALID == readEvent->id );
    readEvent->id = Id;
    readEvent->dest_mem = Memory;
    readEvent->dest_sz  = Size;

    //
    // Prepare the event's mutex, which the Xen event handler will
    // release. It starts off in an acquired state.
    //
    init_mutex_locked( &readEvent->mutex );
    
    DEBUG_PRINT( "Traffic 0x%llx waiting on mutex at %p\n",
                 Id, &readEvent->mutex );
    
    // The entry is in the list. Now it's safe to enable events.
    if ( 1 == xenevent_atomic_inc( &g_state.outstanding_reads ) )
    {
        minios_unmask_evtchn( g_state.local_event_port );
    }

    //
    // Block this thread until this read event is satisfied by an event callback.
    //
    
    xe_comms_mutex_acquire( &readEvent->mutex );
    DEBUG_PRINT( "Event has arrived and been processed\n" );
    DEBUG_BREAK();

    //
    // The thread has passed the block and the read is satisfied. Clean up.
    //
    
    //
    // If there are no other outstanding reads, disable event delivery.
    //
    if ( 0 == xenevent_atomic_dec( &g_state.outstanding_reads ) )
    {
        //xe_comms_disable_events( g_state.local_event_port );
        minios_mask_evtchn( g_state.local_event_port );
    }

    // Safe to do this unconditionally
    xe_comms_set_event_available( readEvent );

    return rc;
}
*/


/*
 *
 */
static int
receive_grant_references( void )
{
    struct xenbus_event_queue events;
    int rc = 0;
    char * err = NULL;
    char * msg = NULL;

    // One grant ref per shared page
    for ( int i = 0; i < DEFAULT_NMBR_GNT_REF; i++ )
    {
        xenbus_event_queue_init(&events);

        xenbus_watch_path_token(XBT_NIL, GRANT_REF_PATH, GRANT_REF_PATH, &events);
        while ( (err = xenbus_read(XBT_NIL, GRANT_REF_PATH, &msg)) != NULL
                ||  msg[0] == '0') {
            bmk_memfree(msg, BMK_MEMWHO_WIREDBMK);
            bmk_memfree(err, BMK_MEMWHO_WIREDBMK);
            xenbus_wait_for_watch(&events);
        }

        xenbus_unwatch_path_token(XBT_NIL, GRANT_REF_PATH, GRANT_REF_PATH);

        DEBUG_PRINT("Action on Grant State Key\n");

        // Read in the grant reference
        rc = xe_comms_read_int_from_key( GRANT_REF_PATH, &g_state.grant_refs[i] );
        if ( rc )
        {
            goto ErrorExit;
        }

        // XXXXXXXXXX signal that we're ready for the next one

        
    }
ErrorExit:
    return rc;
}


/*
 * Client-side function that maps in a page of memory 
 *  granted by the server. 
 *
 *  domu_server_id   - Domain Id of the server 
 *  client_grant_ref - Client grant reference to 
 *                     shared memory
 *  msg_len          - Length of data the server writes
 *                     to shared memory
 *
 *  return - 0 if successful, 1 if not 
 */

static int
xe_comms_accept_grant(domid_t      domu_server_id, 
                      grant_ref_t  client_grant_ref)
{
    // All pages are shared with one dom ID
    uint32_t       domids[1];

    gntmap_init( &g_state.gntmap_map );

    // All the pages are being shared with one dom ID
    domids[ 0 ]    = domu_server_id;

    // Allocates a block of memory and shares
    g_state.shared_mem = 
        gntmap_map_grant_refs( &g_state.gntmap_map,
                               DEFAULT_NMBR_GNT_REF, // page count
                               domids,   // dom ID (only 1)
                               0,        // dom ID stride - use the same one
                               g_state.grant_refs,
                               WRITE_ACCESS_ON );
    if (NULL == g_state.shared_mem)
    {
        MYASSERT( !"Mapping in the Memory bombed out!\n");
        return BMK_ENOMEM;
    }

    DEBUG_PRINT("Shared Mem: %p\n", g_state.shared_mem);

    return 0;
}


static int
xe_comms_bind_to_interdom_chn (domid_t srvr_id,
                               evtchn_port_t remote_prt_nmbr)
{
    int err = 0;
    // Don't use minios_evtchn_bind_interdomain; spurious events are
    // delivered to us when we do.
    
    g_state.event_channel_mem = bmk_pgalloc_one();
    if (NULL == g_state.event_channel_mem)
    {
        MYASSERT( !"Failed to alloc Event Channel page" );
        err = BMK_ENOMEM;
        goto ErrorExit;
    }

    DEBUG_PRINT("Event Channel page address: %p\n", g_state.event_channel_mem);
    bmk_memset(g_state.event_channel_mem, 0, PAGE_SIZE);	
 
    // Ports are bound in the masked state
    err = minios_evtchn_bind_interdomain( srvr_id,
                                          remote_prt_nmbr,
                                          xe_comms_event_callback,
                                          g_state.event_channel_mem,
                                          &g_state.local_event_port );
    if (err)
    {
        MYASSERT(!"Could not bind to event channel\n");
        goto ErrorExit;
    }
    
    // Clear channel of events and unmask
    minios_clear_evtchn( g_state.local_event_port );
    minios_unmask_evtchn( g_state.local_event_port );

    // Indicate that the VM's event channel is bound
    err = xe_comms_write_int_to_key( VM_EVT_CHN_IS_BOUND, 1 );
    if ( err )
    {
        goto ErrorExit;
    }

    bmk_printf( "Waiting." );
    for ( int i = 0; i < 10; i++ )
    {
        if ( i % 5 == 0 ) bmk_printf(".");
    }

    send_event( g_state.local_event_port );
    
ErrorExit:
    return err;
}

#if 0
// Creates an unbound event channel
static int
xe_comms_create_interdom_chn ( domid_t srvr_id )
{
    int err = 0;

    g_state.self_event_channel_mem = bmk_pgalloc_one();
    if (NULL == g_state.self_event_channel_mem)
    {
        MYASSERT( !"Failed to alloc Self Event Channel page" );
        err = BMK_ENOMEM;
        goto ErrorExit;
    }

    DEBUG_PRINT("Self Event Channel page address: %p\n", g_state.self_event_channel_mem);
    bmk_memset(g_state.self_event_channel_mem, 0, PAGE_SIZE);

    err = minios_evtchn_alloc_unbound(srvr_id,
                                      xe_comms_event_self_chn_callback,
                                      g_state.self_event_channel_mem,
                                      &g_state.self_event_port);
    if (err)
    {
        MYASSERT(!"Could not create self event channel\n");
        goto ErrorExit;
    }

    // Clear channel of events and unmask
    minios_clear_evtchn( g_state.self_event_port );
    minios_unmask_evtchn( g_state.self_event_port );

    DEBUG_PRINT("Local port for self event channel: %u\n", g_state.self_event_port );

    err = xe_comms_write_int_to_key( UK_EVT_CHN_PRT_PATH, g_state.self_event_port );
    if (err)
    {
        goto ErrorExit;
    }

ErrorExit:
    return err;
}
#endif


////////////////////////////////////////////////////
//
// xe_comms_init
//
// Initializes the channel to Xen. This means we wait on the remote
// (the "server") ID and grant reference to appear.
//
int
xe_comms_init( void ) //IN xenevent_semaphore_t MsgAvailableSemaphore )
{
    int             rc = 0;
    domid_t         localId = 0;
    domid_t         remoteId = 0;

    grant_ref_t	    client_grant_ref = 0;
    evtchn_port_t   vm_evt_chn_prt_nmbr = 0;

    DEBUG_BREAK();
    bmk_memset( &g_state, 0, sizeof(g_state) );

    rc = xenevent_semaphore_init( &g_state.messages_available );
    if ( 0 != rc )
    {
        goto ErrorExit;
    }
    
    //
    // Begin init protocol
    //

    // Read our own dom ID
    rc = xe_comms_read_int_from_key( PRIVATE_ID_PATH, (int *) &localId );
    if ( rc )
    {
        goto ErrorExit;
    }

    // Write our dom ID to agreed-upon path
    rc = xe_comms_write_int_to_key( CLIENT_ID_PATH, localId );
    if ( rc )
    {
        goto ErrorExit;
    }

    // Wait for the server ID.
    rc = xe_comms_wait_and_read_int_from_key( SERVER_ID_PATH,
                                              (int *) &remoteId );
    if ( rc )
    {
        goto ErrorExit;
    }
    DEBUG_PRINT( "Discovered remote server ID: %u\n", remoteId );

    //
    // Wait for the grant reference
    //
    rc = receive_grant_references();
    if ( rc )
    {
        goto ErrorExit;
    }

    // Map in the page offered by the remote side
    rc = xe_comms_accept_grant( remoteId, client_grant_ref );
    if ( rc )
    {
        goto ErrorExit;
    }

    // Get the event port and bind to it
    rc = xe_comms_wait_and_read_int_from_key( VM_EVT_CHN_PRT_PATH,
                                              &vm_evt_chn_prt_nmbr );
    if ( rc )
    {
        goto ErrorExit;
    }

    rc = xe_comms_bind_to_interdom_chn( remoteId, vm_evt_chn_prt_nmbr );
    if ( rc )
    {
        goto ErrorExit;
    }

    // The grant has been accepted. We can use shared memory for the
    // ring buffer now. We're the back end, so we only perform the back init.
    BACK_RING_INIT( &g_state.back_ring,
                    g_state.shared_ring,
                    PAGE_SIZE * DEFAULT_NMBR_GNT_REF);
#if 0
    // We do not currently create an unbound channel.
    rc = xe_comms_create_interdom_chn ( remoteId );
    if ( rc )
    {
        goto ErrorExit;
    }
#endif
    //send_event(g_state.self_event_port);
    for ( int i = 0; i < 3; i++ )
    {
        send_event(g_state.local_event_port);
    }
    
ErrorExit:
    return rc;
}


int
xe_comms_fini( void )
{
    int rc = 0;

    DEBUG_PRINT("Unbinding from Local Interdomain Event Channel Port: %u ...\n",
                g_state.local_event_port);

    minios_mask_evtchn( g_state.local_event_port );
    minios_clear_evtchn( g_state.local_event_port );

    minios_unbind_evtchn(g_state.local_event_port);

    
    xenevent_semaphore_destroy( &g_state.messages_available );

    
    if (g_state.event_channel_mem)
    {
        bmk_memfree(g_state.event_channel_mem, BMK_MEMWHO_WIREDBMK);
    }

//    if (g_state.self_event_channel_mem)
//    {
//        bmk_memfree(g_state.self_event_channel_mem, BMK_MEMWHO_WIREDBMK);
//    }

    gntmap_fini( &g_state.gntmap_map );

    // bmk_memset( &g_state, 0, sizeof(g_state) );

    return rc;
}
