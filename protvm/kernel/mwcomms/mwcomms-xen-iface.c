/*************************************************************************
 * STAR LAB PROPRIETARY & CONFIDENTIAL
 * Copyright (C) 2018, Star Lab — All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited.
 ***************************************************************************/

/**
 * This file handles the initial handshake between the PVM and the
 * INS, meaning that it monitors and publishes to certain paths in
 * XenStore and manages the sharing of memory pages for the ring
 * buffer.
 *
 * The XenStore keys that are managed are defined in xen_keystore_defs.h. They are
 *
 * SERVER_ID_KEY
 * INS_ID_KEY
 * GNT_REF_KEY
 * VM_EVT_CHN_PORT_KEY
 * VM_EVT_CHN_BOUND_KEY
 *
 * The sequence of events is:
 *
 * 1. Write the current domU's domid to SERVER_ID_KEY
 * 2. Wait for the client domU's domid to appear in INS_ID_KEY
 * 3. Create an unbound event channel and write its port to VM_EVT_CHN_PORT_KEY
 * 4. Allocate memory and offer the client grants to it (1/page)
 * 5. Reset the value in INS_ID_KEY
 * 6. Watch for VM_EVT_CHN_BOUND_KEY to be populated by the client
 * 7. Write the grant refs to GNT_REF_KEY
 * 8. Invoke the callback given in mw_xen_init.
 *
 *
 * Also supplies support for reaping dead INSes. However, this code
 * does not provide the thread for the reaper.
 */

#include "mwcomms-common.h"

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>

#include <asm/uaccess.h>
#include <linux/time.h>
#include <asm/atomic.h>

#include <linux/list.h>

#include <xen/grant_table.h>
#include <xen/page.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/interface/callback.h>
#include <xen/interface/io/ring.h>

#include "mwcomms-xen-iface.h"

// How long to wait if we want to write a request but the ring is full?
#define RING_FULL_TIMEOUT (HZ >> 6)

// Defines:
// union mwevent_sring_entry
// struct mwevent_sring_t
// struct mwevent_front_ring_t
// struct mwevent_back_ring_t
DEFINE_RING_TYPES( mwevent, mt_request_generic_t, mt_response_generic_t );

// Per-INS data
typedef struct _mwcomms_ins_data
{
    atomic64_t               in_use;
    domid_t                  domid;

    // Ring memory descriptions
    // Is there a reason to keep a ring and an sring
    // reference anymore?
    mw_region_t               ring;
    struct mwevent_sring    * sring;
    struct mwevent_front_ring front_ring;

    bool                      is_ring_ready;

    //Grant refs shared with this INS
    grant_ref_t               grant_refs[ XENEVENT_GRANT_REF_COUNT ];

    int                       common_evtchn;
    int                       irq;

    // Time last seen, in jiffies
    unsigned long             last_seen_time;

    // count: how many heartbeats have been missed?
    int                       missed_heartbeats;

    // Only one request slot is revealed at a time. It is reserved for
    // the client's usage: it is acquired in
    // mw_xen_get_next_request_slot() and released in
    // mw_xen_dispatch_request().

    // The current (outstanding) request for this INS.
    mt_request_generic_t    * curr_req;

    bool                      locked;

} mwcomms_ins_data_t;


typedef struct _mwcomms_xen_globals
{
    domid_t          my_domid;

    bool             xenbus_watch_active;
    bool             pending_exit;
    atomic64_t       ins_count;

    struct semaphore event_channel_sem;

    mw_xen_init_complete_cb_t * completion_cb;
    mw_xen_event_handler_cb_t * event_cb;

    mwcomms_ins_data_t ins[ MAX_INS_COUNT ];

    struct mutex ring_lock;
    int new_ins_delay;

} mwcomms_xen_globals_t;

static mwcomms_xen_globals_t g_mwxen_state = {0};


static int
mw_xen_rm( const char * Dir, const char * Node )
{
    struct xenbus_transaction   txn;
    int                         err;
    bool                        txnstarted = false;
    int                         term = 0;

    MYASSERT( Dir );
    MYASSERT( Node );

    err = xenbus_transaction_start( &txn );
    if ( err )
    {
        pr_err( "Error removing dir:%s node:%s\n", Dir, Node );
        goto ErrorExit;
    }

    txnstarted = true;

    if ( !xenbus_exists( txn, Dir, Node ) )
    {
       goto ErrorExit;
    }

    err = xenbus_rm( txn, Dir, Node );
    if ( err )
    {
        goto ErrorExit;
    }

ErrorExit:

    if ( txnstarted )
    {
        if ( xenbus_transaction_end( txn, term ) )
        {
            pr_err( "Failed to end transaction makedir dir:%s node:%s\n", Dir, Node );
        }
    }
    return err;
}

static int
mw_xen_mkdir( const char * Dir, const char * Node )
{
    struct xenbus_transaction   txn;
    int                         err;
    bool                        txnstarted = false;
    int                         term = 0;

    MYASSERT( Dir );
    MYASSERT( Node );

    pr_debug( "Making dir %s/%s\n", Dir, Node );

    err = xenbus_transaction_start( &txn );
    if ( err )
    {
        pr_err( "Error starting xenbus transaction\n" );
        goto ErrorExit;
    }

    txnstarted = true;

    if ( xenbus_exists( txn, Dir, Node ) )
    {
        pr_debug( "dir:%s node:%s exists; removing it.\n", Dir, Node );
        err = xenbus_rm( txn, Dir, Node);
        if ( err )
        {
            pr_err( "Could not delete existing xenstore dir:%s node:%s\n", Node, Dir );
            goto ErrorExit;
        }
        pr_debug( "%s: removed\n", Dir );
    }

    pr_debug( "Creating dir:%s node:%s\n", Dir, Node );
    err = xenbus_mkdir( txn, Dir, Node );
    if ( err )
    {
        pr_err( "Could not create node:%s in dir:%s\n", Node, Dir );
        goto ErrorExit;
    }

ErrorExit:
    if ( txnstarted )
    {
        if ( xenbus_transaction_end( txn, term ) )
        {
            pr_err( "Failed to end transaction makedir dir:%s node:%s\n", Dir, Node );
        }
    }

    pr_debug( "Complete - dir:%s node:%s\n", Dir, Node );

    return err;
}


int
mw_xen_write_to_key( const char * Dir, const char * Node, const char * Value )
{
   struct xenbus_transaction txn;
   int                       err;
   int                      term = 0;
   bool               txnstarted = false;

   MYASSERT( Dir   );
   MYASSERT( Node  );
   MYASSERT( Value );

   pr_debug( "Writing %s to %s/%s\n", Value, Dir, Node );

   err = xenbus_transaction_start(&txn);
   if ( err )
   {
       pr_err( "Error starting xenbus transaction\n" );
       goto ErrorExit;
   }

   txnstarted = true;

   err = xenbus_exists( txn, Dir, XENEVENT_NO_NODE );
   // 1 ==> exists
   if ( !err )
   {
       pr_err( "Xenstore directory %s does not exist.\n", Dir );
       err = -EIO;
       term = 1;
       goto ErrorExit;
   }

   err = xenbus_write( txn, Dir, Node, Value );
   if ( err )
   {
       pr_err( "Could not write to XenStore Key dir:%s node:%s\n", Dir, Node );
      goto ErrorExit;
   }

ErrorExit:
   if ( txnstarted )
   {
       if ( xenbus_transaction_end( txn, term ) )
       {
           pr_err( "Failed to end transaction: %s/%s = %s\n", Dir, Node, Value );
       }
   }

   return err;
}


static char *
mw_xen_read_from_key( const char * Dir, const char * Node )
{
    struct xenbus_transaction   txn = {0};
    char                       *str = NULL;
    int                         err = 0;

    pr_debug( "Reading value in dir:%s node:%s\n", Dir, Node );

    err = xenbus_transaction_start(&txn);
    if (err)
    {
        pr_err( "Error starting xenbus transaction\n" );
        return NULL;
    }

    str = (char *)xenbus_read(txn, Dir, Node, NULL);
    if (XENBUS_IS_ERR_READ(str))
    {
        pr_err( "Could not read XenStore Key: %s/%s\n", Dir, Node );
        xenbus_transaction_end(txn,1);
        return NULL;
    }

    err = xenbus_transaction_end(txn, 0);

    return str;
}


static int
mw_xen_write_server_id_to_key( void )
{
   const char *dom_id_str = NULL;
   int err = 0;
   
   // Get my domain id
   dom_id_str = (const char *)
       mw_xen_read_from_key( PRIVATE_ID_PATH, XENEVENT_NO_NODE );

   if ( NULL == dom_id_str )
   {
       pr_err( "Error: Failed to read my Dom Id Key\n" );
       err = -EIO;
       goto ErrorExit;
   }

   pr_debug( "Running within Xen domid=%s\n", dom_id_str);

   err = mw_xen_write_to_key( XENEVENT_XENSTORE_PVM, SERVER_ID_KEY, dom_id_str );
   if ( err )
   {
       goto ErrorExit;
   }

   g_mwxen_state.my_domid = simple_strtol(dom_id_str, NULL, 10);

ErrorExit:
   if ( NULL != dom_id_str )
   {
       kfree( dom_id_str );
   }
   return err;
}


static int
MWSOCKET_DEBUG_ATTRIB
mw_xen_create_unbound_evt_chn( mwcomms_ins_data_t *Ins  )
{
   struct evtchn_alloc_unbound alloc_unbound; 
   char                        str[ MAX_GNT_REF_WIDTH ] = {0};
   char                        path[ XENEVENT_PATH_STR_LEN ] = {0};
   int                         err = 0;

   MYASSERT( Ins );

   if ( 0 >= Ins->domid  )
   {
       pr_err("Invalid domid passed to mw_xen_create_unbound_evt_chn\n");
       goto ErrorExit;
   }

   alloc_unbound.dom        = g_mwxen_state.my_domid;
   alloc_unbound.remote_dom = Ins->domid;

   err = HYPERVISOR_event_channel_op( EVTCHNOP_alloc_unbound, &alloc_unbound );
   if ( err )
   {
       pr_err("Failed to set up event channel\n");
       goto ErrorExit;
   }

   Ins->common_evtchn = alloc_unbound.port;

   pr_debug( "Event Channel Port (%d <=> %d): %d\n",
             alloc_unbound.dom, Ins->domid,
             Ins->common_evtchn );

   snprintf( str, MAX_GNT_REF_WIDTH,
             "%u", Ins->common_evtchn );

   snprintf( path, sizeof(path), "%s/%d",
             XENEVENT_XENSTORE_ROOT, Ins->domid );

   err = mw_xen_write_to_key( path, VM_EVT_CHN_PORT_KEY, str );
   if ( err ) { goto ErrorExit; }

ErrorExit:
   return err;
}


static void
mw_xen_ins_alive( mwcomms_ins_data_t * Ins )
{
    MYASSERT( Ins );
    MYASSERT( 1 == atomic64_read( &Ins->in_use ) );

    pr_debug( "Recognized heartbeat for INS %d\n", Ins->domid );

    Ins->last_seen_time = jiffies;
    Ins->missed_heartbeats = 0;
}


/**
 * @brief Notifies the mwsocket system that an item is available on an
 * unspecified INS.
 */
static irqreturn_t
mw_xen_irq_event_handler( int Port, void * Data )
{
    pr_verbose( "Event arrived ==> INS data available, Port=%d\n", Port );
    g_mwxen_state.event_cb();
    return IRQ_HANDLED;
}


static int
//MWSOCKET_DEBUG_ATTRIB
mw_xen_send_event( mwcomms_ins_data_t * Ins )
{
    MYASSERT( Ins );

    int rc = 0;
    struct evtchn_send send = { .port = Ins->common_evtchn };

    rc = HYPERVISOR_event_channel_op( EVTCHNOP_send, &send );
    if ( rc )
    {
        pr_err( "Failed to send event\n" );
    }

    return rc;
}


static bool
mw_xen_is_evt_chn_closed( mwcomms_ins_data_t * Ins )
{

    MYASSERT( Ins );
    int                  rc;
    struct evtchn_status status = { .dom = DOMID_SELF,
                                    .port = Ins->common_evtchn };

    rc = HYPERVISOR_event_channel_op( EVTCHNOP_status, &status );
    if ( rc < 0 )
    {
        return true;
    }

    if ( status.status != EVTCHNSTAT_closed )
    {
        return false;
    }

    return true;
}


static void
mw_xen_free_unbound_evt_chn( mwcomms_ins_data_t * Ins )
{
    struct evtchn_close close = { 0 };
    int err = 0;

    if ( !Ins->common_evtchn )
    {
        goto ErrorExit;
    }

    close.port = Ins->common_evtchn;

    err = HYPERVISOR_event_channel_op(EVTCHNOP_close, &close);

    if ( err )
    {
       pr_err( "Failed to close event channel\n" );
    }
    else
    {
       pr_debug( "Closed event channel port=%d\n", close.port );
    }

ErrorExit:
    return;
}


static int
MWSOCKET_DEBUG_ATTRIB
mw_xen_offer_grant( mwcomms_ins_data_t * Ins )
{
   int rc = 0;

   // Calc the VA, then find the backing pseudo-physical address (Xen
   // book pg 61). The pages were allocated via GFP() which returns a
   // physically-contiguous block of pages.

   unsigned long mfn = virt_to_mfn( Ins->ring.ptr );
   
   for ( int i = 0; i < Ins->ring.pagect; ++i )
   {
       rc = gnttab_grant_foreign_access( Ins->domid,
                                         mfn + i,
                                         0 ); // not RO
       if ( rc < 0 )
       {
           pr_err( "gnttab_grant_foreign_access failed: %d\n", rc );
           goto ErrorExit;
       }

       Ins->grant_refs[ i ] = rc;
       pr_verbose( "VA: %p MFN: %p grant 0x%x\n",
                 (void *) ((unsigned long)Ins->ring.ptr +
                           i * PAGE_SIZE),
                 (void *)(mfn+i),
                 rc );
   }

   // Success
   rc = 0;

ErrorExit:
   return rc;
}


static int
mw_xen_write_grant_refs_to_key( mwcomms_ins_data_t * Ins )
{
    int rc = 0;

    // Must be large enough for one grant ref, in hex, plus '\0'
    // Make space for 12345678\0
    char one_ref[ 8 + 1 ];
    char path[ XENEVENT_PATH_STR_LEN ] = {0};
    
    size_t gnt_refs_sz = XENEVENT_GRANT_REF_COUNT * sizeof(one_ref);

    char * gnt_refs = kmalloc( gnt_refs_sz, GFP_KERNEL );
    if ( NULL == gnt_refs )
    {
        MYASSERT( "!kmalloc" );
        rc = -ENOMEM;
        goto ErrorExit;
    }

    gnt_refs[0] = '\0';

    for ( int i = 0; i < XENEVENT_GRANT_REF_COUNT; i++ )
    {
        if ( snprintf( one_ref, sizeof(one_ref),
                       "%x ", Ins->grant_refs[i] ) >= sizeof(one_ref))
        {
            pr_err( "Insufficient space to write grant ref.\n" );
            rc = -E2BIG;
            goto ErrorExit;
        }

        if ( strlen( gnt_refs ) >= gnt_refs_sz - sizeof(one_ref) )
        {
            pr_err( "Insufficient space to write all grant refs.\n" );
            rc = -E2BIG;
            goto ErrorExit;
        }

        (void) strncat( gnt_refs, one_ref, gnt_refs_sz );
    }

    snprintf( path, sizeof(path), "%s/%d",
              XENEVENT_XENSTORE_ROOT, Ins->domid );

    rc = mw_xen_write_to_key( path, GNT_REF_KEY, gnt_refs );

ErrorExit:
    if ( NULL != gnt_refs )
    {
        kfree( gnt_refs );
    }
    return rc;
}


static int
//MWSOCKET_DEBUG_ATTRIB // XXXX: don't uncomment directive
mw_xen_get_ins_from_xs_path( IN  const char         *  Path,
                             OUT mwcomms_ins_data_t ** Ins )
{
    int rc = -EINVAL;
    int index = 0;
    int domid = 0;
    char * copy = NULL;

    copy = kmalloc( XENEVENT_PATH_STR_LEN, GFP_KERNEL | __GFP_ZERO );
    if ( NULL == copy )
    {
        rc = -ENOMEM;
        MYASSERT( !"kmalloc" );
        goto ErrorExit;
    }

    strncpy( copy, Path, XENEVENT_PATH_STR_LEN );

    // This is required because kstrtoint stops at null terminator
    strreplace( copy, '/', '\0' );

    index = strlen( XENEVENT_XENSTORE_ROOT ) + 1;

    rc = kstrtoint( &copy[index], 10, &domid );
    if ( rc )
    {
        pr_err( "Could not get domid from path %s\n", Path );
        goto ErrorExit;
    }

    // Set to error in case INS not found
    rc = -ENOENT;

    for ( int i = 0; i < MAX_INS_COUNT; i++ )
    {
        if ( g_mwxen_state.ins[i].domid == domid )
        {
            rc = 0;
            *Ins = &g_mwxen_state.ins[i];
            break;
        }
    }

    if ( rc )
    {
        pr_err( "No backing INS entry found for XenStore path %s\n", Path );
    }

ErrorExit:
    CHECK_FREE( copy );
    return rc;
}


static int
MWSOCKET_DEBUG_ATTRIB
mw_xen_bind_evt_chn( const char * Path )
{
    char * is_bound_str = NULL;
    mwcomms_ins_data_t * ins = NULL;
    int rc = 0;

    rc = mw_xen_get_ins_from_xs_path( Path, &ins );
    if ( rc ) { goto ErrorExit; }

    mw_xen_ins_alive( ins );

    is_bound_str = (char *) mw_xen_read_from_key( Path, 
                                                  XENEVENT_NO_NODE );
    if ( !is_bound_str )
    {
        rc = -EIO;
        goto ErrorExit;
    }

    if ( 0 == strcmp( is_bound_str, "0" ) )
    {
        rc = -EIO;
        goto ErrorExit;
    }

    rc =  bind_evtchn_to_irqhandler( ins->common_evtchn,
                                     mw_xen_irq_event_handler,
                                     0, NULL, NULL );
    if ( rc <= 0 )
    {
        pr_err( "bind_evtchn_to_irqhandler failed\n" );
        goto ErrorExit;
    }

    pr_debug( "The remote event channel is bound\n" );

    // Success
    ins->irq = rc;
    rc = 0;

    pr_debug( "Bound event channel %d to irq: %d\n",
              ins->common_evtchn, ins->irq );

ErrorExit:
    CHECK_FREE( is_bound_str );
    return rc;
}


static int
MWSOCKET_DEBUG_ATTRIB
mw_xen_ins_heartbeat( const char * Path )
{
    int rc = 0;
    mwcomms_ins_data_t * ins = NULL;

    rc = mw_xen_get_ins_from_xs_path( Path, &ins );
    if ( rc )
    {
        goto ErrorExit;
    }

    // Heartbeat must come from active INS
    if ( 1 != atomic64_read( &ins->in_use ) )
    {
        MYASSERT( !"Heartbeat from INS that is not in use" );
        rc = -EINVAL;
        goto ErrorExit;
    }

    mw_xen_ins_alive( ins );

ErrorExit:
    return rc;
}


int
MWSOCKET_DEBUG_ATTRIB
mw_xen_init_ring( mwcomms_ins_data_t * Ins )
{
    int rc = 0;

    SHARED_RING_INIT( Ins->sring );

    FRONT_RING_INIT( &Ins->front_ring,
                     Ins->sring,
                     Ins->ring.pagect * PAGE_SIZE );

    return rc;
}


static int
MWSOCKET_DEBUG_ATTRIB
mw_xen_init_ring_block( IN mwcomms_ins_data_t * Ins )
{
    int rc = 0;

    // Get shared memory in an entire zeroed block
    Ins->ring.ptr = (void *)
        __get_free_pages( GFP_KERNEL | __GFP_ZERO,
                          XENEVENT_GRANT_REF_ORDER );
    if ( NULL == Ins->ring.ptr )
    {
        pr_err( "Failed to allocate 0x%x pages for domid %d\n",
                XENEVENT_GRANT_REF_COUNT, Ins->domid );
        rc = -ENOMEM;
    }

    Ins->sring = (struct mwevent_sring *) Ins->ring.ptr;
    Ins->ring.pagect = XENEVENT_GRANT_REF_COUNT;

    return rc;
}


static int
MWSOCKET_DEBUG_ATTRIB
mw_xen_ins_found( IN const char * Path )
{
    char               *client_id_str = NULL;
    int                 err = -ENXIO;
    mwcomms_ins_data_t *curr = NULL;

    client_id_str = (char *) mw_xen_read_from_key( Path, XENEVENT_NO_NODE );
    if ( !client_id_str )
    {
        err = -EIO;
        pr_err( "Error reading client id key!!!\n" );
        goto ErrorExit;
    }

    if ( strcmp( client_id_str, "0" ) == 0 )
    {
        err = -EIO;
        goto ErrorExit;
    }

    //
    // Get the client ID
    //
    for ( int i = 0; i < MAX_INS_COUNT; i++ )
    {
        // atomic64_cmpexchng returns the original value
        // of the atomic64_t
        if( 0 == atomic64_cmpxchg( &g_mwxen_state.ins[i].in_use, 0, 1 ) )
        {
            g_mwxen_state.ins[i].domid = simple_strtol( client_id_str, NULL, 10 );
            curr = &g_mwxen_state.ins[i];
            pr_debug( "Discovered client, domid %d added to ins_data[ %d ]\n",
                      g_mwxen_state.ins[i].domid, i );
            err = 0;
            break;
        }
    }

    if ( err )
    {
        pr_err("No available INS slots for new INS instance");
        goto ErrorExit;
    }

    err = mw_xen_init_ring_block( curr );
    if ( err ) { goto ErrorExit; }

    // Create unbound event channel with client
    err = mw_xen_create_unbound_evt_chn( curr );
    if ( err ) { goto ErrorExit; }

    // Offer Grant to Client
    err = mw_xen_offer_grant( curr );
    if( err ) { goto ErrorExit; }

    // Allocate shared mem for new INS
    err = mw_xen_init_ring( curr );
    if ( err ) { goto ErrorExit; }

    // Write Grant Ref to key 
    err = mw_xen_write_grant_refs_to_key( curr );
    if ( err ) { goto ErrorExit; }

    //
    // Complete: the handshake is done
    //
    mw_xen_ins_alive( curr );

    curr->locked = false;

    curr->is_ring_ready = true;

    pr_info( "INS %d is ready\n", curr->domid );

    atomic64_inc( &g_mwxen_state.ins_count );

    // Slow down the first couple of transactions
    // when secondary INS instances start up, otherwise
    // mwcomms runs into a timeout on request responses.
    if ( atomic64_read ( &g_mwxen_state.ins_count ) > 1 ) {
        g_mwxen_state.new_ins_delay = 5;
    } else {
        g_mwxen_state.new_ins_delay = 0;
    }

    g_mwxen_state.completion_cb( curr->domid );

ErrorExit:
    if ( NULL != client_id_str )
    {
        kfree(client_id_str);
    }
    return err;
}


bool
MWSOCKET_DEBUG_ATTRIB
mw_xen_iface_ready( void )
{
    bool ready = false;
    if ( atomic64_read ( &g_mwxen_state.ins_count ) > 0 )
    {
        ready = true;
    }

    return ready;
}


static int
mw_xen_get_next_index_rr( void )
{
    static int i = 0;
    int rc = 0;

    i = i % MAX_INS_COUNT;
    rc = i;

    i++;
    return rc;
}


static int
mw_xen_new_socket_rr( void )
{
    static int curr_index = 0;
    int selected = 0;

    for ( int i = 0; i < MAX_INS_COUNT; i++ )
    {
        if( g_mwxen_state.ins[ curr_index % MAX_INS_COUNT ].is_ring_ready  )
        {
            selected = curr_index % MAX_INS_COUNT;
            curr_index++;
            goto ErrorExit;
        }
    }

ErrorExit:
    return g_mwxen_state.ins[selected].domid;
}


static void
MWSOCKET_DEBUG_ATTRIB
mw_xen_release_ins( mwcomms_ins_data_t * Ins )
{
    if ( 0 == atomic64_read( &Ins->in_use ) ) { goto ErrorExit; }

    for ( int i = 0; i < XENEVENT_GRANT_REF_COUNT; i++ )
    {
        if ( 0 != Ins->grant_refs[ i ] )
        {
            pr_verbose( "Ending access to grant ref 0x%x\n", Ins->grant_refs[i] );
            gnttab_end_foreign_access_ref( Ins->grant_refs[i], 0 );
        }
    }

    if ( Ins->irq )
    {
        unbind_from_irqhandler( Ins->irq, NULL );
    }

    if ( !mw_xen_is_evt_chn_closed( Ins ) )
    {
        mw_xen_free_unbound_evt_chn( Ins );
    }

ErrorExit:
    atomic64_set( &Ins->in_use, 0 );
    Ins->is_ring_ready = false;
    Ins->domid = DOMID_INVALID;
    return;
}


int
MWSOCKET_DEBUG_ATTRIB
mw_xen_reap_dead_ins( OUT int Dead_INS_Array[ MAX_INS_COUNT ] )
{
    unsigned long now = jiffies;

    int dead_ins_num = 0;

    for ( int i = 0; i < MAX_INS_COUNT; i++ )
    {
        mwcomms_ins_data_t * ins = &g_mwxen_state.ins[ i ];

        // Skip if not in use
        if ( 0 == atomic64_read( &ins->in_use ) ) { continue; }

        unsigned long age = ( now - ins->last_seen_time ) / HZ;

        // Skip if it has been seen recently
        if ( age <=
             (HEARTBEAT_INTERVAL_SEC * ( ins->missed_heartbeats + 1 ) ) )
        {
            continue;
        }

        // Otherwise, increase missed_heartbeats counter every
        // time we've gone HEARTBEAT_INTERVAL_SEC seconds without
        // hearing from INS
        ++ins->missed_heartbeats;
        pr_info( "INS %d has missed %d hearbeat(s)\n",
                 ins->domid, ins->missed_heartbeats );

        if ( ins->missed_heartbeats == HEARTBEAT_MAX_MISSES )
        {
            pr_warn( "INS %d is now considered dead\n", ins->domid );
            Dead_INS_Array[ dead_ins_num++ ] = ins->domid;
            mw_xen_release_ins( ins );
        }
    }

//ErrorExit:
    return 0;
}


bool
mw_xen_response_available( OUT void ** Handle )
{
    bool available = false;
    int ins_index = 0;

    for ( int i = 0; i < MAX_INS_COUNT; i++ )
    {
        ins_index = mw_xen_get_next_index_rr();

        if ( !g_mwxen_state.ins[ins_index].is_ring_ready ) { continue; }

        available =
            RING_HAS_UNCONSUMED_RESPONSES( &g_mwxen_state.ins[ins_index].front_ring );

        if ( !available ) { continue; }

        *Handle = (void *) &g_mwxen_state.ins[ins_index];
        pr_verbose( "Response available on INS %d (0x%x)\n",
                    g_mwxen_state.ins[ins_index].domid,
                    g_mwxen_state.ins[ins_index].domid );
        break;
    }

    return available;
}


int
MWSOCKET_DEBUG_ATTRIB
mw_xen_get_next_response( OUT mt_response_generic_t ** Response,
                          OUT void                   * Handle )
{
    MYASSERT( Handle );
    MYASSERT( Response );

    int rc = 0;
    mt_response_generic_t * response = NULL;
    mwcomms_ins_data_t    * ins = (mwcomms_ins_data_t *) Handle;

    //
    // An item is available. Consume it. The response resides
    // only on the ring now. It isn't in the active request
    // yet. We only copy it there upon request.
    //
    response = (mt_response_generic_t *)
        RING_GET_RESPONSE( &ins->front_ring,
                           ins->front_ring.rsp_cons );

    if ( !MT_IS_RESPONSE( response ) )
    {
        // Fatal: The ring is corrupted.
        pr_crit( "Received data that is not a response at idx %d\n",
                 ins->front_ring.rsp_cons );
        rc = -EIO;
        goto ErrorExit;
    }

    *Response = response;

ErrorExit:
    return rc;
}


int
mw_xen_mark_response_consumed( IN void * Handle )
{
    int rc = 0;
    mwcomms_ins_data_t *ins = ( mwcomms_ins_data_t * ) Handle;

    if( NULL == ins )
    {
        pr_err( "NULL INS pointer detected\n" );
        rc = -EIO;
        goto ErrorExit;
    }
    ++ins->front_ring.rsp_cons;

ErrorExit:
    return rc;
}


static void
mw_xen_wait( long TimeoutJiffies )
{
    set_current_state( TASK_INTERRUPTIBLE );
    schedule_timeout( TimeoutJiffies );
}


int
mw_xen_get_ins_from_domid( IN domid_t               Domid,
                           OUT mwcomms_ins_data_t **Ins )
{
    int rc = -ENXIO;
    int idx = 0;

    for( idx = 0; idx < MAX_INS_COUNT; idx++ )
    {
        if( g_mwxen_state.ins[idx].domid == Domid )
        {
            rc = 0;
            *Ins = &g_mwxen_state.ins[idx];
            break;
        }
    }

    if( rc )
    {
        pr_info( "Could not get INS from domid=%d\n", Domid );
        goto ErrorExit;
    }

    // Success above, now verify the INS is alive
    if( !atomic64_read( &g_mwxen_state.ins[idx].in_use ) )
    {
        MYASSERT( !"INS was found but is no longer in use" );
        rc = -ESTALE;
        goto ErrorExit;
    }

    MYASSERT( 0 == rc );

ErrorExit:
    return rc;
}


int
MWSOCKET_DEBUG_ATTRIB
mw_xen_get_next_request_slot( IN  bool                    WaitForRing,
                              IN  domid_t                 DomId,
                              OUT mt_request_generic_t ** Dest,
                              OUT void                 ** Handle )
{
    int                  rc    = 0;
    mwcomms_ins_data_t * ins   = NULL;

    MYASSERT( NULL != Handle );
    MYASSERT( NULL != Dest );

    if ( DOMID_INVALID == DomId || (domid_t)-1 == DomId )
    {
        DomId = mw_xen_new_socket_rr();
    }

    rc = mw_xen_get_ins_from_domid( DomId, &ins );
    if( rc ) { goto ErrorExit; }

    if ( !ins->is_ring_ready )
    {
        MYASSERT( !"Received request against unprepared/dead INS.\n" );
        rc = -EIO;
        goto ErrorExit;
    }

    // The INS is ready. No matter what failures occur below, we
    // acquire the lock now. That means we must also return the handle
    // to the caller (in locked state).

    mutex_lock(&g_mwxen_state.ring_lock);

    ins->locked = true;
    ins->curr_req = NULL;

    *Handle = ins;

    while( true )
    {
        // Is the INS still alive?
        if( 0 == atomic64_read( &ins->in_use ) )
        {
            rc = -ESTALE;
            pr_info( "INS %d died since request arrived. Failing it now, rc=%d.\n",
                     DomId, rc );
            goto ErrorExit;
        }
        if( !RING_FULL( &ins->front_ring ) ) { break; }
        if( !WaitForRing )
        {
            // Wait was not requested and there's no slot, so report
            // the failure. This happens very often so don't emit a
            // message for it.
            rc = -EAGAIN;
            goto ErrorExit;
        }

        // Wait and try again...
        mw_xen_wait( RING_FULL_TIMEOUT );
    }

    *Dest = (mt_request_generic_t *)
        RING_GET_REQUEST( &ins->front_ring,
                          ins->front_ring.req_prod_pvt );
    if( !Dest )
    {
        pr_err( "Destination buffer is NULL\n" );
        rc = -EIO;
        goto ErrorExit;
    }

    // Success: *Dest is populated, rc is 0

    ins->curr_req = *Dest;

ErrorExit:
    return rc;
}


int
mw_xen_for_each_live_ins( IN mw_xen_per_ins_cb_t Callback,
                          IN void *              Arg )
{
    int rc = 0;
    mwcomms_ins_data_t * curr = NULL;

    for( int i = 0; i < MAX_INS_COUNT; i++ )
    {
        curr = &g_mwxen_state.ins[i];

        if ( !curr->is_ring_ready )
        {
            continue;
        }

        int rc2 = Callback( curr->domid, Arg );
        if( rc2 )
        {
            MYASSERT( !"Callback failed. Continuing." );
            rc = rc2;
        }
    }

    return rc;
}


/**
 * @brief Releases slot for request in this INS's ring buffer.
 *
 * Optionally sends the request to the INS. Closely paired with
 * mw_xen_get_next_request_slot().
 */
int
MWSOCKET_DEBUG_ATTRIB
mw_xen_release_request( IN void * Handle,
                        IN bool   SendRequest )
{
    int rc = 0;
    mwcomms_ins_data_t * ins = (mwcomms_ins_data_t *) Handle;

    // mw_xen_get_next_request_slot() gives a NULL handle upon failure
    if( NULL == ins ||
        0 == atomic64_read( &ins->in_use ) )
    {
        rc = -EINVAL;
        goto ErrorExit;
    }

    // mw_xen_get_next_request_slot() succeeded: the lock is acquired

    MYASSERT( ins->locked );

    if( g_mwxen_state.new_ins_delay > 0 ) {
        g_mwxen_state.new_ins_delay--;
        mdelay(100);
    }

    if( SendRequest )
    {
        mt_request_generic_t * req  = (mt_request_generic_t *)
            RING_GET_REQUEST( &ins->front_ring,
                              ins->front_ring.req_prod_pvt );
        MYASSERT( MT_IS_REQUEST( req ) );
        MYASSERT( ins->curr_req == req );
        if( !MT_IS_REQUEST( ins->curr_req ) )
        {
            rc = -EINVAL;
            MYASSERT( !"Invalid request on ring buffer!!!!" );
            goto ErrorExit;
        }

        // Advance the ring metadata in shared memory, notify remote side
        ++ins->front_ring.req_prod_pvt;
        RING_PUSH_REQUESTS( &ins->front_ring );

#if INS_USES_EVENT_CHANNEL
        rc = mw_xen_send_event( ins );
#endif

    }

ErrorExit:

    if( ins )
    {
        // MYASSERT( ins->locked ); // Valid in single-threaded usage
        ins->curr_req = NULL;
        ins->locked = false;
        mutex_unlock(&g_mwxen_state.ring_lock);
    }
    return rc;
}


static void
MWSOCKET_DEBUG_ATTRIB
mw_xen_xenstore_state_changed( struct xenbus_watch * W,
                               const char         ** V,
                               unsigned int          L )
{
    int rc = 0;

    if ( NULL == W
         || NULL == V )
    {
        MYASSERT( !"NULL passed in" );
        goto ErrorExit;
    }

    pr_debug( "XenStore path %s changed\n", V[ XS_WATCH_PATH ] );

    if ( strstr( V[ XS_WATCH_PATH ], INS_ID_KEY ) )
    {
        mw_xen_ins_found( V[ XS_WATCH_PATH ] );
        goto ErrorExit;
    }

    if ( strstr( V[ XS_WATCH_PATH ], VM_EVT_CHN_BOUND_KEY ) )
    {
        rc = mw_xen_bind_evt_chn( V[ XS_WATCH_PATH ] );
        if ( rc )
        {
            pr_err( "Problem with binding event chn\n ");
        }
        goto ErrorExit;
    }

    if ( strstr( V[ XS_WATCH_PATH ], INS_HEARTBEAT_KEY ) )
    {
        rc = mw_xen_ins_heartbeat( V[ XS_WATCH_PATH ] );
        goto ErrorExit;
    }
    
ErrorExit:
    return;
}


static struct xenbus_watch
mw_xen_xenstore_watch =
{
    .node = XENEVENT_XENSTORE_ROOT,
    .callback = mw_xen_xenstore_state_changed
};


static int
mw_xen_initialize_keystore(void)
{
    int rc = 0;

    rc = mw_xen_mkdir( XENEVENT_XENSTORE_ROOT, 
                       XENEVENT_XENSTORE_PVM_NODE );

    if ( rc ) goto ErrorExit;

    rc = mw_xen_mkdir( XENEVENT_XENSTORE_PVM, 
                       SERVER_ID_KEY );

    if ( rc ) goto ErrorExit;

ErrorExit:
    return rc;
}


static int 
mw_xen_initialize_keys(void)
{
    int rc = 0;

    rc = mw_xen_write_to_key( XENEVENT_XENSTORE_PVM,
                              SERVER_ID_KEY,
                              KEY_RESET_VAL );
    if ( rc ) goto ErrorExit;

ErrorExit:
    return rc;
}


int
MWSOCKET_DEBUG_ATTRIB
mw_xen_init( mw_xen_init_complete_cb_t CompletionCallback ,
             mw_xen_event_handler_cb_t EventCallback )
{
    int rc = 0;

    bzero( &g_mwxen_state, sizeof(g_mwxen_state) );

    g_mwxen_state.completion_cb = CompletionCallback;
    g_mwxen_state.event_cb = EventCallback;
    g_mwxen_state.pending_exit = false;

    // Serialize all Xen ring buffer accesses
    mutex_init(&g_mwxen_state.ring_lock);

    // Create keystore path for pvm
    rc = mw_xen_initialize_keystore();
    if ( rc )
    {
        pr_err( "Keystore initialization failed\n" );
        goto ErrorExit;
    }

    // Set all protocol keys to zero
    rc = mw_xen_initialize_keys();
    if ( rc )
    {
        pr_err( "Key initialization failed: %d\n", rc );
        goto ErrorExit;
    }

    // 1. Write Dom Id for Server to Key
    rc = mw_xen_write_server_id_to_key();
    if ( rc )
    {
        goto ErrorExit;
    }

    // 2. Watch Client Id XenStore Key
    rc = register_xenbus_watch( &mw_xen_xenstore_watch );
    if (rc)
    {
        pr_err( "Failed to set xenstore_watcher\n" );
        goto ErrorExit;
    }

    g_mwxen_state.xenbus_watch_active = true;

ErrorExit:
    return rc;
}


void
MWSOCKET_DEBUG_ATTRIB
mw_xen_fini( void )
{
    g_mwxen_state.pending_exit = true;

    for( int i = 0; i < MAX_INS_COUNT; i++ )
    {
        mw_xen_release_ins( &g_mwxen_state.ins[i] );
    }

    if ( g_mwxen_state.xenbus_watch_active )
    {
        unregister_xenbus_watch( &mw_xen_xenstore_watch );
    }

    (void) mw_xen_initialize_keys();

    mw_xen_rm( XENEVENT_XENSTORE_ROOT, XENEVENT_XENSTORE_PVM_NODE );
}


domid_t
mw_xen_get_local_domid( void )
{
    return g_mwxen_state.my_domid;
}
