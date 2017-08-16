/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

//
// Module for communicating with Xen.
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

#include <bmk-core/string.h>
#include <bmk-core/printf.h>
#include <bmk-core/memalloc.h>
#include <bmk-core/pgalloc.h>

#include "xenevent_common.h"
#include "xenevent_comms.h"

#include "xenevent_minios.h"
#include "xenevent_netbsd.h"

#include "message_types.h"
#include "xen_keystore_defs.h"
#include "ins-ioctls.h"

// The RING macros use memset
#define memset bmk_memset

#define INS_XENSTORE_DIR_FMT "%s/%d"

#define INS_EVENT_CHANNEL_MEM_ORDER 0

// Defines:
// mwevent_sring_t
// mwevent_front_ring_t
// mwevent_back_ring_t
DEFINE_RING_TYPES( mwevent, mt_request_generic_t, mt_response_generic_t );


// Maintain all state here for future flexibility
typedef struct _xen_comm_state
{
    bool comms_established;

    domid_t         local_id;

    // Grant map 
    struct gntmap gntmap_map;

    // One grant ref is required per page
    grant_ref_t    grant_refs[ XENEVENT_GRANT_REF_COUNT ];

    // Ring buffer types. This side implements the "back end" of the
    // ring: requests are taken off the ring and responses are put on
    // it. The "sring" is the shared ring - it points to a contiguous
    // block of shared pages. The shared ring resides in shared memory.
    mwevent_sring_t     * shared_ring;
    mwevent_back_ring_t   back_ring;
    size_t                shared_ring_size;

    // Event channel memory
    void    * event_channel_mem;
    size_t    event_channel_mem_size;

    // Event channel local port
    evtchn_port_t local_event_port;

    // Semaphore that is signalled once for each message that has arrived
    xenevent_semaphore_t messages_available;
} xen_comm_state_t;

static xen_comm_state_t g_state;

/***************************************************************************
 * Basic utility functions
 ***************************************************************************/

int
xe_comms_write_str_to_key( const char * Path,
                           const char * Value )
{
    xenbus_transaction_t    txn;
    int                   retry;
    char                   *err;
    int                     res = 0;
    bool             started = false;

    // N.B. We must handle an empty string here

    DEBUG_PRINT( "Writing to xenstore: %s <= %s\n", Path, Value );

    err = xenbus_transaction_start( &txn );
    if ( err )
    {
        MYASSERT( !"xenbus_transaction_start" );
        goto ErrorExit;
    }

    started = true;

    err = xenbus_write( txn, Path, Value );
    if ( err )
    {
        MYASSERT( !"xenbus_transaction_start" );
        goto ErrorExit;
    } 

ErrorExit:
    if ( err ) 
    {
        // there was an error above, so report error to caller
        res = 1;
        DEBUG_PRINT( "Failure: %s\n", err );
        bmk_memfree( err, BMK_MEMWHO_WIREDBMK );
    }

    if ( started )
    {
        (void) xenbus_transaction_end( txn, 0, &retry );
    }

    return res;
}


static int
xe_comms_write_int_to_key( const char * Path,
                           const int    Value )
{
    int rc = 0;
    char buf[MAX_KEY_VAL_WIDTH];

    bmk_memset( buf, 0, sizeof(buf) );
    bmk_snprintf( buf, sizeof(buf), "%u", Value );

    rc = xe_comms_write_str_to_key( Path, buf );

    return rc;
}


int
xe_comms_publish_ip_addr( const char * Ip )
{
    int rc = 0;
    char path[ XENEVENT_PATH_STR_LEN ] = {0};

    bmk_snprintf( path, sizeof( path ),
                  INS_XENSTORE_DIR_FMT "/%s",
                  XENEVENT_XENSTORE_ROOT,
                  g_state.local_id,
                  INS_IP_ADDR_KEY );

    rc = xe_comms_write_str_to_key( path, Ip );

    return rc;
}


static int
xe_comms_read_str_from_key( IN const char * Path,
                            OUT char     ** OutVal )
{
    xenbus_transaction_t txn;
    int                retry;
    char                *err;
    int                  res = 0;
    bool             started = false;

    *OutVal = NULL;

    err = xenbus_transaction_start( &txn );
    if (err)
    {
        MYASSERT( !"xenbus_transaction_start" );
        goto ErrorExit;
    }

    started = true;

    err = xenbus_read( txn, Path, OutVal );
    if (err)
    {
        MYASSERT( !"xenbus_read" );
        goto ErrorExit;
    }

    DEBUG_PRINT( "Read from xenstore: %s => %s\n", Path, *OutVal );

ErrorExit:
    if ( err )
    {
        res = 1;
        DEBUG_PRINT( "Failure: %s\n", err );
        bmk_memfree( err, BMK_MEMWHO_WIREDBMK );
    }

    if ( started )
    {
        (void) xenbus_transaction_end( txn, 0, &retry );
    }

    return res;
}


static int
xe_comms_remove_directory( const char * Path )
{
    xenbus_transaction_t txn;
    int                retry;
    char                *err;
    int                  res = 0;
    bool             started = false;

    err = xenbus_transaction_start( &txn );
    if ( err )
    {
        MYASSERT( !"xenbus_transaction_start" );
        goto ErrorExit;
    }

    started = true;

    err = xenbus_rm( txn, Path );
    if ( err )
    {
        MYASSERT( !"xenbus_rm" );
        goto ErrorExit;
    }

ErrorExit:
    if ( err )
    {
        res = 1;
        DEBUG_PRINT( "Failure: %s\n", err );
        bmk_memfree( err, BMK_MEMWHO_WIREDBMK );
    }

    if ( started )
    {
        (void) xenbus_transaction_end( txn, 0, &retry );
    }

    return res;
}


int
xe_comms_read_int_from_key( IN const char * Path,
                            OUT int       * OutVal )
{
    char * val = NULL;
    int    res = 0;

    *OutVal = 0;

    res = xe_comms_read_str_from_key( Path, &val );
    if ( res )
    {
        goto ErrorExit;
    }

    *OutVal = bmk_strtoul( val, NULL, 10 );

ErrorExit:
    if ( val )
    {
        bmk_memfree( val, BMK_MEMWHO_WIREDBMK );
    }

    return res;
}


static int
xe_comms_wait_and_read_int_from_key( IN const char * Path,
                                     OUT int       * OutVal )
{
    int     rc = 0;
    bool printed = false;
    struct xenbus_event_queue events;

    bmk_memset( &events, 0, sizeof(events) );
    xenbus_event_queue_init( &events );

    xenbus_watch_path_token( XBT_NIL, Path, Path, &events );

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
        xenbus_wait_for_watch( &events );
    }

    xenbus_unwatch_path_token( XBT_NIL, Path, Path );

    return rc;
}


/***************************************************************************
 * Functions for item read/write via Xen ring buffer
 ***************************************************************************/


#if PVM_USES_EVENT_CHANNEL
static int
send_event( evtchn_port_t Port )
{

    int  err;

    DEBUG_PRINT( "Sending event to (local) port %d\n", Port );
    err = minios_notify_remote_via_evtchn( Port );

    if ( err )
    {
        MYASSERT( !"Failed to send event" );
    }

    return err;
}
#endif


#if (INS_USES_EVENT_CHANNEL || PVM_USES_EVENT_CHANNEL)
static void
xe_comms_event_callback( evtchn_port_t    Port,
                         struct pt_regs * Regs,
                         void           * Data )
{
    DEBUG_PRINT( "Event Channel %u\n", Port );

#if INS_USES_EVENT_CHANNEL
    //
    // Release xe_comms_read_item() to check for another item
    //
    xenevent_semaphore_up( g_state.messages_available );
#endif
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
xe_comms_read_item( void   * Memory,
                    size_t   Size,
                    size_t * BytesRead )
{
    int                         rc = 0;
    bool                 available = false;
    mt_request_generic_t * request = NULL;

    *BytesRead = 0;
    
    do
    {
        available = RING_HAS_UNCONSUMED_REQUESTS( &g_state.back_ring );

        if ( !available )
        {
            // Nothing was available. Block until either (a) event
            // arrives or (b) a timeout has expired, and try again.
#if INS_USES_EVENT_CHANNEL
            xenevent_semaphore_down( g_state.messages_available );
#else
            xenevent_kpause( "Poll ring buffer", true, 1, NULL );
#endif
            continue;
        }
    } while ( !available );

    // Consume the request
    request = (mt_request_generic_t *)
        RING_GET_REQUEST( &g_state.back_ring,
                          g_state.back_ring.req_cons );

    DEBUG_PRINT( "Processing item idx %x, ID=%lx size=%d\n",
                 g_state.back_ring.req_cons,
                 (unsigned long)request->base.id, request->base.size );
    
    if ( !MT_IS_REQUEST( request )
         || request->base.size > Size  )
    {
        rc = BMK_EINVAL;
        MYASSERT( !"Programming error: there's a problem with the request" );
        goto ErrorExit;
    }

    // Total size: header + payload sizes. Do a direct memory copy;
    // Rump has no division between user and kernel memory.
    *BytesRead = request->base.size;
    bmk_memcpy( Memory, request, *BytesRead );

ErrorExit:
    // Advance the local (private) counter for the next request
    ++g_state.back_ring.req_cons;

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
                     size_t Size,
                     size_t * BytesWritten )
{
    int rc = 0;
    mt_response_generic_t * response = (mt_response_generic_t *) Memory;

    // A response can only be placed in a slot that was used by a
    // request that has been consumed. An overflow here is
    // "impossible".

    // Get the buffer for the next response and populate it
    void * dest = RING_GET_RESPONSE( &g_state.back_ring,
                                     g_state.back_ring.rsp_prod_pvt );

    // Verify that we are clobbering a request with a response.
    MYASSERT( MT_IS_REQUEST( (mt_response_generic_t *)dest ) );
    MYASSERT( MT_IS_RESPONSE( response ) );

    *BytesWritten = response->base.size;
    MYASSERT( *BytesWritten <= Size );

    DEBUG_PRINT( "Writing item idx %d to %p\n",
                 g_state.back_ring.rsp_prod_pvt, dest );

    bmk_memcpy( dest, response, *BytesWritten );

    // We're producing a response
    ++g_state.back_ring.rsp_prod_pvt;
    RING_PUSH_RESPONSES( &g_state.back_ring );

#if PVM_USES_EVENT_CHANNEL
    (void) send_event( g_state.local_event_port );
#endif

    DEBUG_PRINT( "g_state.back_ring.rsp_prod_pvt: %u\n", g_state.back_ring.rsp_prod_pvt );

    return rc;
}


/*
 * Receive a set of grant references from the PVM. The set is written
 * to GRANT_REF_PATH like this:
 * "ref1 ref2 ... refN"
 *
 * Where the delimiter is XENEVENT_GRANT_REF_DELIM (" " above) and N
 * is XENEVENT_GRANT_REF_COUNT.
 */
static int
xe_comms_rcv_grant_refs( domid_t RemoteId )
{
    struct xenbus_event_queue events;
    int rc = 0;
    char * err = NULL;
    char * msg = NULL;
    char * msgptr = NULL;
    char * refstr = NULL;
    char path[ XENEVENT_PATH_STR_LEN ] = {0};

    xenbus_event_queue_init(&events);

    bmk_snprintf( path,
                  sizeof( path ),
                  INS_XENSTORE_DIR_FMT "/%s",
                  XENEVENT_XENSTORE_ROOT,
                  g_state.local_id,
                  GNT_REF_KEY ); 

    xenbus_watch_path_token(XBT_NIL, path, XENEVENT_NO_NODE, &events);
    while ( (err = xenbus_read( XBT_NIL, path, &msg ) ) != NULL
            ||  msg[0] == '0')
    {
        bmk_memfree(msg, BMK_MEMWHO_WIREDBMK);
        bmk_memfree(err, BMK_MEMWHO_WIREDBMK);
        xenbus_wait_for_watch(&events);
    }

    xenbus_unwatch_path_token( XBT_NIL, path, XENEVENT_NO_NODE );

    DEBUG_PRINT( "Parsing grant references in %s\n", path );

    rc = xe_comms_read_str_from_key( path, &refstr );
    if ( rc )
    {
        goto ErrorExit;
    }

    // Extract the grant references from XenStore - they are space-delimited hex values
    msgptr = msg;
    for ( int i = 0; i < XENEVENT_GRANT_REF_COUNT; i++ )
    {
        char * next = NULL;
        g_state.grant_refs[i] = bmk_strtoul( msgptr, &next, 16 );
        if ( *next != XENEVENT_GRANT_REF_DELIM [0] )
        {
            rc = BMK_EINVAL;
            MYASSERT( !"Invalid data in grant ref path" );
            goto ErrorExit;
        }

        DEBUG_PRINT( "Found grant reference 0x%x\n", g_state.grant_refs[i] );
        msgptr = next;         // Advance msgptr to next token
    }

    gntmap_init( &g_state.gntmap_map );

    // Map in the memory described by the grant ref(s) into a contiguous region
    g_state.shared_ring = (mwevent_sring_t *)
        gntmap_map_grant_refs( &g_state.gntmap_map,
                               XENEVENT_GRANT_REF_COUNT,  // number of grant refs
                               (uint32_t *) &RemoteId,    // dom ID (only 1 so stride=0)
                               0,
                               (grant_ref_t *) g_state.grant_refs,
                               1 ); // region is writable
    if ( NULL == g_state.shared_ring )
    {
        MYASSERT( !"gntmap_map_grant_refs failed" );
        rc = BMK_ENOMEM;
        goto ErrorExit;
    }

    g_state.shared_ring_size = XENEVENT_GRANT_REF_COUNT * PAGE_SIZE;

ErrorExit:
    if ( refstr )
    {
        bmk_memfree( refstr, BMK_MEMWHO_WIREDBMK );
    }

    return rc;
}


static int
xe_comms_bind_to_interdom_chn (domid_t Srvr_Id,
                               evtchn_port_t Remote_Prt_Nmbr )
{
    int err = 0;
    char path[ XENEVENT_PATH_STR_LEN ] = {0};
    // Don't use minios_evtchn_bind_interdomain; spurious events are
    // delivered to us when we do.

    g_state.event_channel_mem = bmk_pgalloc( INS_EVENT_CHANNEL_MEM_ORDER );
    if (NULL == g_state.event_channel_mem)
    {
        MYASSERT( !"Failed to alloc Event Channel page" );
        err = BMK_ENOMEM;
        goto ErrorExit;
    }

    DEBUG_PRINT("Event Channel page address: %p\n", g_state.event_channel_mem);
    bmk_memset(g_state.event_channel_mem, 0, PAGE_SIZE);	

#if (INS_USES_EVENT_CHANNEL || PVM_USES_EVENT_CHANNEL)
    // Ports are bound in the masked state
    err = minios_evtchn_bind_interdomain( Srvr_Id,
                                          Remote_Prt_Nmbr,
                                          xe_comms_event_callback,
                                          g_state.event_channel_mem,
                                          &g_state.local_event_port );
    if ( err )
    {
        MYASSERT(!"Could not bind to event channel\n");
        goto ErrorExit;
    }

    // Clear channel of events and unmask
    minios_clear_evtchn( g_state.local_event_port );
    minios_unmask_evtchn( g_state.local_event_port );
#endif

    // Indicate that the VM's event channel is bound
    bmk_snprintf( path, sizeof( path ),
                  INS_XENSTORE_DIR_FMT "/%s",
                  XENEVENT_XENSTORE_ROOT,
                  g_state.local_id,
                  VM_EVT_CHN_BOUND_KEY );

    err = xe_comms_write_int_to_key( path, 1 );
    if ( err )
    {
        goto ErrorExit;
    }

ErrorExit:
    return err;
}


int
xe_comms_get_sock_params( OUT char * SockParams )
{
    int rc = 0;
    static bool init = false;
    static char sock_path[XENEVENT_PATH_STR_LEN] = {0};
    char * params = NULL;

    if ( !init )
    {
        // Just create these the first time
        init = true;
        bmk_snprintf( sock_path, sizeof( sock_path ),
                      INS_XENSTORE_DIR_FMT "/%s",
                      XENEVENT_XENSTORE_ROOT,
                      g_state.local_id,
                      INS_SOCKET_PARAMS_KEY );
    }

    memset( SockParams, 0, INS_SOCK_PARAMS_MAX_LEN );

    rc = xe_comms_read_str_from_key( sock_path, &params );
    if ( rc )
    {
        goto ErrorExit;
    }

    bmk_strncpy( SockParams, params, INS_SOCK_PARAMS_MAX_LEN-1 );

ErrorExit:
    if ( params )
    {
        bmk_memfree( params, BMK_MEMWHO_WIREDBMK );
    }
    return rc;
}


int
xe_comms_heartbeat( const char * NetworkStats )
{
    static int heartbeat = 0;
    static char heartbeat_path[XENEVENT_PATH_STR_LEN] = {0};
    static char stats_path[XENEVENT_PATH_STR_LEN] = {0};
    int err = 0;

    if ( 0 == heartbeat )
    {
        // Just create these the first time
        bmk_snprintf( heartbeat_path, sizeof( heartbeat_path ),
                      INS_XENSTORE_DIR_FMT "/%s",
                      XENEVENT_XENSTORE_ROOT,
                      g_state.local_id,
                      INS_HEARTBEAT_KEY );

        bmk_snprintf( stats_path, sizeof( stats_path ),
                      INS_XENSTORE_DIR_FMT "/%s",
                      XENEVENT_XENSTORE_ROOT,
                      g_state.local_id,
                      INS_NETWORK_STATS_KEY );
    }

    err = xe_comms_write_int_to_key( heartbeat_path, heartbeat );
    if ( 0 != err )
    {
        DEBUG_PRINT( "Failed to write updated heartbeat to xenstore" );
        goto ErrorExit;
    }

    ++heartbeat;

    MYASSERT( bmk_strlen(NetworkStats) > 0 );
    err = xe_comms_write_str_to_key( stats_path, NetworkStats );
    if ( err )
    {
        DEBUG_PRINT( "Failed to write network stats to xenstore" );
        goto ErrorExit;
    }

ErrorExit:
    return err;
}


int
xe_comms_listeners( const char * Listeners )
{
    static char listener_path[XENEVENT_PATH_STR_LEN] = {0};
    int err = 0;

    // Just create path the first time
    if ( '\0' == listener_path[0] )
    {
        bmk_snprintf( listener_path, sizeof( listener_path ),
                      INS_XENSTORE_DIR_FMT "/%s",
                      XENEVENT_XENSTORE_ROOT,
                      g_state.local_id,
                      INS_LISTENER_KEY );
    }

    err = xe_comms_write_str_to_key( listener_path, Listeners );
    if ( err )
    {
        DEBUG_PRINT( "Failed to write listener info to xenstore" );
        goto ErrorExit;
    }

ErrorExit:
    return err;
}


int
xe_comms_get_domid( void )
{
    int rc = 0;
    int ret = 0;

    rc = xe_comms_read_int_from_key( PRIVATE_ID_PATH, &ret );
    if ( rc )
    {
        ret = rc;
    }

    return ret;
}


/**
 * @brief Initializes the comms subsystem upon driver load
 */
int
xe_comms_init( void )
{
    int rc = 0;

    rc = xenevent_semaphore_init( &g_state.messages_available );
    if ( 0 != rc )
    {
        goto ErrorExit;
    }

    // Read our own dom ID
    rc = xe_comms_read_int_from_key( PRIVATE_ID_PATH, (int *) &g_state.local_id );
    if ( rc )
    {
        goto ErrorExit;
    }

ErrorExit:
    return rc;
}


/**
 * @brief Registers this INS instance with the PVM driver.
 *
 * This must not be invoked until the user-mode component has loaded.
 * Initializes the channel to Xen. This means we wait on the remote
 * (the "server") ID and grant reference to appear.
 */
int
xe_comms_register( void )
{
    int             rc = 0;
    domid_t         remoteId = 0;
    char            path[ MAX_KEY_VAL_WIDTH ] = {0};
    evtchn_port_t   vm_evt_chn_prt_nmbr = 0;

    //
    // Begin init protocol
    //

    MYASSERT( g_state.local_id > 0 );

    // Write our dom ID to agreed-upon path
    bmk_snprintf( path, 
                  sizeof( path ),
                  INS_XENSTORE_DIR_FMT "/%s",
                  XENEVENT_XENSTORE_ROOT,
                  g_state.local_id,
                  INS_ID_KEY );
    rc = xe_comms_write_int_to_key( path, g_state.local_id );
    if ( rc )
    {
        goto ErrorExit;
    }

    memset( &path, 0, XENEVENT_PATH_STR_LEN );

    // Wait for the server ID.
    rc = xe_comms_wait_and_read_int_from_key( SERVER_ID_PATH,
                                              (int *) &remoteId );
    if ( rc )
    {
        goto ErrorExit;
    }
    DEBUG_PRINT( "Discovered remote server ID: %u\n", remoteId );

    //
    // Wait for the grant references and map them in
    //
    rc = xe_comms_rcv_grant_refs( remoteId );
    if ( rc )
    {
        goto ErrorExit;
    }

    // Get the event port and bind to it
    bmk_snprintf( path,
                  sizeof( path ),
                  INS_XENSTORE_DIR_FMT "/%s",
                  XENEVENT_XENSTORE_ROOT,
                  g_state.local_id,
                  VM_EVT_CHN_PORT_KEY );

    rc = xe_comms_wait_and_read_int_from_key( path,
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
                    g_state.shared_ring_size );

ErrorExit:
    return rc;
}


int
xe_comms_fini( void )
{
    int rc = 0;
    char path[ XENEVENT_PATH_STR_LEN ] = {0};

    DEBUG_PRINT("Unbinding from Local Interdomain Event Channel Port: %u ...\n",
                g_state.local_event_port);

    minios_mask_evtchn( g_state.local_event_port );
    minios_clear_evtchn( g_state.local_event_port );

    minios_unbind_evtchn(g_state.local_event_port);

    xenevent_semaphore_destroy( &g_state.messages_available );

    if ( g_state.event_channel_mem )
    {
        bmk_pgfree( g_state.event_channel_mem, INS_EVENT_CHANNEL_MEM_ORDER );
    }

    gntmap_fini( &g_state.gntmap_map );

    // Cleanup entries in xenstore
    bmk_snprintf( path,
                  sizeof( path ),
                  INS_XENSTORE_DIR_FMT,
                  XENEVENT_XENSTORE_ROOT,
                  g_state.local_id );
    xe_comms_remove_directory( path );

    DEBUG_PRINT( "Cleaning up xenstore path: %s\n", path );

    // XXXX g_state.shared_ring was allocated with allocate_ondemand()
    // in gntmap_map_grant_refs(). Is this the right way to free it?

    // XXXX currently the build system doesn't let us link to the
    //symbol unmap_frames. Investigate this.

    // This is the case even if the Makefile includes this line:
    // RUMP_SYM_NORENAME = xenbus_|HYPERVISOR_|minios_|bmk_|gntmap|gnttab|_minios|_text|unmap_frames
    //rc = unmap_frames( (unsigned long) g_state.shared_ring,
    //                     XENEVENT_GRANT_REF_COUNT );
    
    // bmk_memset( &g_state, 0, sizeof(g_state) );

    return rc;
}
