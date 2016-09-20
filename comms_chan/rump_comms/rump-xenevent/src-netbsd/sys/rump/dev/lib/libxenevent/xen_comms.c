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

// Debug macros should use minios_printk, not printf
#define DEBUG_PRINT_FUNCTION minios_printk
#include "xenevent_common.h"
#include "xen_comms.h"


//
// The number of slots for read events. This defines the maximum
// number of concurrent reads we can handle within our system.
//
#define MAX_READ_EVENTS 64


//
// Associate an outstanding read with an anticipated Xen event.
//
typedef struct _xen_read_event
{
    // Use this only with interlocked operations,
    // e.g. synch_cmpxchg(ptr,old,new) from os.h
    uint32_t in_use;
    
    event_id_t id;
    
    void * dest_mem;
    size_t dest_sz;
    size_t data_received;

    //
    // mini-os/semaphore.h
    //
    // This is used to communicate between the user-land thread that
    // is waiting on the read() to complete, and the thread that
    // handles the incoming Xen event by populating the data in this
    // structure and releasing the mutex.
    //
    struct semaphore mutex;
    
} xen_read_event_t;

// Maintain all state here for future flexibility
typedef struct _xen_comm_state
{
    bool comms_established;

    // alter only via interlocked operations - sys/atomic.h
    uint32_t handle_ct;

    xen_read_event_t read_events[ MAX_READ_EVENTS ];
    
    // How many reads are occuring currently
    uint32_t outstanding_reads;

    // Shared memory
    void *client_page;

    // Event channel page
    void *event_channel_page;

    // Event channel local port
    evtchn_port_t local_event_port;

    // Grant map 
    struct gntmap *gntmap_map;
    
} xen_comm_state_t;

static xen_comm_state_t g_state;


//
// Addition to semaphore.h
//
#define init_mutex_locked(m) init_SEMAPHORE( (m), 0 )

static void
xe_comms_mutex_acquire( struct semaphore * m )
{
    DEBUG_PRINT( "Mutex %p: acquiring\n", m );
    down( (m) );
    DEBUG_PRINT( "Mutex %p: acquired\n", m );
}

static void
xe_comms_mutex_release( struct semaphore * m )
{
    up( (m) );
    DEBUG_PRINT( "Mutex %p: released\n", m );
}

/*
static void
xe_comms_enable_events( evtchn_port_t Port )
{
   minios_unmask_evtchn( Port );
}

static void
xe_comms_disable_events( evtchn_port_t Port )
{
   minios_mask_evtchn( Port );
}
*/

//
// Finds the first event in the xen_read_event_t array whose in_use
// field is 0. Performs interlocked check.
//
// Return codes:
//    BMK_EAGAIN - All the slots are busy. The caller should try again.
//
static int
xe_comms_get_next_event_struct( xen_read_event_t ** AvailableEvent )
{

    int rc = BMK_EBUSY;
    
    for ( int i = 0; i < MAX_READ_EVENTS; i++ )
    {
        xen_read_event_t * evt = &g_state.read_events[i];

        // Synchronous compare exchange on the in_use field to see if
        // this element is available. If the field's previous value
        // was 0, then it is available (and we have taken it).
        if ( 0 == synch_cmpxchg( &evt->in_use, evt->in_use, 1 ) )
        {
            // The caller must set the id. Don't leak an old one.
            evt->id = EVENT_ID_INVALID;
            *AvailableEvent = evt;
            DEBUG_PRINT( "Found available event slot: ptr %p, idx %d\n", evt, i );
            rc = 0;
            break;
        }
    }

    return rc;
}

static int
xe_comms_find_event_by_id( event_id_t Id,
                           xen_read_event_t ** FoundEvent )
{
    int rc = BMK_ENOENT;

    *FoundEvent = NULL;
    
    for ( int i = 0; i < MAX_READ_EVENTS; i++ )
    {
        xen_read_event_t * evt = &g_state.read_events[i];
        if ( Id == evt->id )
        {
            *FoundEvent = evt;
            rc = 0;
            break;
        }
    }

    return rc;
}


//
// Sets the in_use field of the given event to 0, using interlocked
// function.
//
static void
xe_comms_set_event_available( xen_read_event_t * TargetEvent )
{
    MYASSERT( NULL != TargetEvent );

    TargetEvent->id = EVENT_ID_INVALID;
    (void) synch_cmpxchg( &TargetEvent->in_use, TargetEvent->in_use, 0 );
}


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
xe_comms_read_int_from_key( const char *Path,
                            int * OutVal)
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


int
xe_comms_read_data( event_id_t Id,
                    void * Memory,
                    size_t Size )
{
    int rc = 0;

    xen_read_event_t * readEvent = NULL;

    // There should be no event with the given id. Check this.
    MYASSERT( BMK_ENOENT == xe_comms_find_event_by_id( Id, &readEvent ) );

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

//
// xe_comms_handle_arrived_data
//
// Invoked by the Xen event callback.
static int
xe_comms_handle_arrived_data( event_id_t Id )
{
    int rc = 0;
    xen_read_event_t * curr = NULL;

    // Find the ID
    rc = xe_comms_find_event_by_id( Id, &curr );
    if ( 0 != rc )
    {
        goto ErrorExit;
    }
    
    // TODO: Copy the received data into the waiter's buffer.
    curr->data_received = MIN( curr->dest_sz, sizeof(TEST_MSG) );
    bmk_memcpy( curr->dest_mem, TEST_MSG, curr->data_received );

    // Release the waiting thread
    xe_comms_mutex_release( &curr->mutex );
    
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
    uint32_t       count         = DEFAULT_NMBR_GNT_REF;
    int            domids_stride = DEFAULT_STRIDE;
    int            write         = WRITE_ACCESS_ON;;

    grant_ref_t    grant_refs[DEFAULT_NMBR_GNT_REF];
    uint32_t       domids[DEFAULT_NMBR_GNT_REF];

    unsigned char  read_buf[TEST_MSG_SZ];

    bmk_memset(read_buf, 0, TEST_MSG_SZ);

    g_state.gntmap_map = (struct gntmap *)bmk_pgalloc_one();
	
    gntmap_init(g_state.gntmap_map);

    domids[FIRST_DOM_SLOT] = domu_server_id;
    grant_refs[FIRST_GNT_REF] = client_grant_ref;

    g_state.client_page = (unsigned char *)
        gntmap_map_grant_refs(g_state.gntmap_map,
                              count,
                              domids,
                              domids_stride,
                              grant_refs,
                              write);
    if (NULL == g_state.client_page)
    {
        MYASSERT( !"Mapping in the Memory bombed out!\n");
        return BMK_ENOMEM;
    }

    DEBUG_PRINT("Shared Mem: %p\n", g_state.client_page);

    return 0;
}

static void
xe_comms_event_callback( evtchn_port_t port,
                         struct pt_regs *regs,
                         void *data )
{
    DEBUG_PRINT("Event Channel %u\n", port );
    DEBUG_BREAK();

    //
    // TODO: Extract the event type and connection ID from shared memory.
    //
    // If this is a new connection, deliver the event to the "new
    // connection" thread.
    //
    // Otherwise, deliver the event to the thread that has registered
    // for the given connection ID.
    //
    (void) xe_comms_handle_arrived_data( (event_id_t) 1 );
}

#if 0
static int
xe_comms_bind_to_interdom_chn (domid_t srvr_id,
                               evtchn_port_t remote_prt_nmbr)
{
    int err = 0;
    // Don't use minios_evtchn_bind_interdomain; spurious events are
    // delivered to us when we do.
    evtchn_bind_interdomain_t op = {0};

    op.remote_dom  = srvr_id;
    op.remote_port = remote_prt_nmbr;
    // FAILS: err = -14
    err = HYPERVISOR_event_channel_op(EVTCHNOP_bind_interdomain, &op);
    if (err)
    {
        minios_printk("ERROR: bind_interdomain failed with return code =%d\n", err);
        MYASSERT( !"HYPERVISOR_event_channel_op" );
        goto ErrorExit;
    }
    
    g_state.event_channel_page = bmk_pgalloc_one();
    if (NULL == g_state.event_channel_page)
    {
        MYASSERT( !"Failed to alloc Event Channel page" );
        err = BMK_ENOMEM;
        goto ErrorExit;
    }

    DEBUG_PRINT("Event Channel page address: %p\n", g_state.event_channel_page);
    bmk_memset(g_state.event_channel_page, 0, PAGE_SIZE);	

    // Clear channel of events and unmask
    minios_clear_evtchn(op.local_port);
    minios_unmask_evtchn(op.local_port);

    // Bind the handler
    minios_bind_evtchn(op.local_port,
                       xe_comms_event_callback,
                       g_state.event_channel_page );

    g_state.local_event_port = op.local_port;

    DEBUG_PRINT("Local port for event channel: %u\n", g_state.local_event_port );

    err = xe_comms_write_int_to_key( LOCAL_PRT_PATH, g_state.local_event_port );
    if (err)
    {
        goto ErrorExit;
    }

ErrorExit:
    return err;
}
#endif

static int
xe_comms_bind_to_interdom_chn (domid_t srvr_id,
                               evtchn_port_t remote_prt_nmbr)
{
    int err = 0;
    // Don't use minios_evtchn_bind_interdomain; spurious events are
    // delivered to us when we do.
    
    g_state.event_channel_page = bmk_pgalloc_one();
    if (NULL == g_state.event_channel_page)
    {
        MYASSERT( !"Failed to alloc Event Channel page" );
        err = BMK_ENOMEM;
        goto ErrorExit;
    }

    DEBUG_PRINT("Event Channel page address: %p\n", g_state.event_channel_page);
    bmk_memset(g_state.event_channel_page, 0, PAGE_SIZE);	
 
    // Ports are bound in the masked state
    err = minios_evtchn_bind_interdomain( srvr_id,
                                          remote_prt_nmbr,
                                          xe_comms_event_callback,
                                          g_state.event_channel_page,
                                          &g_state.local_event_port );
    if (err)
    {
        MYASSERT(!"Could not bind to event channel\n");
        goto ErrorExit;
    }
    
    // Clear channel of events and unmask
//    minios_clear_evtchn( g_state.local_event_port );
//    minios_unmask_evtchn( g_state.local_event_port );

    DEBUG_PRINT("Local port for event channel: %u\n", g_state.local_event_port );

    err = xe_comms_write_int_to_key( LOCAL_PRT_PATH, g_state.local_event_port );
    if (err)
    {
        goto ErrorExit;
    }

ErrorExit:
    return err;
}


////////////////////////////////////////////////////
//
// xe_comms_init
//
// Initializes the channel to Xen. This means we wait on the remote
// (the "server") ID and grant reference to appear.
//
int
xe_comms_init( void )
{
    int rc = 0;
    domid_t                   localId = 0;
    domid_t                   remoteId = 0;

    char*                     err = NULL;
    char*                     msg = NULL;

    struct xenbus_event_queue events;
    grant_ref_t	              client_grant_ref = 0;
    evtchn_port_t             evt_chn_prt_nmbr = 0;

    bmk_memset( &g_state, 0, sizeof(g_state) );

    //
    // Init protocol
    // XXX: update ??
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
    xenbus_event_queue_init(&events);

    xenbus_watch_path_token(XBT_NIL, SERVER_ID_PATH, SERVER_ID_PATH, &events);
    while ( (rc = xe_comms_read_int_from_key( SERVER_ID_PATH, (int *) &remoteId ) ) != 0
            || (0 == remoteId) )
    {
        DEBUG_PRINT( "Waiting for server to come online\n" );
        bmk_memfree(msg, BMK_MEMWHO_WIREDBMK);
        bmk_memfree(err, BMK_MEMWHO_WIREDBMK);
        xenbus_wait_for_watch(&events);
    }

    xenbus_unwatch_path_token(XBT_NIL, SERVER_ID_PATH, SERVER_ID_PATH);
    bmk_memset( &events, 0, sizeof(events) );

    DEBUG_PRINT( "Discovered remote server ID: %u\n", remoteId );

    // Wait for the grant reference
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
    rc = xe_comms_read_int_from_key( GRANT_REF_PATH, &client_grant_ref );
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
    rc = xe_comms_read_int_from_key( EVT_CHN_PRT_PATH, &evt_chn_prt_nmbr );
    if ( rc )
    {
        goto ErrorExit;
    }

    rc = xe_comms_bind_to_interdom_chn( remoteId, evt_chn_prt_nmbr );
    if ( rc )
    {
        goto ErrorExit;
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

    // ????
    // minios_unbind_all_ports();
    
    if (g_state.event_channel_page)
    {
        bmk_memfree(g_state.event_channel_page, BMK_MEMWHO_WIREDBMK);
    }

    if (g_state.gntmap_map)
    {
        gntmap_fini( g_state.gntmap_map );
        bmk_memfree( g_state.gntmap_map, BMK_MEMWHO_WIREDBMK );
    }

    if (g_state.client_page)
    {
        bmk_memfree(g_state.client_page, BMK_MEMWHO_WIREDBMK);
    }

    bmk_memset( &g_state, 0, sizeof(g_state) );

    return rc;
}
