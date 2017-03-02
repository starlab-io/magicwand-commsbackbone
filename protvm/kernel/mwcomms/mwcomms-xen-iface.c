/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

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

typedef struct _mwcomms_xen_globals {
    domid_t  my_domid;
    domid_t  remote_domid;

    mw_region_t xen_shmem;

    bool client_id_watch_active;
    bool evtchn_bound_watch_active;

    int common_evtchn;
    int irq;

    grant_ref_t grant_refs[ XENEVENT_GRANT_REF_COUNT ];

    mw_xen_init_complete_cb_t * completion_cb;

    mw_xen_event_handler_cb_t * event_cb;
} mwcomms_xen_globals_t;

static mwcomms_xen_globals_t g_mwxen_state;


static int
mw_xen_write_to_key(const char * dir, const char * node, const char * value)
{
   struct xenbus_transaction   txn;
   int                         err;
   bool                  txnstarted = false;
   int                   term = 0;
   
   pr_debug( "Begin write %s/%s <== %s\n", dir, node, value );
   
   err = xenbus_transaction_start(&txn);
   if (err)
   {
       pr_err("Error starting xenbus transaction\n");
       goto ErrorExit;
   }

   txnstarted = true;

   err = xenbus_exists( txn, dir, "" );
   // 1 ==> exists
   if ( !err )
   {
       pr_err( "Xenstore directory %s does not exist.\n", dir );
       err = -EIO;
       term = 1;
       goto ErrorExit;
   }
   
   err = xenbus_write(txn, dir, node, value);
   if (err)
   {
      pr_err("Could not write to XenStore Key\n");
      goto ErrorExit;
   }

ErrorExit:
   if ( txnstarted )
   {
       if ( xenbus_transaction_end(txn, term) )
       {
           pr_err("Failed to end transaction\n");
       }
   }
   
   return err;
}


static char *
mw_xen_read_from_key( const char * dir, const char * node )
{
   struct xenbus_transaction   txn;
   char                       *str;
   int                         err;

   pr_debug( "Begin read %s/%s <== ?\n", dir, node );

   err = xenbus_transaction_start(&txn);
   if (err) {
      pr_err("Error starting xenbus transaction\n");
      return NULL;
   }

   str = (char *)xenbus_read(txn, dir, node, NULL);
   if (XENBUS_IS_ERR_READ(str))
   {
      pr_err("Could not read XenStore Key\n");
      xenbus_transaction_end(txn,1);
      return NULL;
   }

   err = xenbus_transaction_end(txn, 0);

   pr_debug( "End read %s/%s <== %s\n", dir, node, str );
   
   return str;
}


static int
mw_xen_write_server_id_to_key(void) 
{
   const char *dom_id_str = NULL;
   int err = 0;
   
   // Get my domain id
   dom_id_str = (const char *) mw_xen_read_from_key( PRIVATE_ID_PATH, "" );

   if (!dom_id_str)
   {
       pr_err("Error: Failed to read my Dom Id Key\n");
       err = -EIO;
       goto ErrorExit;
   }

   pr_debug("Read my Dom Id Key: %s\n", dom_id_str);

   err = mw_xen_write_to_key( XENEVENT_XENSTORE_ROOT, SERVER_ID_KEY, dom_id_str );
   if ( err )
   {
       goto ErrorExit;
   }

   g_mwxen_state.my_domid = simple_strtol(dom_id_str, NULL, 10);

ErrorExit:
   if ( dom_id_str )
   {
       kfree(dom_id_str);
   }
   return err;
}




static int
mw_xen_create_unbound_evt_chn(void) 
{
   struct evtchn_alloc_unbound alloc_unbound; 
   char                        str[MAX_GNT_REF_WIDTH]; 
   int                         err = 0;

   if ( !g_mwxen_state.remote_domid )
   {
       goto ErrorExit;
   }

   //alloc_unbound.dom = DOMID_SELF;
   alloc_unbound.dom        = g_mwxen_state.my_domid;
   alloc_unbound.remote_dom = g_mwxen_state.remote_domid; 

   err = HYPERVISOR_event_channel_op(EVTCHNOP_alloc_unbound, &alloc_unbound);
   if (err) {
       pr_err("Failed to set up event channel\n");
       goto ErrorExit;
   }
   
   pr_debug("Event Channel Port (%d <=> %d): %d\n",
          alloc_unbound.dom, g_mwxen_state.remote_domid, alloc_unbound.port);

   g_mwxen_state.common_evtchn = alloc_unbound.port;

   pr_debug( "Event channel's local port: %d\n", g_mwxen_state.common_evtchn );
   memset( str, 0, MAX_GNT_REF_WIDTH );
   snprintf( str, MAX_GNT_REF_WIDTH,
             "%u", g_mwxen_state.common_evtchn );

   err = mw_xen_write_to_key( XENEVENT_XENSTORE_ROOT,
                              VM_EVT_CHN_PORT_KEY,
                              str );

ErrorExit:
   return err;
}


static irqreturn_t
mw_xen_irq_event_handler( int port, void * data )
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

   if (HYPERVISOR_event_channel_op(EVTCHNOP_send, &send))
   {
      pr_err("Failed to send event\n");
   }
   /*else
   {
      pr_info("Sent Event. Port: %u\n", send.port);
   }*/
}


static int 
mw_xen_is_evt_chn_closed( void )
{
   struct evtchn_status status;
   int                  rc;

   status.dom = DOMID_SELF;
   status.port = g_mwxen_state.common_evtchn;

   rc = HYPERVISOR_event_channel_op(EVTCHNOP_status, &status); 
   if (rc < 0)
     return 1;

   if (status.status != EVTCHNSTAT_closed)
      return 0;

   return 1;
}

static void 
mw_xen_free_unbound_evt_chn( void )
{
   struct evtchn_close close;
   int err;

   if (!g_mwxen_state.common_evtchn)
      return;
      
   close.port = g_mwxen_state.common_evtchn;

   err = HYPERVISOR_event_channel_op(EVTCHNOP_close, &close);

   if (err)
   {
      pr_err("Failed to close event channel\n");
   }
   else
   {
      pr_debug("Closed Event Channel Port: %d\n", close.port);
   }
}



/*
///////////////////////////////////////////////////
static void
mw_xen_init_shared_ring( void )
{
    
    SHARED_RING_INIT( g_mwxen_state.xen_shmem );
    FRONT_RING_INIT( &front_ring, shared_ring, shared_mem_size);

   ring_prepared = true;
   complete( &ring_ready );
}
////////
*/
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
                                         0 );
       if ( rc < 0 )
       {
           pr_err( "gnttab_grant_foreign_access: %d\n", rc );
           goto ErrorExit;
       }

       g_mwxen_state.grant_refs[ i ] = rc;
       //pr_info("VA: %p MFN: %p grant 0x%x\n", va, (void *)mfn, ret);
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
    char one_ref[5];
    
    // XXXX: If we make the shared memory region "really big", we may
    // have to get this memory via kmalloc()
    char gnt_refs[ XENEVENT_GRANT_REF_COUNT * sizeof(one_ref) ] = {0};

    for ( int i = 0; i < XENEVENT_GRANT_REF_COUNT; i++ )
    {
        if ( snprintf( one_ref, sizeof(one_ref),
                       "%x ", g_mwxen_state.grant_refs[i] ) >= sizeof(one_ref))
        {
            pr_err("Insufficient space to write grant ref.\n");
            rc = -E2BIG;
            goto ErrorExit;
        }
        
        if (strncat( gnt_refs, one_ref, sizeof(gnt_refs) ) >=
            gnt_refs + sizeof(gnt_refs) )
        {
            pr_err("Insufficient space to write all grant refs.\n");
            rc = -E2BIG;
            goto ErrorExit;
        }
    }

    rc = mw_xen_write_to_key( XENEVENT_XENSTORE_ROOT, GNT_REF_KEY, gnt_refs );

ErrorExit:
    return rc;
}


static void
mw_xen_client_id_state_changed( struct xenbus_watch *w,
                                const char **v,
                                unsigned int l )
{
    char     *client_id_str = NULL;
    int       err = 0;

    client_id_str = (char *)mw_xen_read_from_key( XENEVENT_XENSTORE_ROOT,
                                                  CLIENT_ID_KEY );
    if ( !client_id_str )
    {
        pr_err("Error reading client id key!!!\n");
        return;
    }

    if (strcmp(client_id_str, "0") == 0)
    {
        kfree(client_id_str);
        return;
    }

    //
    // Get the client Id 
    // 
    pr_debug("Client Id changed value:\n");
    pr_debug("\tRead Client Id Key: %s\n", client_id_str);

    g_mwxen_state.remote_domid = simple_strtol(client_id_str, NULL, 10);

    pr_debug("\t\tuint form: %u\n", g_mwxen_state.remote_domid );

    kfree(client_id_str);

    // Create unbound event channel with client
    err = mw_xen_create_unbound_evt_chn();
    if ( err ) return;
   
    // Offer Grant to Client
    mw_xen_offer_grant( g_mwxen_state.remote_domid );

    // Reset Client Id Xenstore Key
    err = mw_xen_write_to_key( XENEVENT_XENSTORE_ROOT, CLIENT_ID_KEY, KEY_RESET_VAL );
    if ( err )
    {
        // XXXX: There's more cleanup if we failed here
        return;
    }

    // Write Grant Ref to key 
    err = mw_xen_write_grant_refs_to_key();
    if ( err )
    {
        return;
    }

    //
    // Complete: the handshake is done
    //
    g_mwxen_state.completion_cb( g_mwxen_state.remote_domid );
}

static struct xenbus_watch
mw_xen_client_id_watch =
{
    .node = CLIENT_ID_PATH,
    .callback = mw_xen_client_id_state_changed
};


static void
mw_xen_vm_port_is_bound(struct xenbus_watch *w,
                             const char **v,
                             unsigned int l)
{
   char * is_bound_str = NULL; 

   pr_debug( "Checking whether %s is asserted\n", VM_EVT_CHN_BOUND_PATH );

   is_bound_str = (char *) mw_xen_read_from_key( XENEVENT_XENSTORE_ROOT,
                                                 VM_EVT_CHN_BOUND_KEY );
   if ( !is_bound_str )
   {
      return;
   }

   if ( 0 == strcmp( is_bound_str, "0" ) )
   {
      kfree(is_bound_str);
      return;
   }

   pr_debug("The remote event channel is bound\n");

   g_mwxen_state.irq =
       bind_evtchn_to_irqhandler( g_mwxen_state.common_evtchn,
                                  mw_xen_irq_event_handler,
                                  0, NULL, NULL );

   pr_debug( "Bound event channel %d to irq: %d\n",
             g_mwxen_state.common_evtchn, g_mwxen_state.irq );
}

static struct xenbus_watch
mw_xen_evtchn_bound_watch =
{
    .node = VM_EVT_CHN_BOUND_PATH,
    .callback = mw_xen_vm_port_is_bound
};

static int 
mw_xen_initialize_keys(void)
{
    int rc = 0;
    
    rc = mw_xen_write_to_key( XENEVENT_XENSTORE_ROOT,
                              CLIENT_ID_KEY,
                              KEY_RESET_VAL );
    if ( rc ) goto ErrorExit;
    
    rc = mw_xen_write_to_key( XENEVENT_XENSTORE_ROOT,
                              SERVER_ID_KEY,
                              KEY_RESET_VAL );
    if ( rc ) goto ErrorExit;
    
    rc = mw_xen_write_to_key( XENEVENT_XENSTORE_ROOT,
                              GNT_REF_KEY,
                              KEY_RESET_VAL );
    if ( rc ) goto ErrorExit;
    
    rc = mw_xen_write_to_key( XENEVENT_XENSTORE_ROOT,
                              VM_EVT_CHN_PORT_KEY,
                              KEY_RESET_VAL );
    if ( rc ) goto ErrorExit;
    
    rc = mw_xen_write_to_key( XENEVENT_XENSTORE_ROOT,
                              VM_EVT_CHN_BOUND_KEY,
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

    // 2. Watch Client Id XenStore Key
    rc = register_xenbus_watch( &mw_xen_client_id_watch );
    if (rc)
    {
        pr_err("Failed to set client id watcher\n");
        goto ErrorExit;
    }

    g_mwxen_state.client_id_watch_active = true;

    // 3. Watch for our port being bound
    rc = register_xenbus_watch( &mw_xen_evtchn_bound_watch );
    if (rc)
    {
        pr_err("Failed to set client local port watcher\n");
        goto ErrorExit;
    }

    g_mwxen_state.evtchn_bound_watch_active = true;

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
            pr_debug("Ending access to grant ref 0x%x\n", g_mwxen_state.grant_refs[i]);
            gnttab_end_foreign_access_ref( g_mwxen_state.grant_refs[i], 0 );
        }
    }

    if ( g_mwxen_state.evtchn_bound_watch_active )
    {
        unregister_xenbus_watch( &mw_xen_evtchn_bound_watch );
    }

    if ( g_mwxen_state.client_id_watch_active )
    {
        unregister_xenbus_watch( &mw_xen_client_id_watch );
    }

    mw_xen_initialize_keys();

    if ( g_mwxen_state.irq )
    {
        unbind_from_irqhandler( g_mwxen_state.irq, NULL );
    }

    if ( !mw_xen_is_evt_chn_closed() )
    {
        mw_xen_free_unbound_evt_chn();
    }
}


domid_t
mw_xen_get_local_domid( void )
{
    return g_mwxen_state.my_domid;
}
