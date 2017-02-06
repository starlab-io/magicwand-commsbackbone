/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

/**
 * @file    char_driver.c
 * @author  Mark Mason 
 * @date    10 September 2016
 * @version 0.1
 * @brief   A character driver to support comms.
 */

/**
 * This is the Magic Wand driver for the protected virtual machine
 * (PVM). It supports multithreading/multiprocessing - e.g a
 * multi-threaded application above it can read/write to its device,
 * and each request (write) can expect to receive the corresponding
 * response (from read).
 *
 * The driver supports a handshake with another Xen virtual machine,
 * the unikernel agent (implemented on the Rump unikernel). The
 * handshake involves discovering each others' domain IDs, event
 * channels, and sharing memory via grant references.
 *
 * The driver supports the usage of an underlying Xen ring buffer. The
 * driver writes requests on to the ring buffer, and reads responses
 * off the ring buffer. The driver cannot assume that a response
 * following a request will correspond to that last request.
 *
 * The multithreading support works as follows:
 *
 * (1) A user-mode program writes a request to the driver. The driver
 *     assigns an ID to that request and associates the caller's PID
 *     with the request ID via a connection mapping.
 *
 * (2) The user-mode program reads a response from the driver. The
 *     driver will cause that read() to block until it receives the
 *     response with an ID that matches the ID of the request sent by
 *     that program. This is implemented by a kernel thread that runs
 *     the consume_response_worker() function.
 *
 * This model implies some strict standards:
 *  - The remote side *must* send a response for every request it receives
 *  - The programs that use this driver must be well-written: they must
 *    always read a response for every request they write.
 */


#define  DEVICE_NAME "mwchar"    // The device will appear at /dev/mwchar using this value
#define  CLASS_NAME  "mw"        // The device class -- this is a character device driver

#define pr_fmt(fmt)                             \
    DEVICE_NAME "P%d (%s) " fmt, current->pid, __func__

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

#include <message_types.h>
#include <xen_keystore_defs.h>

// Record performance metrics?
#define MW_DO_PERFORMANCE_METRICS 0

// down() blocks and triggers kernel warning message (good for debugging)
//
// down_interruptible() blocks until interrupt and does not trigger message
//#define SEM_DOWN(x) down((x))
#define SEM_DOWN(x) down_interruptible((x))


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark Mason");
MODULE_DESCRIPTION("A driver to support MagicWand's INS");
MODULE_VERSION("0.2");

//
// Used to map a thread to the (single) currently-outstanding
// request. Assumption: no async IO!!
//
typedef struct _thread_conn_map {
    mt_id_t message_id;
    
    pid_t pid;
    pid_t tgid; // thread group
    struct task_struct * task;

    bool awaiting_read;
    
    // Worker thread writes the response here upone receipt, then
    // releases data_pending
    mt_response_generic_t response;
    
    // Use semaphore so we can lock/unlock in different process' contexts
    struct semaphore data_pending;
    struct list_head list;
} thread_conn_map_t;


static int    majorNumber = -1;             //  Device number -- determined automatically
static int    numberOpens = 0;              //  Counts the number of times the device is opened
static struct class*  mwcharClass  = NULL;  //  The device-driver class struct pointer
static struct device* mwcharDevice = NULL;  //  The device-driver device struct pointer

static grant_ref_t   foreign_grant_ref;
static unsigned int  msg_counter;

static bool  client_id_watch_active;
static bool  evtchn_bound_watch_active;

static domid_t       client_dom_id;
static domid_t       srvr_dom_id;
static int           common_event_channel;

// For allocating, tracking, and sharing XENEVENT_GRANT_REF_COUNT
// pages.
static uint8_t      *server_region;
static size_t        server_region_size;
static grant_ref_t   grant_refs[ XENEVENT_GRANT_REF_COUNT ];

static int          irqs_handled = 0;
static int          irq = 0;

#if MW_DO_PERFORMANCE_METRICS

// Vars for performance tests
ktime_t start, end;
s64 actual_time;

#endif // MW_DO_PERFORMANCE_METRICS

static struct semaphore event_channel_sem;

// Enforce only one writer to ring buffer at a time
static struct mutex mw_req_mutex;

//static struct mutex mw_res_mutex;

static struct list_head    thread_conn_head;
static struct rw_semaphore thread_conn_lock;
static struct completion   ring_ready;
// Only this single thread reads from the ring buffer
static struct task_struct * worker_thread;
static bool pending_exit = false;

// Defines:
// mwevent_sring_t
// mwevent_front_ring_t
// mwevent_back_ring_t
DEFINE_RING_TYPES( mwevent, mt_request_generic_t, mt_response_generic_t );

struct mwevent_sring *shared_ring = NULL;
struct mwevent_front_ring front_ring;
size_t shared_mem_size = PAGE_SIZE * XENEVENT_GRANT_REF_COUNT;


// The prototype functions for the character driver
static int     dev_open(struct inode *, struct file *);
static int     dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);

//static void __exit mwchar_exit(void);
static void mwchar_exit(void);

// Tracks the current request ID. Corresponds to mt_request_base_t.id
static mt_id_t
get_next_id( void )
{
    static atomic64_t counter = ATOMIC64_INIT( 0 );

    return (mt_id_t) atomic64_inc_return( &counter );
}


/**
 * @brief Helper function to find or create socket/pid mapping.
 *
 * Mappins are keyed by PID, not by thread group. This way there can
 * be one mapping per thread.
 */
static thread_conn_map_t *
get_thread_conn_map_by_task( struct task_struct * task,
                             bool create )
{
    thread_conn_map_t * curr = NULL;

    down_read( &thread_conn_lock );
    
    list_for_each_entry( curr, &thread_conn_head, list )
    {
        if ( task->pid == curr->pid )
        {
            up_read( &thread_conn_lock );
            goto ErrorExit;
        }
    } // for

    // Not found; create a new one
    up_read( &thread_conn_lock );

    if ( !create )
    {
        curr = NULL;
        goto ErrorExit;
    }
    
    // XXXX: use SLAB instead
    curr = kmalloc( sizeof(thread_conn_map_t),
                    GFP_KERNEL | __GFP_ZERO );
    if ( NULL == curr )
    {
        pr_err( "kmalloc failed\n" );
        goto ErrorExit;
    }

    curr->pid  = task->pid;
    curr->tgid = task->tgid;
    curr->task = task;

    curr->awaiting_read = false;
    
    sema_init( &curr->data_pending, 0 );

    // Modify the global list
    down_write( &thread_conn_lock );
    list_add( &curr->list, &thread_conn_head );
    up_write( &thread_conn_lock );

ErrorExit:
    return curr;
}

static thread_conn_map_t *
find_thread_conn_map_by_message_id( mt_id_t id )
{
    thread_conn_map_t * curr = NULL;
    
    down_read( &thread_conn_lock );
    
    list_for_each_entry( curr, &thread_conn_head, list )
    {
        if ( id == curr->message_id )
        {
            goto ErrorExit;
        }
    } // for

    // Not found
    curr = NULL;

ErrorExit:
    up_read( &thread_conn_lock );
    return curr;
}


// Caller must hold thread_conn_lock for write

static void
destroy_thread_conn_map( thread_conn_map_t ** tcmap )
{
    // Release the waiter
    up( &( (*tcmap)->data_pending ) );
    
    pr_debug( "Removing mapping for PID %d TG %d (last message ID 0x%llx)\n",
              (*tcmap)->pid, (*tcmap)->tgid, (*tcmap)->message_id );

    list_del( &( (*tcmap)->list ) );

    kfree( *tcmap );
    *tcmap = NULL;
}


/** @brief Set the callback functions for the file_operations struct
 */
static struct file_operations fops =
{
   .open    = dev_open,
   .read    = dev_read,
   .write   = dev_write,
   .release = dev_release,
};

static int write_to_key(const char * dir, const char * node, const char * value)
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
read_from_key( const char * dir, const char * node )
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

static int write_server_id_to_key(void) 
{
   const char  *dom_id_str;
   int err = 0;
   
   // Get my domain id
   dom_id_str = (const char *)read_from_key( PRIVATE_ID_PATH, "" );

   if (!dom_id_str) {
       pr_err("Error: Failed to read my Dom Id Key\n");
       err = -EIO;
       goto ErrorExit;
   }

   pr_debug("Read my Dom Id Key: %s\n", dom_id_str);

   err = write_to_key( XENEVENT_XENSTORE_ROOT, SERVER_ID_KEY, dom_id_str );
   if ( err )
   {
       goto ErrorExit;
   }

   srvr_dom_id = simple_strtol(dom_id_str, NULL, 10);

ErrorExit:
   if ( dom_id_str )
   {
       kfree(dom_id_str);
   }
   return err;
}

static int create_unbound_evt_chn(void) 
{
   struct evtchn_alloc_unbound alloc_unbound; 
   char                        common_event_channel_str[MAX_GNT_REF_WIDTH]; 
   int                         err = 0;

   if ( !client_dom_id )
   {
       goto ErrorExit;
   }

   //alloc_unbound.dom = DOMID_SELF;
   alloc_unbound.dom = srvr_dom_id;
   alloc_unbound.remote_dom = client_dom_id; 

   err = HYPERVISOR_event_channel_op(EVTCHNOP_alloc_unbound, &alloc_unbound);
   if (err) {
       pr_err("Failed to set up event channel\n");
       goto ErrorExit;
   }
   
   pr_debug("Event Channel Port (%d <=> %d): %d\n",
          alloc_unbound.dom, client_dom_id, alloc_unbound.port);

   common_event_channel = alloc_unbound.port;

   pr_debug("Event channel's local port: %d\n", common_event_channel );
   memset(common_event_channel_str, 0, MAX_GNT_REF_WIDTH);
   snprintf(common_event_channel_str, MAX_GNT_REF_WIDTH, "%u", common_event_channel);

   err = write_to_key( XENEVENT_XENSTORE_ROOT,
                       VM_EVT_CHN_PORT_KEY,
                       common_event_channel_str );

ErrorExit:
   return err;
}

static void send_evt(int evtchn_prt)
{
   struct evtchn_send send;

   send.port = evtchn_prt;

   //xen_clear_irq_pending(irq);

   if (HYPERVISOR_event_channel_op(EVTCHNOP_send, &send)) {
      pr_err("Failed to send event\n");
   } /*else {
      pr_info("Sent Event. Port: %u\n", send.port);
   }*/
}

static int 
is_evt_chn_closed(void)
{
   struct evtchn_status status;
   int                  rc;

   status.dom = DOMID_SELF;
   status.port = common_event_channel;

   rc = HYPERVISOR_event_channel_op(EVTCHNOP_status, &status); 

   if (rc < 0)
     return 1;

   if (status.status != EVTCHNSTAT_closed)
      return 0;

   return 1;
}

static void 
free_unbound_evt_chn(void)
{

   struct evtchn_close close;
   int err;

   if (!common_event_channel)
      return;
      
   close.port = common_event_channel;

   err = HYPERVISOR_event_channel_op(EVTCHNOP_close, &close);

   if (err) {
      pr_err("Failed to close event channel\n");
   } else {
      pr_debug("Closed Event Channel Port: %d\n", close.port);
   }
}

static int
consume_response_worker( void * Data )
{
    int rc = 0;
    bool available = false;
    mt_response_generic_t * response = NULL;
    thread_conn_map_t * conn = NULL;

    pr_debug( "Worker thread spawned in process %d...\n",
             current->pid );
    do
    {
        rc = wait_for_completion_timeout( &ring_ready, 0x10 );
        if ( rc > 0 )
        {
            break;
        }
        if ( pending_exit )
        {
            goto ErrorExit;
        }
    } while ( true );

    pr_debug( "Waiting for responses on the ring buffer in process %d...\n",
             current->pid );

    while( true )
    {
        do
        {
            available = RING_HAS_UNCONSUMED_RESPONSES(&front_ring);
            if ( pending_exit )
            {
                pr_info( "Detected pending exit request\n" );
                goto ErrorExit;
            }

            if ( !available )
            {
                // must be smarter -- infinite wait is bad
                SEM_DOWN(&event_channel_sem);
            }
        } while (!available);

        // An item is available. Consume it. Policy: continue upon
        // error.
        response = (mt_response_generic_t *)
            RING_GET_RESPONSE(&front_ring, front_ring.rsp_cons);

        if ( !MT_IS_RESPONSE( response ) )
        {
            // Fatal: The ring is corrupted.
            pr_crit( "Received data that is not a response\n" );
            rc = -EIO;
            goto ErrorExit;
        }

        pr_debug( "Response ID %lx size %x on ring at idx %d\n",
                  (unsigned long)response->base.id,
                  response->base.size,
                  front_ring.rsp_cons );

        // Hereafter: Advance index as soon as we're done with the
        // item in the ring bufer.

        conn = find_thread_conn_map_by_message_id( response->base.id );
        if ( NULL == conn )
        {
            // This can happen if the process dies between issuing a
            // request and receiving the response.
            pr_err( "Received response for unknown request. "
                     "Did the process die?\n" );
            ++front_ring.rsp_cons;
            continue;
        }
        
        // Compare size of response to permitted size.
        if ( response->base.size > sizeof( conn->response ) )
        {
            pr_err( "Received response that is too big\n" );
            // Reduce the size that we send to user and fall through
            response->base.size = sizeof( conn->response );
        }

        // Put the response into the connection map struct
        memcpy( &conn->response, response, response->base.size );
        ++front_ring.rsp_cons;

        up( &conn->data_pending );
    } // while

ErrorExit:
    do_exit( rc );
}

static int
send_request( mt_request_generic_t * Request, mt_size_t RequestSize )
{
    int rc = 0;
    void * dest = NULL;
    thread_conn_map_t * connmap = NULL;
    mt_id_t id = get_next_id();
    
    pr_debug( "Attempting to send request %lx\n", (unsigned long)id );

    // Hold the lock for the duration of this function. All of these
    // must be done with no interleaving:
    //
    // * Write the request to the ring buffer
    // * Map the requesting process to the request, to match with the response
    // * Flush our private indices to the shared ring

    mutex_lock(&mw_req_mutex);

    // Now get this task's mapping to associate the message with the 
    connmap = get_thread_conn_map_by_task( current, true );
    if ( NULL == connmap )
    {
        rc = -ENOMEM;
        goto ErrorExit;
    }

    // The semaphore 'data_pending' starts off locked. read_request()
    // attempts to acquire it and blocks until the worker thread
    // releases it upon receipt of a response.

    // Map this process' connection map to this message ID
    connmap->message_id = id;

    // If we are awaiting a read, then we cannot write!
    if ( connmap->awaiting_read )
    {
        pr_err( "Caller already called write() and must call read() next\n" );
        rc = -EPERM;
        goto ErrorExit;
    }

    if ( RING_FULL(&front_ring) )
    {
        pr_alert("Front ring is full\n");
        rc = -EAGAIN;
        goto ErrorExit;
    }

    dest = RING_GET_REQUEST(&front_ring, front_ring.req_prod_pvt);
    if ( !dest ) 
    {
        pr_err("destination buffer is NULL\n");
        rc = -EIO;
        goto ErrorExit;
    }

    pr_debug( "Sending request %lx to idx %x\n",
              (long int) id, front_ring.req_prod_pvt );

    // Copy only the request's claimed size into the ring buffer
    rc = copy_from_user( dest, Request, RequestSize );
    if ( rc )
    {
        pr_err( "copy_from_user failed: %d\n", rc );
        rc = -EFAULT;
        goto ErrorExit;
    }

    // Update the ID in the ring buffer
    ((mt_request_generic_t *)dest)->base.id = id;

    // Validate the request in the ring buffer ...
    if ( !MT_IS_REQUEST( (mt_request_generic_t *)dest ) )
    {
        pr_err( "Invalid request given\n" );
        rc = -EINVAL;
        goto ErrorExit;
    }
    
    // We've written to the ring; now we are awaiting a response
    connmap->awaiting_read = true;
    
    // Update the ring buffer only *after* the mapping is in place!
    ++front_ring.req_prod_pvt;
    RING_PUSH_REQUESTS( &front_ring );

    pr_debug("front_ring.req_prod_pvt: %x\n", front_ring.req_prod_pvt);
    
ErrorExit:
#if 0 // Rump is polling and not using the event channel
    if ( 0 == rc )
    {
        // Inform remote side, only if everything went right
        send_evt( common_event_channel );
    }
#endif
    mutex_unlock(&mw_req_mutex);

    return rc;
}

static void
init_shared_ring(void)
{
   if (!server_region)
   {
      pr_err("server_region is NULL\n");
      return;
   }

   shared_ring = (struct mwevent_sring *)server_region;
   SHARED_RING_INIT(shared_ring);
   FRONT_RING_INIT(&front_ring, shared_ring, shared_mem_size);

   complete( &ring_ready );
}

static int 
offer_grant(domid_t domu_client_id) 
{
   int ret = 0;

   server_region_size = PAGE_SIZE * XENEVENT_GRANT_REF_COUNT;

   // Do not use vmalloc() -- the pages can't be accessed on the other DomU
   server_region = (uint8_t *) __get_free_pages( GFP_KERNEL, XENEVENT_GRANT_REF_ORDER );
   if (!server_region)
   {
       pr_err( "Error allocating memory (0x%x pages)\n",
               XENEVENT_GRANT_REF_COUNT);
      return 1;
   }

   // Zero out the entire shared region. This validates each page.
   memset(server_region, 0, server_region_size );

   for ( int i = 0; i < XENEVENT_GRANT_REF_COUNT; i++ )
   {
       // Calc the VA, then find the backing pseudo-physical address
       // (Xen book pg 61)
       void * va = (void *) (server_region + i * PAGE_SIZE);
       unsigned long mfn = virt_to_mfn( va );

       ret = gnttab_grant_foreign_access(domu_client_id, mfn, 0);
       if (ret < 0) {
           pr_err("Error obtaining Grant\n");
           return 1;
       }

       grant_refs[ i ] = ret;
       //pr_info("VA: %p MFN: %p grant 0x%x\n", va, (void *)mfn, ret);
   }

   return 0;
}


static int write_grant_refs_to_key( void )
{
    // Must be large enough for one grant ref, in hex, plus '\0'
    char one_ref[5];
    char gnt_refs[ XENEVENT_GRANT_REF_COUNT * sizeof(one_ref) ] = {0};
    int rc = 0;

    for ( int i = 0; i < XENEVENT_GRANT_REF_COUNT; i++ )
    {
        if ( snprintf( one_ref, sizeof(one_ref), "%x ", grant_refs[i] ) >= sizeof(one_ref))
        {
            pr_err("Insufficient space to write grant ref.\n");
            rc = 1;
            goto ErrorExit;
        }
        if (strncat( gnt_refs, one_ref, sizeof(gnt_refs) ) >= gnt_refs + sizeof(gnt_refs) )
        {
            pr_err("Insufficient space to write all grant refs.\n");
            rc = 2;
            goto ErrorExit;
        }
    }

    rc = write_to_key( XENEVENT_XENSTORE_ROOT, GNT_REF_KEY, gnt_refs );

ErrorExit:
    return rc;
}


static void
client_id_state_changed( struct xenbus_watch *w,
                         const char **v,
                         unsigned int l )
{
    char     *client_id_str = NULL;
    int       err = 0;

    client_id_str = (char *)read_from_key( XENEVENT_XENSTORE_ROOT,
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

    client_dom_id = simple_strtol(client_id_str, NULL, 10);

    pr_debug("\t\tuint form: %u\n", client_dom_id);

    kfree(client_id_str);

    // Create unbound event channel with client
    err = create_unbound_evt_chn();
    if ( err ) return;
   
    // Offer Grant to Client  
    offer_grant((domid_t)client_dom_id);

    // Reset Client Id Xenstore Key
    err = write_to_key( XENEVENT_XENSTORE_ROOT, CLIENT_ID_KEY, KEY_RESET_VAL );
    if ( err )
    {
        // XXXX: There's more cleanup if we failed here
        return;
    }

    // Write Grant Ref to key 
    err = write_grant_refs_to_key();
    if ( err )
    {
        return;
    }

    init_shared_ring();
}

static struct xenbus_watch client_id_watch = {

    .node = CLIENT_ID_PATH,
    .callback = client_id_state_changed
};

static irqreturn_t irq_event_handler( int port, void * data )
{
    //unsigned long flags;
    //local_irq_save( flags );
   
    ++irqs_handled;

    //xen_clear_irq_pending(irq);
    //local_irq_restore( flags );

    up(&event_channel_sem);

    return IRQ_HANDLED;
}

static void vm_port_is_bound(struct xenbus_watch *w,
                             const char **v,
                             unsigned int l)
{
   char     *is_bound_str; 
   int       i;

   pr_debug("Checking whether %s is asserted\n", VM_EVT_CHN_BOUND_PATH);

   is_bound_str = (char *) read_from_key( XENEVENT_XENSTORE_ROOT,
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

   irq = bind_evtchn_to_irqhandler( common_event_channel,
                                    irq_event_handler,
                                    0, NULL, NULL );

   pr_debug( "Bound event channel %d to irq: %d\n",
             common_event_channel, irq );

   for ( i = 0; i < 1; i++ )
   {
       pr_debug( "NOT Sending event via IRQ %d\n", irq );
   }
}

static struct xenbus_watch evtchn_bound_watch = {

    .node = VM_EVT_CHN_BOUND_PATH,
    .callback = vm_port_is_bound
};

static int initialize_keys(void)
{
    int rc = 0;
    
    rc = write_to_key( XENEVENT_XENSTORE_ROOT,
                       CLIENT_ID_KEY,
                       KEY_RESET_VAL );
    if ( rc ) goto ErrorExit;
    
    rc = write_to_key( XENEVENT_XENSTORE_ROOT,
                       SERVER_ID_KEY,
                       KEY_RESET_VAL );
    if ( rc ) goto ErrorExit;
    
    rc = write_to_key( XENEVENT_XENSTORE_ROOT,
                       GNT_REF_KEY,
                       KEY_RESET_VAL );
    if ( rc ) goto ErrorExit;
    
    rc = write_to_key( XENEVENT_XENSTORE_ROOT,
                       VM_EVT_CHN_PORT_KEY,
                       KEY_RESET_VAL );
    if ( rc ) goto ErrorExit;
    
    rc = write_to_key( XENEVENT_XENSTORE_ROOT,
                       VM_EVT_CHN_BOUND_KEY,
                       KEY_RESET_VAL );
    if ( rc ) goto ErrorExit;

ErrorExit:
    return rc;
}

/** @brief The LKM initialization function
 *  @return returns 0 if successful
 */
static int __init
mwchar_init(void)
{
   int   err;

   foreign_grant_ref = 0;
   server_region = NULL;
   msg_counter = 0;
   client_dom_id = 0;
   common_event_channel = 0;
   
   client_id_watch_active = false;
   evtchn_bound_watch_active = false;

#if MW_DO_PERFORMANCE_METRICS
   start = ktime_set(0,0);;
   end = ktime_set(0,0);;
#endif // MW_DO_PERFORMANCE_METRICS
   pr_debug("Initializing\n");

#if 0
   struct module * mod = (struct module *) THIS_MODULE;
   // gdb> add-symbol-file char_driver.ko $eax/$rax
   asm( "int $3" // module base in *ax
        //:: "a" ((THIS_MODULE)->module_core));
        :: "a" ((THIS_MODULE)->init_layout.base)
        , "c" (mod) );
#endif
   // Set up the structs for the thread-connection mapping
   INIT_LIST_HEAD( &thread_conn_head );
   init_rwsem( &thread_conn_lock );
   init_completion( &ring_ready );
   
   sema_init( &event_channel_sem, 0 );
   mutex_init( &mw_req_mutex );

   // Create worker thread that reads items from the ring buffer
   worker_thread = kthread_run( &consume_response_worker,
                                NULL,
                                "MwMsgConsumer" );
   if ( NULL == worker_thread )
   {
       pr_err( "kthread_run() failed\n" );
       err = -ESRCH;
       goto ErrorExit;
   }

   pr_info("Created process %d to consume messages on the ring buffer\n",
           worker_thread->pid);

   // Try to dynamically allocate a major number for the device --
   // more difficult but worth it
   majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
   if (majorNumber < 0)
   {
       err = majorNumber;
       pr_err( "register_chrdev failed: %d\n", err );
       goto ErrorExit;
   }

   // Register the device class
   mwcharClass = class_create(THIS_MODULE, CLASS_NAME);
   if (IS_ERR(mwcharClass))
   {
       err = PTR_ERR(mwcharClass);
       pr_err( "class_create failed: %d\n", err );
       goto ErrorExit;
   }

   // Register the device driver
   mwcharDevice = device_create(mwcharClass,
                                NULL,
                                MKDEV(majorNumber, 0),
                                NULL,
                                DEVICE_NAME);
   if (IS_ERR(mwcharDevice))
   {
      err = PTR_ERR(mwcharDevice);
      pr_err( "device_create failed: %d\n", err );
      goto ErrorExit;
   }

   // Set all protocol keys to zero
   err = initialize_keys();
   if ( err )
   {
       pr_err("Key initialization failed: %d\n", err );
       goto ErrorExit;
   }
   
   // 1. Write Dom Id for Server to Key
   err = write_server_id_to_key();
   if ( err )
   {
       goto ErrorExit;
   }

   // 2. Watch Client Id XenStore Key
   err = register_xenbus_watch(&client_id_watch);
   if (err)
   {
      pr_err("Failed to set client id watcher\n");
   }
   else
   {
      client_id_watch_active = true;
   }

   // 3. Watch for our port being bound
   err = register_xenbus_watch(&evtchn_bound_watch);
   if (err)
   {
      pr_err("Failed to set client local port watcher\n");
   }
   else
   {
      evtchn_bound_watch_active = true;
   }

ErrorExit:
   if ( err )
   {
       mwchar_exit();
   }
   return err;
}

/** @brief The driver cleanup function
 */
static void mwchar_exit(void)
{
    thread_conn_map_t * curr = NULL;
    thread_conn_map_t * next = NULL;

    pending_exit = true;

    if ( NULL != worker_thread )
    {
        // Kick the thread to make it resume
        up( &event_channel_sem );
        
        kthread_stop( worker_thread );
        worker_thread = NULL;
    }
    
    if ( majorNumber >= 0 )
    {
        device_destroy(mwcharClass, MKDEV(majorNumber, 0)); // remove the device
    }

    if ( NULL != mwcharClass )
    {
        class_destroy(mwcharClass); // remove the device class
        class_unregister(mwcharClass); // unregister the device class
    }
    
    if ( NULL != mwcharDevice )
    {
        unregister_chrdev(majorNumber, DEVICE_NAME); // unregister the major number
    }

   for ( int i = 0; i < XENEVENT_GRANT_REF_COUNT; i++ )
   {
       if ( 0 != grant_refs[ i ] )
       {
           pr_debug("Ending access to grant ref 0x%x\n", grant_refs[i]);
           gnttab_end_foreign_access_ref( grant_refs[i], 0 );
       }
   }

   if ( NULL != server_region )
   {
       free_pages( (unsigned long) server_region, XENEVENT_GRANT_REF_ORDER );
   }

   if (client_id_watch_active)
   {
      unregister_xenbus_watch(&client_id_watch);
   }

   if (evtchn_bound_watch_active)
   {
      unregister_xenbus_watch(&evtchn_bound_watch);
   }

   initialize_keys();

   if (irq)
   {
      unbind_from_irqhandler(irq, NULL);
   }

   if (!is_evt_chn_closed())
   {
      free_unbound_evt_chn();
   }

   mutex_destroy( &mw_req_mutex );

   // Destroy all the remaining mappings. Lock the list.
   down_write( &thread_conn_lock );

   list_for_each_entry_safe( curr, next, &thread_conn_head, list )
   {
       destroy_thread_conn_map( &curr );
   }

   up_write( &thread_conn_lock );

   pr_debug("cleanup is complete\n");
}

/** @brief The device open function that is called each time the device is opened
 *  This will only increment the numberOpens counter in this case.
 *  @param inodep A pointer to an inode object (defined in linux/fs.h)
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 */
static int
dev_open(struct inode *inodep, struct file *filep)
{
   numberOpens++;
   pr_debug("Device has been opened %d time(s)\n", numberOpens);
   return 0;
}

/** @brief This function is called whenever device is being read from user space i.e. data is
 *  being sent from the device to the user. In this case it uses the copy_to_user() function to
 *  send the buffer to the user and captures any errors.
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 *  @param buffer The pointer to the buffer to which this function writes the data
 *  @param len The length of the buffer
 *  @param offset The offset if required
 */
static ssize_t
dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset)
{
   int rc = 0; 
   thread_conn_map_t * connmap = NULL;
   mt_size_t resp_size = 0;
   
   connmap = get_thread_conn_map_by_task( current, false );
   if ( NULL == connmap )
   {
       pr_err( "read() called from thread that hasn't issued write()\n" );
       rc = -EPERM;
       goto ErrorExit;
   }

   pr_debug( "Waiting for response %lx to arrive\n",
             (unsigned long) connmap->message_id );
   
   // Now we have the mapping for this thread. Wait for data to
   // arrive. This may block the calling process.

   if ( down_interruptible( &connmap->data_pending ) )
   {
       // Signal encountered, semaphore not acquired. The caller must try again.
       pr_err( "read() was interrupted\n" );
       rc = -EINTR;
       goto ErrorExit;
   }

   if ( !connmap->awaiting_read )
   {
       pr_err( "Caller hasn't called write() prior to this read()\n" );
       rc = -EPERM;
       goto ErrorExit;
   }

   connmap->awaiting_read = false;
   
   resp_size = connmap->response.base.size;

   pr_debug( "Response %lx (size %x) has arrived\n",
             (unsigned long) connmap->message_id,
             resp_size );

   if ( pending_exit )
   {
       pr_err( "Driver is unloading\n" );
       rc = -EINTR;
       goto ErrorExit;
   }
   
   // An item is available and is in connmap. Process it.
   if ( len < resp_size )
   {
       pr_err( "User buffer too small for response.\n" );
       rc = -EINVAL;
       goto ErrorExit;
   }
   
   // Data has been received. Write it to the user buffer.
   rc = copy_to_user( buffer,
                      &connmap->response,
                      resp_size );
   if ( rc )
   {
       pr_err( "copy_to_user() failed: %d\n", rc );
       rc = -EFAULT;
       goto ErrorExit;
   }

   // Success
   rc = resp_size;

ErrorExit:
   pr_debug( "Returning %d\n", rc );
   return rc;
}

/** @brief Writes a request to the shared ring buffer.
 *
 *  @param filep A pointer to a file object
 *  @param buffer The buffer to that contains the string to write to the device
 *  @param len The length of the array of data that is being passed in the const char buffer
 *  @param offset The offset if required
 */
static ssize_t 
dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset)
{
    int rc = 0;
    mt_request_generic_t *req = (mt_request_generic_t *)buffer;
    mt_size_t req_size = 0;
    
#if MW_DO_PERFORMANCE_METRICS
    int effective_number;
    if (ktime_to_ns(start) == 0)
    {
        start = ktime_get();
    }
    else
    {
        end = ktime_get();
        actual_time = ktime_to_ns(ktime_sub(end, start));
        effective_number = (irqs_handled + 1)/2;
        pr_debug("%d     %lld\n", effective_number, actual_time);
        start = ktime_set(0,0);
    }
#endif // MW_DO_PERFORMANCE_METRICS

    rc = get_user( req_size, &req->base.size );
    if ( rc )
    {
        pr_err( "get_user failed: %d\n", rc );
        rc = -EFAULT;
        goto ErrorExit;
    }
    
    if ( req_size > len || 0 == req_size )
    {
        pr_err( "Received request with invalid size\n" );
        rc = -EINVAL;
        goto ErrorExit;
    }

    rc = send_request( req, req_size );
    if ( rc )
    {
        goto ErrorExit;
    }

    // Success
    rc = req_size;

#if MW_DO_PERFORMANCE_METRICS
    end = ktime_get();

    actual_time = ktime_to_ns(ktime_sub(end, start));
    pr_info("Time taken for send_request() execution (ns): %lld\n",
            actual_time);

    actual_time = ktime_to_ms(ktime_sub(end, start));

    pr_info("Time taken for send_request() execution (ms): %lld\n",
            actual_time);
#endif // MW_DO_PERFORMANCE_METRICS
    
ErrorExit:
    return rc;
}

/** @brief Handle close() request.
 *
 *  @param inodep A pointer to an inode object (defined in linux/fs.h)
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 */
static int dev_release(struct inode *inodep, struct file *filep)
{
    thread_conn_map_t * currtc = NULL;
    thread_conn_map_t * nexttc = NULL;
    
    // Release all the mappings associated with current's thread
    // group.
    
    down_write( &thread_conn_lock );

    list_for_each_entry_safe( currtc, nexttc, &thread_conn_head, list )
    {
        if ( currtc->tgid == current->tgid )
        {
            destroy_thread_conn_map( &currtc );
        }
    }

    up_write( &thread_conn_lock );

    pr_debug("Device successfully closed\n");

    return 0;
}

/** @brief A module must use the module_init() module_exit() macros
 *  from linux/init.h, which identify the initialization function at
 *  insertion time and the cleanup function (as listed above)
 */
module_init(mwchar_init);
module_exit(mwchar_exit);
