/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
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
//#include <linux/moduleparam.h>
//#include <linux/slab.h>
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

#include "mwcomms-common.h"
#include "mwcomms-xen-iface.h"




// Even with verbose debugging, don't show these request/response types
#define DEBUG_SHOW_TYPE( _t )                         \
    ( ((_t) & MT_TYPE_MASK) != MtRequestPollsetQuery )


//how long to wait if we want to write a request but the ring is full
#define RING_FULL_TIMEOUT (HZ >> 6)

#define INS_REAPER_INTERVAL_SEC 1

typedef struct _mwcomms_xen_globals
{
    domid_t          my_domid;

    bool             xenbus_watch_active;
    bool             pending_exit;
    bool             xen_iface_ready;    

    struct semaphore event_channel_sem;

    mw_xen_init_complete_cb_t * completion_cb;
    mw_xen_event_handler_cb_t * event_cb;

    // Kernel thread info:
    struct task_struct * ins_reaper_thread;
    struct completion    ins_reaper_completion;
} mwcomms_xen_globals_t;

static mwcomms_xen_globals_t g_mwxen_state = {0};

static mwcomms_ins_data_t g_ins_data[ MAX_INS_COUNT ] = {0};

static int
mw_xen_rm( const char *Dir, const char *Node )
{
    struct xenbus_transaction   txn;
    int                         err;
    bool                        txnstarted = false;
    int                         term = 0;

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
   struct xenbus_transaction   txn;
   int                         err;
   bool                  txnstarted = false;
   int                   term = 0;

   pr_debug( "Writing %s to %s/%s\n", Value, Dir, Node );

   err = xenbus_transaction_start(&txn);
   if ( err )
   {
       pr_err("Error starting xenbus transaction\n");
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
       pr_err("Could not write to XenStore Key dir:%s node:%s\n", Dir, Node );
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
   if (err) {
      pr_err("Error starting xenbus transaction\n");
      return NULL;
   }

   str = (char *)xenbus_read(txn, Dir, Node, NULL);
   if (XENBUS_IS_ERR_READ(str))
   {
      pr_err("Could not read XenStore Key: %s/%s\n", Dir, Node );
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
       pr_err("Error: Failed to read my Dom Id Key\n");
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

   if ( !Ins->domid )
   {
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


static irqreturn_t
mw_xen_irq_event_handler( int Port, void * Data )
{
    g_mwxen_state.event_cb();
    return IRQ_HANDLED;
}


static void
mw_xen_send_event( void )
{
   struct evtchn_send send;
   int temp = 0;

   //send.port = Ins->common_evtchn;
   //TODO REMOVE THSI IS BAD
   send.port = g_ins_data[temp].common_evtchn;
   
   //xen_clear_irq_pending(irq);

   if ( HYPERVISOR_event_channel_op(EVTCHNOP_send, &send) )
   {
       pr_err("Failed to send event\n");
   }
}


static bool
mw_xen_is_evt_chn_closed( mwcomms_ins_data_t * Ins )
{
   struct evtchn_status status;
   int                  rc;

   status.dom = DOMID_SELF;
   status.port = Ins->common_evtchn;

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
mw_xen_offer_grant( mwcomms_ins_data_t *Ins )
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
                                         0 );
       if ( rc < 0 )
       {
           pr_err( "gnttab_grant_foreign_access failed: %d\n", rc );
           goto ErrorExit;
       }

       Ins->grant_refs[ i ] = rc;
       pr_debug( "VA: %p MFN: %p grant 0x%x\n",
                 (void *) ((unsigned long)Ins->ring.ptr +
                           i * PAGE_SIZE),
                 (void *)(mfn+i),
                 rc );
   }

   //Success
   rc = 0;

ErrorExit:
   return rc;
}


static int
mw_xen_write_grant_refs_to_key( mwcomms_ins_data_t *Ins )
{
    int rc = 0;

    // Must be large enough for one grant ref, in hex, plus '\0'
    // Make space for 12345678\0
    char one_ref[ 8 + 1 ];
    
    // XXXX: If we make the shared memory region "really big", we may
    // have to get this memory via kmalloc()
    char gnt_refs[ XENEVENT_GRANT_REF_COUNT * sizeof(one_ref) ] = {0};

    char path[ XENEVENT_PATH_STR_LEN ] = {0};

    for ( int i = 0; i < XENEVENT_GRANT_REF_COUNT; i++ )
    {
        if ( snprintf( one_ref, sizeof(one_ref),
                       "%x ", Ins->grant_refs[i] ) >= sizeof(one_ref))
        {
            pr_err("Insufficient space to write grant ref.\n");
            rc = -E2BIG;
            goto ErrorExit;
        }

        if ( strncat( gnt_refs, one_ref, sizeof(gnt_refs) ) >=
             gnt_refs + sizeof(gnt_refs) )
        {
            pr_err("Insufficient space to write all grant refs.\n");
            rc = -E2BIG;
            goto ErrorExit;
        }
    }

    snprintf( path, sizeof(path), "%s/%d",
              XENEVENT_XENSTORE_ROOT, Ins->domid );

    rc = mw_xen_write_to_key( path, GNT_REF_KEY, gnt_refs );

ErrorExit:
    return rc;
}


static int
get_ins_from_xs_path( IN  const char *Path,
                      OUT mwcomms_ins_data_t ** Ins )
{
    int rc = -EINVAL;
    int index = 0;
    int domid = 0;
    char * copy = NULL;

    copy = kmalloc( XENEVENT_PATH_STR_LEN, GFP_KERNEL | __GFP_ZERO );
    if( NULL == copy )
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
        pr_err( "Could not get domid from path\n" );
        goto ErrorExit;
    }

    for( int i = 0; i < MAX_INS_COUNT; i++ )
    {
        if( g_ins_data[i].domid == domid )
        {
            rc = 0;
            *Ins = &g_ins_data[i];
            break;
        }
    }

ErrorExit:
    
    if( NULL != copy )
    {
        kfree( copy );
    }
    return rc;
}
            
            

static int
MWSOCKET_DEBUG_ATTRIB
mw_xen_vm_port_is_bound( const char *Path )
{
    char * is_bound_str = NULL;
    mwcomms_ins_data_t *Ins = NULL;
    int rc = 0;

    
    if( get_ins_from_xs_path( Path, &Ins ) )
    {
        pr_err( "No ins found for vm_port_is_bound\n");
        rc = -EIO;
        goto ErrorExit;
    }

    mw_xen_ins_alive( Ins );

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

    pr_debug("The remote event channel is bound\n");

    Ins->irq =
        bind_evtchn_to_irqhandler( Ins->common_evtchn,
                                   mw_xen_irq_event_handler,
                                   0, NULL, NULL );

    pr_debug( "Bound event channel %d to irq: %d\n",
              Ins->common_evtchn, Ins->irq );

ErrorExit:
    if ( NULL != is_bound_str )
    {
        kfree( is_bound_str );
    }
    return rc;
}


static int
MWSOCKET_DEBUG_ATTRIB
mw_xen_ins_heartbeat( const char * Path )
{
    int rc = 0;
    mwcomms_ins_data_t * ins = NULL;

    rc = get_ins_from_xs_path( Path, &ins );
    if ( rc )
    {
        pr_err( "No ins found for vm_port_is_bound\n" );
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

int
MWSOCKET_DEBUG_ATTRIB
mw_xen_init_ring_block( mwcomms_ins_data_t * Ins )
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
mw_xen_ins_found( const char *Path )
{
    char       *client_id_str = NULL;
    int        err = 0;
    mwcomms_ins_data_t *curr = NULL;

    client_id_str = (char *)mw_xen_read_from_key( Path,
                                                  XENEVENT_NO_NODE );
    if ( !client_id_str )
    {
        err = -EIO;
        pr_err("Error reading client id key!!!\n");
        goto ErrorExit;
    }

    if ( strcmp( client_id_str, "0" ) == 0 )
    {
        err = -EIO;
        goto ErrorExit;
    }

    //
    // Get the client Id 
    //
    for( int i = 0; i < MAX_INS_COUNT; i++ )
    {
        // atomic64_cmpexchng returns the original value
        // of the atomic64_t
        if( 0 == atomic64_cmpxchg( &g_ins_data[i].in_use, 0, 1 ) )
        {
            g_ins_data[i].domid = simple_strtol( client_id_str, NULL, 10 );
            curr = &g_ins_data[i];
            pr_debug("Discovered client, domid %d added to ins_data[] pos: %d\n",
                     g_ins_data[i].domid, i );

            break;
        }
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
    curr->is_ring_ready = true;
    pr_info( "INS %d is ready\n", curr->domid );

    g_mwxen_state.xen_iface_ready = true;
    g_mwxen_state.completion_cb();

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
    return g_mwxen_state.xen_iface_ready;
}


static int
mw_xen_get_next_index_round_robin( void )
{
    static int i = 0;
    int rc = 0;

    i = i % MAX_INS_COUNT;
    rc = i;

    i++;
    return rc;
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
            pr_debug("Ending access to grant ref 0x%x\n", Ins->grant_refs[i]);
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
    return;
}


static int
MWSOCKET_DEBUG_ATTRIB
mw_xen_ins_reaper( void * Arg )
{
    while ( true )
    {
        unsigned long now = jiffies;

        for ( int i = 0; i < MAX_INS_COUNT; i++ )
        {
            mwcomms_ins_data_t * ins = &g_ins_data[i];

            // Skip if not in use
            if ( 0 == atomic64_read( &ins->in_use ) ) { continue; }

            // Skip if it has been seen recently
            unsigned long age = ( now - ins->last_seen_time ) / HZ;

            // Otherwise, increase missed_heartbeats counter every
            // time we've gone HEARTBEAT_INTERVAL_SEC seconds without
            // hearing from INS
            if ( age <=
                 (HEARTBEAT_INTERVAL_SEC * ( ins->missed_heartbeats + 1 ) ) )
            {
                continue;
            }

            ++ins->missed_heartbeats;
            pr_info( "INS %d has missed %d hearbeat(s)\n",
                    ins->domid, ins->missed_heartbeats );

            if ( ins->missed_heartbeats == HEARTBEAT_MAX_MISSES )
            {
                pr_warn( "INS %d is now considered dead\n", ins->domid );
                mw_xen_release_ins( ins );
            }
        }

        if ( g_mwxen_state.pending_exit )
        {
            pr_debug( "Detected pending exit\n" );
            goto ErrorExit;
        }

        // Sleep till next check
        set_current_state( TASK_INTERRUPTIBLE );
        schedule_timeout( INS_REAPER_INTERVAL_SEC * HZ );
    }

ErrorExit:
    complete_all( &g_mwxen_state.ins_reaper_completion );
    return 0;
}


bool
mw_xen_response_available(OUT mwcomms_ins_data_t ** Ins)
{
    bool available = false;
    int ins_index = 0;
    
    for( int i = 0; i < MAX_INS_COUNT; i++ )
    {
        ins_index = mw_xen_get_next_index_round_robin();
        
        if( g_ins_data[ins_index].is_ring_ready )
        {
            available =
                RING_HAS_UNCONSUMED_RESPONSES( &g_ins_data[ins_index].front_ring );
            if( available )
            {
                *Ins = &g_ins_data[ins_index];
                goto ErrorExit;
            }
        }
    }

ErrorExit:
    return available;
}


int
MWSOCKET_DEBUG_ATTRIB
mw_xen_get_next_response( OUT mt_response_generic_t **Response,
                          OUT mwcomms_ins_data_t    *Ins )
{

    int rc = 0;
    mt_response_generic_t *found_response = NULL;


    if( NULL == Ins )
    {
        pr_err("Null ins passed to mw_xen_get_resposne\n");
        rc = -EIO;
        goto ErrorExit;
    }

    //
    // An item is available. Consume it. The response resides
    // only on the ring now. It isn't in the active request
    // yet. We only copy it there upon request.
    //
    found_response = (mt_response_generic_t *)
        RING_GET_RESPONSE( &Ins->front_ring,
                           Ins->front_ring.rsp_cons );

    if ( DEBUG_SHOW_TYPE( found_response->base.type ) )
    {
        pr_debug( "Response ID %lx size %x type %x status %d on ring \
                       at idx %x\n",
                  (unsigned long)found_response->base.id,
                  found_response->base.size, found_response->base.type,
                  found_response->base.status,
                  Ins->front_ring.rsp_cons );
    }

    if ( !MT_IS_RESPONSE( found_response ) )
    {
        // Fatal: The ring is corrupted.
        pr_crit( "Received data that is not a response at idx %d\n",
                 Ins->front_ring.rsp_cons );
        rc = -EIO;
        goto ErrorExit;
    }

    *Response = found_response;

ErrorExit:
    return rc;
}

int
mw_xen_mark_response_consumed( mwcomms_ins_data_t * Ins )
{
    int rc = 0;
    
    if( NULL == Ins )
    {
        pr_err( "NULL INS pointer detected\n" );
        rc = -EIO;
        goto ErrorExit;
    }
    ++Ins->front_ring.rsp_cons;
    
ErrorExit:
    return rc;
}

static void
mw_xen_socket_wait( long TimeoutJiffies )
{
    set_current_state( TASK_INTERRUPTIBLE );
    schedule_timeout( TimeoutJiffies );
}


int
mw_xen_get_next_request_slot( bool WaitForRing, uint8_t **Dest )
{
    int temp = 0;
    int rc = 0;
    
    if ( !g_ins_data[temp].is_ring_ready )
    {
        MYASSERT( !"Received request too early - ring not ready.\n" );
        rc = -EIO;
        goto ErrorExit;
    }

    do
    {
        if ( !RING_FULL( &g_ins_data[temp].front_ring ) )
        {
            break;
        }
        if ( !WaitForRing )
        {
            // Wait was not requested so fail. This happens often so
            // don't emit a message for it.
            rc = -EAGAIN;
            goto ErrorExit;
        }

        // Wait and try again...
        mw_xen_socket_wait( RING_FULL_TIMEOUT );
    } while( true );
    
    *Dest = (uint8_t *) RING_GET_REQUEST( &g_ins_data[temp].front_ring,
                                         g_ins_data[temp].front_ring.req_prod_pvt );
    if ( !Dest )
    {
        pr_err( "Destination buffer is NULL\n" );
        rc = -EIO;
        goto ErrorExit;
    }

ErrorExit:
    //return for preprocessing
    return rc;
}

int
MWSOCKET_DEBUG_ATTRIB
mw_xen_dispatch_request( mt_request_generic_t      * Request,
                         uint8_t                   * dest)
{
    int temp = 0;
    
    memcpy( dest,
            Request,
            Request->base.size );

    ++g_ins_data[temp].front_ring.req_prod_pvt;
    RING_PUSH_REQUESTS( &g_ins_data[temp].front_ring );

#if INS_USES_EVENT_CHANNEL
    mw_xen_send_event();
#endif
    return 0;
}

    
static void
MWSOCKET_DEBUG_ATTRIB
mw_xenstore_state_changed( struct xenbus_watch *W,
                           const char **V,
                           unsigned int L )
{
    int err = 0;
    
    pr_debug( "XenStore path %s changed\n", V[ XS_WATCH_PATH ] );

    if ( strstr( V[ XS_WATCH_PATH ], INS_ID_KEY ) )
    {
        err = mw_xen_ins_found( V[ XS_WATCH_PATH ] );
        if ( err )
        {
            pr_err("Problem with reading domid from xenstore\n");
        }
        goto ErrorExit;
    }

    if ( strstr( V[ XS_WATCH_PATH ], VM_EVT_CHN_BOUND_KEY ) )
    {
        err = mw_xen_vm_port_is_bound( V[ XS_WATCH_PATH ] );
        if ( err )
        {
            pr_err( "Problem with binding event chn\n ");
        }
        goto ErrorExit;
    }

    if ( strstr( V[ XS_WATCH_PATH ], INS_HEARTBEAT_KEY ) )
    {
        err = mw_xen_ins_heartbeat( V[ XS_WATCH_PATH ] );
        goto ErrorExit;
    }
    
ErrorExit:
    return;
}


static struct xenbus_watch
mw_xenstore_watch =
{
    .node = XENEVENT_XENSTORE_ROOT,
    .callback = mw_xenstore_state_changed
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

    rc = mw_xen_initialize_keystore();
    if ( rc )
    {
        pr_err("Keystore initialization failed\n");
        goto ErrorExit;
    }

    // Set all protocol keys to zero
    rc = mw_xen_initialize_keys();
    if ( rc )
    {
        pr_err("Key initialization failed: %d\n", rc );
        goto ErrorExit;
    }

    // 1. Write Dom Id for Server to Key
    rc = mw_xen_write_server_id_to_key();
    if ( rc )
    {
        goto ErrorExit;
    }

    // *******************************************************
    // Since we have both watchers watching the root, this
    // is ineficient, consider merging both watches into one
    // function
    // *******************************************************

    // 2. Watch Client Id XenStore Key
    rc = register_xenbus_watch( &mw_xenstore_watch );
    if (rc)
    {
        pr_err("Failed to set xenstore_watcher\n");
        goto ErrorExit;
    }

    g_mwxen_state.xenbus_watch_active = true;

    // 3. Reaper thread

    init_completion( &g_mwxen_state.ins_reaper_completion );

    g_mwxen_state.ins_reaper_thread =
        kthread_run( &mw_xen_ins_reaper,
                     NULL,
                     "mw_ins_reaper" );
    if ( NULL == g_mwxen_state.ins_reaper_thread )
    {
        MYASSERT( !"kthread_run" );
        rc = -ESRCH;
        goto ErrorExit;
    }
    
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
        mw_xen_release_ins( &g_ins_data[i] );
    }
    
    if ( g_mwxen_state.xenbus_watch_active )
    {
        unregister_xenbus_watch( &mw_xenstore_watch );
    }

    mw_xen_initialize_keys();

    mw_xen_rm( XENEVENT_XENSTORE_ROOT, XENEVENT_XENSTORE_PVM_NODE );

    if ( NULL != g_mwxen_state.ins_reaper_thread )
    {
        wait_for_completion( &g_mwxen_state.ins_reaper_completion );
    }
}


domid_t
mw_xen_get_local_domid( void )
{
    return g_mwxen_state.my_domid;
}
