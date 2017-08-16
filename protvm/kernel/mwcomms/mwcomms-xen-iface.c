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
#include <linux/kthread.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <asm/uaccess.h>          
#include <linux/time.h>

#include <linux/list.h>

#include <xen/grant_table.h>
#include <xen/page.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/interface/callback.h>
#include <xen/interface/io/ring.h>

#include "mwcomms-xen-iface.h"
#include <xen_keystore_defs.h>

// Per-INS data
typedef struct _mwcomms_xen_ins
{

} mwcomms_xen_ins_t;



typedef struct _mwcomms_xen_globals
{
    domid_t  my_domid;
    domid_t  remote_domid;

    mw_region_t xen_shmem;

    bool xenbus_watch_active;

    int common_evtchn;
    int irq;

    grant_ref_t grant_refs[ XENEVENT_GRANT_REF_COUNT ];

    mw_xen_init_complete_cb_t * completion_cb;

    mw_xen_event_handler_cb_t * event_cb;
} mwcomms_xen_globals_t;

static mwcomms_xen_globals_t g_mwxen_state;


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


char *
mw_xen_read_from_key( const char * Dir, const char * Node )
{
   struct xenbus_transaction   txn;
   char                       *str;
   int                         err;

   pr_debug( "Reading value in dir:%s node:%s\n", Dir, Node );

   err = xenbus_transaction_start(&txn);
   if (err) {
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
mw_xen_create_unbound_evt_chn( void )
{
   struct evtchn_alloc_unbound alloc_unbound; 
   char                        str[MAX_GNT_REF_WIDTH] = {0};
   char                        path[ XENEVENT_PATH_STR_LEN ] = {0};
   int                         err = 0;

   if ( !g_mwxen_state.remote_domid )
   {
       goto ErrorExit;
   }

   alloc_unbound.dom        = g_mwxen_state.my_domid;
   alloc_unbound.remote_dom = g_mwxen_state.remote_domid; 

   err = HYPERVISOR_event_channel_op( EVTCHNOP_alloc_unbound, &alloc_unbound );
   if ( err )
   {
       pr_err("Failed to set up event channel\n");
       goto ErrorExit;
   }

   g_mwxen_state.common_evtchn = alloc_unbound.port;

   pr_debug( "Event Channel Port (%d <=> %d): %d\n",
             alloc_unbound.dom, g_mwxen_state.remote_domid,
             g_mwxen_state.common_evtchn );

   snprintf( str, MAX_GNT_REF_WIDTH,
             "%u", g_mwxen_state.common_evtchn );

   snprintf( path, sizeof(path), "%s/%d",
             XENEVENT_XENSTORE_ROOT, g_mwxen_state.remote_domid );

   err = mw_xen_write_to_key( path, VM_EVT_CHN_PORT_KEY, str );

ErrorExit:
   return err;
}


static irqreturn_t
mw_xen_irq_event_handler( int Port, void * Data )
{
    g_mwxen_state.event_cb();

    return IRQ_HANDLED;
}


void
mw_xen_send_event( void )
{
   struct evtchn_send send;

   send.port = g_mwxen_state.common_evtchn;

   //xen_clear_irq_pending(irq);

   if ( HYPERVISOR_event_channel_op(EVTCHNOP_send, &send) )
   {
       pr_err( "Failed to send event\n" );
   }
}


static bool
mw_xen_is_evt_chn_closed( void )
{
   struct evtchn_status status;
   int                  rc;

   status.dom = DOMID_SELF;
   status.port = g_mwxen_state.common_evtchn;

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
mw_xen_free_unbound_evt_chn( void )
{
    struct evtchn_close close = { 0 };
   int err = 0;

   if ( !g_mwxen_state.common_evtchn )
   {
       goto ErrorExit;
   }

   close.port = g_mwxen_state.common_evtchn;

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
mw_xen_offer_grant( domid_t ClientId )
{
   int rc = 0;

   // Calc the VA, then find the backing pseudo-physical address (Xen
   // book pg 61). The pages were allocated via GFP() which returns a
   // physically-contiguous block of pages.

   unsigned long mfn = virt_to_mfn( g_mwxen_state.xen_shmem.ptr );
   
   for ( int i = 0; i < g_mwxen_state.xen_shmem.pagect; ++i )
   {
       rc = gnttab_grant_foreign_access( g_mwxen_state.remote_domid,
                                         mfn + i,
                                         0 ); // not RO
       if ( rc < 0 )
       {
           pr_err( "gnttab_grant_foreign_access failed: %d\n", rc );
           goto ErrorExit;
       }

       g_mwxen_state.grant_refs[ i ] = rc;
       pr_debug( "VA: %p MFN: %p grant 0x%x\n",
                 (void *) ((unsigned long)g_mwxen_state.xen_shmem.ptr +
                           i * PAGE_SIZE),
                 (void *)(mfn+i),
                 rc );
   }

   // Success:
   rc = 0;

ErrorExit:
   return rc;
}


static int
mw_xen_write_grant_refs_to_key( void )
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
                       "%x ", g_mwxen_state.grant_refs[i] ) >= sizeof(one_ref))
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
              XENEVENT_XENSTORE_ROOT, g_mwxen_state.remote_domid );

    rc = mw_xen_write_to_key( path, GNT_REF_KEY, gnt_refs );

ErrorExit:
    if ( NULL != gnt_refs )
    {
        kfree( gnt_refs );
    }
    return rc;
}


static void
mw_xen_vm_port_is_bound( const char * Path )
{
    char * is_bound_str = NULL;

    is_bound_str = (char *) mw_xen_read_from_key( Path, 
                                                  XENEVENT_NO_NODE );
    if ( !is_bound_str )
    {
        goto ErrorExit;
    }

    if ( 0 == strcmp( is_bound_str, "0" ) )
    {
        goto ErrorExit;
    }

    pr_debug( "The remote event channel is bound\n" );

    g_mwxen_state.irq =
        bind_evtchn_to_irqhandler( g_mwxen_state.common_evtchn,
                                   mw_xen_irq_event_handler,
                                   0, NULL, NULL );

    pr_debug( "Bound event channel %d to irq: %d\n",
              g_mwxen_state.common_evtchn, g_mwxen_state.irq );

ErrorExit:
    if ( NULL != is_bound_str )
    {
        kfree( is_bound_str );
    }
    return;
}


static void
mw_ins_dom_id_found( const char * Path )
{
    char     *client_id_str = NULL;
    int       err = 0;

    client_id_str = (char *)mw_xen_read_from_key( Path,
                                                  XENEVENT_NO_NODE );
    if ( !client_id_str )
    {
        pr_err( "Error reading client id key!!!\n" );
        goto ErrorExit;
    }

    if (strcmp(client_id_str, "0") == 0)
    {
        goto ErrorExit;
    }

    //
    // Get the client Id 
    // 
    g_mwxen_state.remote_domid = simple_strtol( client_id_str, NULL, 10 );
    pr_debug( "Discovered client ID: %u\n", g_mwxen_state.remote_domid );

    // Create unbound event channel with client
    err = mw_xen_create_unbound_evt_chn();
    if ( err ) { goto ErrorExit; }
   
    // Offer Grant to Client
    err = mw_xen_offer_grant( g_mwxen_state.remote_domid );
    if ( err ) { goto ErrorExit; }

    // Write Grant Ref to key 
    err = mw_xen_write_grant_refs_to_key();
    if ( err ) { goto ErrorExit; }

    //
    // Complete: the handshake is done
    //
    pr_debug( "Handshake with client is complete\n" );
    g_mwxen_state.completion_cb( g_mwxen_state.remote_domid );

ErrorExit:
    if ( NULL != client_id_str )
    {
        kfree(client_id_str);
    }
    return;
}


static void
mw_xenstore_state_changed( struct xenbus_watch * W,
                           const char         ** V,
                           unsigned int          L )
{
    if ( NULL == W
         || NULL == V )
    {
        MYASSERT( !"NULL passed in" );
        goto ErrorExit;
    }

    pr_debug( "XenStore path %s changed\n", V[ XS_WATCH_PATH ] );

    if ( strstr( V[ XS_WATCH_PATH ], INS_ID_KEY ) )
    {
        mw_ins_dom_id_found( V[ XS_WATCH_PATH ] );
        goto ErrorExit;
    }

    if ( strstr( V[ XS_WATCH_PATH ], VM_EVT_CHN_BOUND_KEY ) )
    {
        mw_xen_vm_port_is_bound( V[ XS_WATCH_PATH ] );
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
mw_xen_init( mw_region_t * SharedMem,
             mw_xen_init_complete_cb_t CompletionCallback,
             mw_xen_event_handler_cb_t EventCallback )
{
    int rc = 0;

    MYASSERT( SharedMem );

    bzero( &g_mwxen_state, sizeof(g_mwxen_state) );

    g_mwxen_state.xen_shmem     = *SharedMem;
    g_mwxen_state.completion_cb = CompletionCallback;
    g_mwxen_state.event_cb      = EventCallback;

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

    // *******************************************************
    // Since we have both watchers watching the root, this
    // is ineficient, consider merging both watches into one
    // function
    // *******************************************************

    // 2. Watch Client Id XenStore Key
    rc = register_xenbus_watch( &mw_xenstore_watch );
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
mw_xen_fini( void )
{
    for ( int i = 0; i < XENEVENT_GRANT_REF_COUNT; i++ )
    {
        if ( 0 != g_mwxen_state.grant_refs[ i ] )
        {
            pr_debug( "Ending access to grant ref 0x%x\n", g_mwxen_state.grant_refs[i] );
            gnttab_end_foreign_access_ref( g_mwxen_state.grant_refs[i], 0 );
        }
    }

    if ( g_mwxen_state.xenbus_watch_active )
    {
        unregister_xenbus_watch( &mw_xenstore_watch );
    }

    (void) mw_xen_initialize_keys();

    if ( g_mwxen_state.irq )
    {
        unbind_from_irqhandler( g_mwxen_state.irq, NULL );
    }

    if ( !mw_xen_is_evt_chn_closed() )
    {
        mw_xen_free_unbound_evt_chn();
    }

    mw_xen_rm( XENEVENT_XENSTORE_ROOT, XENEVENT_XENSTORE_PVM_NODE );
}


domid_t
mw_xen_get_local_domid( void )
{
    return g_mwxen_state.my_domid;
}
