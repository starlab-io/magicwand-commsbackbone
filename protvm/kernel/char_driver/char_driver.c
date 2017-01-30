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

#define  DEVICE_NAME "mwchar"    // The device will appear at /dev/mwchar using this value
#define  CLASS_NAME  "mw"        // The device class -- this is a character device driver

#define pr_fmt(fmt)                             \
    DEVICE_NAME " (%s) " fmt, __func__

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


// XXXX: ue definitions from xen_keystore_defs.h and remove these ones

#define ROOT_NODE             "/unikernel/random"
#define SERVER_ID_KEY         "server_id" 
#define CLIENT_ID_KEY         "client_id" 
#define GNT_REF_KEY           "gnt_ref" 

#define MAX_GNT_REF_WIDTH     15
#define MSG_LEN_KEY           "msg_len"
#define PRIVATE_ID_PATH       "domid"

#define VM_EVT_CHN_PATH       "vm_evt_chn_prt"
#define VM_EVT_CHN_BOUND      "vm_evt_chn_is_bound"

#define KEY_RESET_VAL      "0"
#define TEST_MSG_SZ         64
#define TEST_MSG_SZ_STR    "64"
#define TEST_MSG           "The abyssal plain is flat.\n"
#define MAX_MSG             1

//#define XENEVENT_GRANT_REF_ORDER  6 //(2^order == page count)
//#define XENEVENT_GRANT_REF_ORDER  1 //(2^order == page count)
//#define XENEVENT_GRANT_REF_COUNT (1 << XENEVENT_GRANT_REF_ORDER)
//#define XENEVENT_GRANT_REF_DELIM " "


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark Mason");
MODULE_DESCRIPTION("A driver to support MagicWand's INS");
MODULE_VERSION("0.2");

//
// Used to map a thread to a connection that it manages. One thread
// can manage multiple connections.
//
typedef struct _thread_conn_map {
    //int sockfd;
    mt_id_t message_id;
    
    pid_t pid;
    struct task_struct * task;

    // Worker thread writes the response here upone receipt, then
    // releases data_pending
    mt_response_generic_t response;
    
    struct mutex data_pending;
    struct list_head list;
} thread_conn_map_t;


static int    majorNumber = -1;             //  Device number -- determined automatically
static int    numberOpens = 0;              //  Counts the number of times the device is opened
static struct class*  mwcharClass  = NULL;  //  The device-driver class struct pointer
static struct device* mwcharDevice = NULL;  //  The device-driver device struct pointer

static grant_ref_t   foreign_grant_ref;
static unsigned int  msg_counter;

static unsigned int  is_msg_len_watch;
static unsigned int  is_client_id_watch;
static unsigned int  is_evtchn_bound_watch;

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

// Vars for performance tests
ktime_t start, end;
s64 actual_time;

static struct semaphore mw_sem;

// Enforce only one writer to ring buffer at a time
static struct mutex mw_req_mutex;

//static struct mutex mw_res_mutex;

static struct list_head    thread_conn_head;
static struct rw_semaphore thread_conn_lock;
static struct completion   ring_ready;
// Only this single thread reads from the ring buffer
static struct task_struct * worker_thread;


// Defines:
// mwevent_sring_t
// mwevent_front_ring_t
// mwevent_back_ring_t
DEFINE_RING_TYPES( mwevent, mt_request_generic_t, mt_response_generic_t );

struct mwevent_sring *shared_ring = NULL;
struct mwevent_front_ring front_ring;
size_t shared_mem_size = PAGE_SIZE * XENEVENT_GRANT_REF_COUNT;

// Tracks the current request ID. Corresponds to mt_request_base_t.id
static atomic64_t request_counter = ATOMIC64_INIT( 0 );

// The prototype functions for the character driver
static int     dev_open(struct inode *, struct file *);
static int     dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);

//static void __exit mwchar_exit(void);
static void mwchar_exit(void);


/**
 * @brief Helper function to find or create socket/pid mapping.
 */
static thread_conn_map_t *
get_thread_conn_map_by_pid( pid_t pid )
{
    thread_conn_map_t * curr = NULL;

    down_read( &thread_conn_lock );
    
    list_for_each_entry( curr, &thread_conn_head, list )
    {
        if ( pid == curr->pid )
        {
            up_read( &thread_conn_lock );
            goto ErrorExit;
        }
    } // for

    // Not found; create a new one
    up_read( &thread_conn_lock );

    // XXXX: use SLAB instead ?
    curr = kmalloc( sizeof(thread_conn_map_t),
                    GFP_KERNEL | __GFP_ZERO );
    if ( NULL == curr )
    {
        pr_crit( "kmalloc failed\n" );
        goto ErrorExit;
    }

    curr->pid = pid;
    mutex_init( &curr->data_pending );

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


static void
destroy_thread_conn_map( thread_conn_map_t ** tcmap )
{
    down_write( &thread_conn_lock );

    mutex_destroy( &( (*tcmap)->data_pending ) );
    list_del( &( (*tcmap)->list ) );

    up_write( &thread_conn_lock );
    
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

static int write_to_key(const char *path, const char *value)
{
   struct xenbus_transaction   txn;
   int                         err;

   err = xenbus_transaction_start(&txn);
   if (err) {
       pr_info("Error starting xenbus transaction\n");
       goto ErrorExit;
   }

   err = xenbus_write(txn, ROOT_NODE, path, value);
   if (err) {
      pr_info("Could not write to XenStore Key\n");
      xenbus_transaction_end(txn,1);
      err = -EIO; // mask the original error
      goto ErrorExit;
   }

   err = xenbus_transaction_end(txn, 0);
   if ( err ) {
       pr_info("Failed to end transaction\n");
   }
   
ErrorExit:
   return err;
}

static char *
read_from_key(const char *path)
{
   struct xenbus_transaction   txn;
   char                       *str;
   int                         err;

   err = xenbus_transaction_start(&txn);
   if (err) {
      pr_info("Error starting xenbus transaction\n");
      return NULL;
   }

   if (path) {
      str = (char *)xenbus_read(txn, ROOT_NODE, path, NULL);
   } else {
      str = (char *)xenbus_read(txn, PRIVATE_ID_PATH, "", NULL);
   }

   if (XENBUS_IS_ERR_READ(str)) {
      pr_info("Could not read XenStore Key\n");
      xenbus_transaction_end(txn,1);
      return NULL;
   }

   err = xenbus_transaction_end(txn, 0);

   return str;
}

static int write_server_id_to_key(void) 
{
   const char  *dom_id_str;
   int err = 0;
   
   // Get my domain id
   dom_id_str = (const char *)read_from_key(NULL);

   if (!dom_id_str) {
       pr_info("Error: Failed to read my Dom Id Key\n");
       err = -EIO;
       goto ErrorExit;
   }

   pr_info("Read my Dom Id Key: %s\n", dom_id_str);

   err = write_to_key(SERVER_ID_KEY, dom_id_str);
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
   
   pr_info("Event Channel Port (%d <=> %d): %d\n",
          alloc_unbound.dom, client_dom_id, alloc_unbound.port);

   common_event_channel = alloc_unbound.port;

   pr_info("Event channel's local port: %d\n", common_event_channel );
   memset(common_event_channel_str, 0, MAX_GNT_REF_WIDTH);
   snprintf(common_event_channel_str, MAX_GNT_REF_WIDTH, "%u", common_event_channel);

   err = write_to_key(VM_EVT_CHN_PATH,common_event_channel_str);

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
      pr_info("Closed Event Channel Port: %d\n", close.port);
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
        if ( rc > 0 ) break;
        pr_debug( "Still waiting...\n" );
    } while ( true );

    pr_debug( "Waiting for responses on the ring buffer in process %d...\n",
             current->pid );
    
//    asm("int $3");
    while( true )
    {
        do
        {
            available = RING_HAS_UNCONSUMED_RESPONSES(&front_ring);

            if ( !available )
            {
                down(&mw_sem);
            }
        } while (!available);

        // An item is available. Consume it. Policy: continue upon
        // error.
        response = (mt_response_generic_t *)
            RING_GET_RESPONSE(&front_ring, front_ring.rsp_cons);

        conn = find_thread_conn_map_by_message_id( response->base.id );
        if ( NULL == conn )
        {
            pr_crit_once( "Received response for unknown request!!\n" );
            continue;
        }
        
        // Compare size of response to permitted size.
        if (response->base.size > sizeof( conn->response ) )
        {
            pr_crit_once( "Received response that is too big" );
            continue;
        }

        // Put the reponse into the map struct and alert the waiting thread
        memcpy( &conn->response, response, response->base.size );
        mutex_unlock( &conn->data_pending );

        // We're done with the item in ring buffer
        ++front_ring.rsp_cons;
    }

//ErrorExit:
    do_exit( rc );
}

#if 0
static int 
receive_response(void *Response, size_t Size, size_t *BytesWritten)
{
   int rc = 0; 
   bool available = false;
   mt_response_generic_t * src = NULL;

   //pr_info("receive_response() called\n");

   //start = ktime_get();

   //pr_info("In receive_response(). Entering while loop ... \n");

//   mutex_lock( &mw_res_mutex );

   do
   {
      available = RING_HAS_UNCONSUMED_RESPONSES(&front_ring);

      if ( !available ) {
         //pr_info("down() called\n");
         down(&mw_sem);
      }
   
   } while (!available);

   //pr_info("front_ring.rsp_cons: %u\n", front_ring.rsp_cons);
   //pr_info("front_ring.sring.rsp_prod: %u\n", front_ring.sring->rsp_prod);
   

   //pr_info("down() called\n");
   //down(&mw_sem);

   //end = ktime_get();

   //actual_time = ktime_to_ns(ktime_sub(end, start));

   //pr_info("Time taken for receive_response() execution (ns): %lld\n",
          //actual_time);

   //pr_info("%d     %lld\n", irqs_handled, actual_time);

   /*
   actual_time = ktime_to_ms(ktime_sub(end, start));

   pr_info("Time taken for receive_response() execution (ms): %lld\n",
          actual_time);

   available = RING_HAS_UNCONSUMED_RESPONSES(&front_ring);

   if ( !available ) {
      pr_info("RING_HAS_UNCONSUMED_RESPONSES() returned false\n");
   }
   else
   {
      pr_info("RING_HAS_UNCONSUMED_RESPONSES() returned true\n");
   }
   */

   src = (mt_response_generic_t *)RING_GET_RESPONSE(&front_ring, front_ring.rsp_cons);

   // Compare size of Response to Size. 
   // If larger, then set rc to EINVAL and go to ErrorExit

   if (src->base.size > Size) {
      rc = EINVAL;
      goto ErrorExit;
   }

   //memcpy(Response, src, src->base.size);
   memcpy(Response, src, sizeof(*src));

   //print_hex_dump(KERN_INFO, "", DUMP_PREFIX_NONE, 16, 1, src, Size, true);

   ++front_ring.rsp_cons;

   //pr_info("front_ring.rsp_cons: %u\n", front_ring.rsp_cons);
   //pr_info("front_ring.sring.rsp_prod: %u\n", front_ring.sring->rsp_prod);

//   mutex_unlock( &mw_res_mutex );


ErrorExit:
   return rc;
}
#endif // 0

static int
send_request(mt_request_generic_t * Request, size_t Size)
{
    int rc = 0;
    bool notify = false;  
    void * dest = NULL;
    thread_conn_map_t * connmap = NULL;
   
    mutex_lock(&mw_req_mutex);

    if ( RING_FULL(&front_ring) )
    {
        pr_alert_once("Front Ring is full\n");
        rc = -EAGAIN;
        goto ErrorExit;
    }

    dest = RING_GET_REQUEST(&front_ring, front_ring.req_prod_pvt);
    if (!dest) 
    {
        pr_info("destination buffer is NULL\n");
        rc = -EIO;
        goto ErrorExit;
    }

    connmap = get_thread_conn_map_by_pid( current->pid );
    if ( NULL == connmap )
    {
        rc = -ENOMEM;
        goto ErrorExit;
    }

    // The mutex starts off locked. read_request() attempts to locks
    // it and blocks until the worker thread unlocks it upon receipt of
    // response.
    mutex_lock( &connmap->data_pending );
    connmap->pid = current->pid;
    connmap->task = current;

    // Map this process' connection map to this message ID
    connmap->message_id = 
        Request->base.id =
        atomic64_inc_return( &request_counter );

    pr_debug( "Sending request %ld\n", (long int)Request->base.id );
    memcpy(dest, Request, Size);

    //print_hex_dump(KERN_INFO, "", DUMP_PREFIX_NONE, 16, 1, dest, Size, true);

    //pr_info("send_request(). Copied %lu bytes to destination buffer\n", Size);

    ++front_ring.req_prod_pvt;
    //pr_info("front_ring.req_prod_pvt: %u\n", front_ring.req_prod_pvt);

    RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&front_ring, notify);

    if (notify || !notify)
    {
        send_evt(common_event_channel);
        //pr_info("send_request(). Sent %lu bytes to UK\n", Size);
        //pr_info("send_request(). notify = %u \n", notify);
    }

ErrorExit:
    mutex_unlock(&mw_req_mutex);
    return rc;
}

static void
init_shared_ring(void)
{
   if (!server_region)
   {
      pr_info("server_region is NULL\n");
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
       pr_info("Error allocating memory (0x%x pages)\n",
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
           pr_info("Error obtaining Grant\n");
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
            pr_info("Insufficient space to write grant ref.\n");
            rc = 1;
            goto ErrorExit;
        }
        if (strncat( gnt_refs, one_ref, sizeof(gnt_refs) ) >= gnt_refs + sizeof(gnt_refs) )
        {
            pr_info("Insufficient space to write all grant refs.\n");
            rc = 2;
            goto ErrorExit;
        }
    }

    rc = write_to_key( GNT_REF_KEY, gnt_refs );

ErrorExit:
    return rc;
}

static void 
msg_len_state_changed(struct xenbus_watch *w,
                      const char **v,
                      unsigned int l)
{
   char *msg_len_str;

   if (msg_counter > MAX_MSG)
      return;

   msg_len_str = (char *)read_from_key(MSG_LEN_KEY);

   if(XENBUS_IS_ERR_READ(msg_len_str)) {
      pr_info("Error reading  Message Length Key!!!\n");
      return;
   }

   if (strcmp(msg_len_str,"0") != 0) {
      kfree(msg_len_str);
      return;
   }

   //
   // Get the message length 
   // 
   pr_info("Message length changed value:\n");
   pr_info("\tRead Message Length Key: %s\n", msg_len_str);

   if (!server_region) {
      kfree(msg_len_str);
      pr_info("Error allocating memory\n");
      return;
   }

   memset(server_region, 0, shared_mem_size);
   memcpy(server_region, TEST_MSG, TEST_MSG_SZ);

   // Write Msg Len to key XXXX: process error code
   (void) write_to_key(MSG_LEN_KEY, TEST_MSG_SZ_STR);

   kfree(msg_len_str);
   msg_counter++;

}

static struct xenbus_watch msg_len_watch = {

    .node = "/unikernel/random/msg_len",
    .callback = msg_len_state_changed
};

static void
client_id_state_changed(struct xenbus_watch *w,
                        const char **v,
                        unsigned int l)
{
    char     *client_id_str = NULL;
    int       err = 0;

    client_id_str = (char *)read_from_key(CLIENT_ID_KEY);

    if (XENBUS_IS_ERR_READ(client_id_str)) {
        pr_info("Error reading client id key!!!\n");
        return;
    }

    if (strcmp(client_id_str,"0") == 0) {
        kfree(client_id_str);
        return;
    }

    //
    // Get the client Id 
    // 
    pr_info("Client Id changed value:\n");
    pr_info("\tRead Client Id Key: %s\n", client_id_str);

    client_dom_id = simple_strtol(client_id_str, NULL, 10);

    pr_info("\t\tuint form: %u\n", client_dom_id);

    kfree(client_id_str);

    // Create unbound event channel with client
    err = create_unbound_evt_chn();
    if ( err ) return;
   
    // Write Msg Len to key

    err = write_to_key(MSG_LEN_KEY, TEST_MSG_SZ_STR);
    if ( err ) return;

    // Offer Grant to Client  
    offer_grant((domid_t)client_dom_id);

    // Reset Client Id Xenstore Key
    err = write_to_key(CLIENT_ID_KEY, KEY_RESET_VAL);
    if ( err )
    {
        // XXXX: There's more cleanup if we failed here
        return;
    }

    // Write Grant Ref to key 
    err = write_grant_refs_to_key();
    if ( err ) return;

    // XXXX: crash here?
    err = register_xenbus_watch(&msg_len_watch);
    if (err) {
        pr_err("Failed to set Message Length watcher\n");
        // XXXX: fall-through?
        return;
    } else {
        is_msg_len_watch = 1;
    }
 
    init_shared_ring();
}

static struct xenbus_watch client_id_watch = {

   .node = "/unikernel/random/client_id",
   .callback = client_id_state_changed
};

static irqreturn_t irq_event_handler( int port, void * data )
{
    //unsigned long flags;

    //local_irq_save( flags );
    //pr_info("irq_event_handler executing: port=%d data=%p call#=%d\n",
           //port, data, irqs_handled);
   
    ++irqs_handled;

    //xen_clear_irq_pending(irq);

    //local_irq_restore( flags );

    up(&mw_sem);

    return IRQ_HANDLED;
}

static void vm_port_is_bound(struct xenbus_watch *w,
                             const char **v,
                             unsigned int l)
{
   char     *is_bound_str; 
   int       i;

   pr_info("Checking whether %s is asserted\n",
          VM_EVT_CHN_BOUND);

   is_bound_str = (char *) read_from_key( VM_EVT_CHN_BOUND );

   if(XENBUS_IS_ERR_READ(is_bound_str)) {
      pr_info("Error reading evtchn bound key!!\n");
      return;
   }

   if (strcmp(is_bound_str,"0") == 0) {
      kfree(is_bound_str);
      return;
   }

   pr_info("The remote event channel is bound\n");

   irq = bind_evtchn_to_irqhandler( common_event_channel,
                                    irq_event_handler,
                                    0, NULL, NULL );

   printk( KERN_INFO "Bound event channel %d to irq: %d\n",
           common_event_channel, irq );

   for ( i = 0; i < 1; i++ )
   {
      printk( KERN_INFO "NOT Sending event via IRQ %d\n", irq );
   }
}

static struct xenbus_watch evtchn_bound_watch = {

   .node = ROOT_NODE "/" VM_EVT_CHN_BOUND,
   .callback = vm_port_is_bound
};

static int initialize_keys(void)
{
    int rc = 0;
    
    rc = write_to_key(CLIENT_ID_KEY, KEY_RESET_VAL);
    if ( rc ) goto ErrorExit;
    
    rc = write_to_key(SERVER_ID_KEY, KEY_RESET_VAL);
    if ( rc ) goto ErrorExit;
    
    rc = write_to_key(MSG_LEN_KEY, KEY_RESET_VAL);
    if ( rc ) goto ErrorExit;
    
    rc = write_to_key(GNT_REF_KEY, KEY_RESET_VAL);
    if ( rc ) goto ErrorExit;
    
    rc = write_to_key(VM_EVT_CHN_PATH, KEY_RESET_VAL);
    if ( rc ) goto ErrorExit;
    
    rc = write_to_key(VM_EVT_CHN_BOUND, KEY_RESET_VAL);
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
   
   is_msg_len_watch = 0;
   is_client_id_watch = 0;
   is_evtchn_bound_watch = 0;

   start = ktime_set(0,0);;
   end = ktime_set(0,0);;

   pr_info("Initializing\n");

#if 1
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
   
   sema_init( &mw_sem, 0 );
   mutex_init( &mw_req_mutex );

   // Create worker thread that reads items from the ring buffer
   worker_thread = kthread_run( &consume_response_worker,
                                NULL,
                                "MwMsgConsumer" );
   if ( NULL == worker_thread )
   {
       pr_crit( "kthread_run() failed\n" );
       err = -ESRCH;
       goto ErrorExit;
   }

   pr_info("Created process %d to consume messages on the ring buffer\n",
           worker_thread->pid);
   
   //pr_info( "Created worker thread PID %d\n", worker_thread->pid );

   // Try to dynamically allocate a major number for the device --
   // more difficult but worth it
   majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
   if (majorNumber < 0)
   {
       //printk(KERN_ALERT "MWChar failed to register a major number\n");
       //return majorNumber;
       err = majorNumber;
       goto ErrorExit;
   }
   pr_info("registered correctly with major number %d\n", majorNumber);

   // Register the device class
   mwcharClass = class_create(THIS_MODULE, CLASS_NAME);
   if (IS_ERR(mwcharClass)) {
       //unregister_chrdev(majorNumber, DEVICE_NAME);
       //printk(KERN_ALERT "Failed to register mwcharClass device class\n");
       //return PTR_ERR(mwcharClass);
       err = PTR_ERR(mwcharClass);
       goto ErrorExit;
   }
   pr_info("device class registered correctly\n");

   // Register the device driver
   mwcharDevice = device_create(mwcharClass, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
   if (IS_ERR(mwcharDevice)) {
       //class_destroy(mwcharClass);
       //unregister_chrdev(majorNumber, DEVICE_NAME);
      pr_alert("Failed to create the device\n");
      //return PTR_ERR(mwcharDevice);
      err = PTR_ERR(mwcharDevice);
      goto ErrorExit;
   }

   pr_info("device class created correctly\n");

   // Set all protocol keys to zero
   err = initialize_keys();
   if ( err )
   {
       pr_alert("Key initialization failed: %d\n", err );
       //class_destroy(mwcharClass);
       //unregister_chrdev(majorNumber, DEVICE_NAME);
       //return err;
       goto ErrorExit;
   }
   
   // 1. Write Dom Id for Server to Key
   err = write_server_id_to_key();
   if ( err )
   {
       //class_destroy(mwcharClass);
       //unregister_chrdev(majorNumber, DEVICE_NAME);
       //return err;
       goto ErrorExit;
   }

   // 2. Watch Client Id XenStore Key
   err = register_xenbus_watch(&client_id_watch);
   if (err) {
      pr_err("Failed to set client id watcher\n");
   } else {
      is_client_id_watch = 1;
   }

   // 3. Watch for our port being bound
   err = register_xenbus_watch(&evtchn_bound_watch);
   if (err) {
      pr_err("Failed to set client local port watcher\n");
   } else {
      is_evtchn_bound_watch = 1;
   }

//   mutex_init( &mw_res_mutex );

ErrorExit:
   if ( err )
   {
       mwchar_exit();
   }
   return err;
}

/** @brief The driver cleanup function
 */
//static void __exit mwchar_exit(void)
static void mwchar_exit(void)
{
    thread_conn_map_t * curr = NULL;
    thread_conn_map_t * next = NULL;

    // XXXX: cleaner to assert a global indicating that system is going down
    if ( NULL != worker_thread )
    {
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
           pr_info("Ending access to grant ref 0x%x\n", grant_refs[i]);
           gnttab_end_foreign_access_ref( grant_refs[i], 0 );
       }
   }

   if ( NULL != server_region )
   {
       free_pages( (unsigned long) server_region, XENEVENT_GRANT_REF_ORDER );
   }

   if (is_client_id_watch) {
      unregister_xenbus_watch(&client_id_watch);
   }

   if (is_evtchn_bound_watch) {
      unregister_xenbus_watch(&evtchn_bound_watch);
   }

   initialize_keys();

   if (is_msg_len_watch)
   {
      unregister_xenbus_watch(&msg_len_watch);
   }

   if (irq)
   {
      unbind_from_irqhandler(irq, NULL);
   }

   if (!is_evt_chn_closed())
   {
      free_unbound_evt_chn();
   }

   mutex_destroy( &mw_req_mutex );
//   mutex_destroy( &mw_res_mutex );

   list_for_each_entry_safe( curr, next, &thread_conn_head, list )
   {
       pr_warning( "Removing mapping for PID %d (message ID 0x%llx)\n",
                   curr->pid, curr->message_id );
       destroy_thread_conn_map( &curr );
   }

   pr_info("cleanup is complete\n");
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
   pr_info("Device has been opened %d time(s)\n", numberOpens);
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

   connmap = get_thread_conn_map_by_pid( current->pid );
   if ( NULL == connmap )
   {
       pr_crit( "read() called from thread that hasn't issued write()\n" );
       rc = -EPERM;
       goto ErrorExit;
   }
   
   // Now we have the mapping for this thread. Wait for data to
   // arrive. This may block the calling process.
   mutex_lock( &connmap->data_pending );

   // An item is available and is in connmap. Process it.

   if ( len < connmap->response.base.size )
   {
       pr_crit( "User buffer too small for response.\n" );
       rc = -EINVAL;
       goto ErrorExit;
   }
   
   // Data has been received. Write it to the user buffer.
   rc = copy_to_user( buffer,
                      &connmap->response,
                      connmap->response.base.size );
   if ( rc )
   {
       // Some bytes could not be copied
       pr_crit( "copy_to_user() failed\n" );
       rc = -EIO;
       goto ErrorExit;
   }

   // Success
   rc = 0;

ErrorExit:
   return rc;
}

/** @brief This function is called whenever the device is being written to from user space i.e.
 *  data is sent to the device from the user. The data is copied to the message[] array in this
 *  LKM using the sprintf() function along with the length of the string.
 *  @param filep A pointer to a file object
 *  @param buffer The buffer to that contains the string to write to the device
 *  @param len The length of the array of data that is being passed in the const char buffer
 *  @param offset The offset if required
 */
static ssize_t 
dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset)
{
    /*
      sprintf(message, "%s(%u letters)", buffer, (unsigned int)len);   // appending received string with its length
      size_of_message = strlen(message);                 // store the length of the stored message
      pr_info("Received %u characters from the user\n", (unsigned int)len);

      send_request(message, size_of_message); 
      //send_evt(common_event_channel);
      */
    int rc = 0;
    int effective_number; 
    mt_request_generic_t *req;

    req = (mt_request_generic_t *)buffer;
   
    //pr_info("Sending %lu bytes through send_request()\n", sizeof(*req));
    if ( req->base.size > len )
    {
        pr_alert( "Received request that's larger than length passed in!\n" );
        rc = -EINVAL;
        goto ErrorExit;
    }
   
    if (ktime_to_ns(start) == 0)
    {
        start = ktime_get();
    }
    else
    {
        end = ktime_get();
        actual_time = ktime_to_ns(ktime_sub(end, start));
        effective_number = (irqs_handled + 1)/2;
        pr_info("%d     %lld\n", effective_number, actual_time);
        start = ktime_set(0,0);
    }
   
    //send_request(req, sizeof(*req));
    rc = send_request( req, req->base.size );
    if ( rc )
    {
        goto ErrorExit;
    }

    // Success
    rc = req->base.size;

    //end = ktime_get();

    //actual_time = ktime_to_ns(ktime_sub(end, start));
    //pr_info("Time taken for send_request() execution (ns): %lld\n",
    //actual_time);

    //actual_time = ktime_to_ms(ktime_sub(end, start));

    //pr_info("Time taken for send_request() execution (ms): %lld\n",
    //actual_time);

ErrorExit:
    return rc;
}

/** @brief The device release function that is called whenever the device is closed/released by
 *  the userspace program
 *  @param inodep A pointer to an inode object (defined in linux/fs.h)
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 */
static int dev_release(struct inode *inodep, struct file *filep){
   pr_info("Device successfully closed\n");
   return 0;
}

/** @brief A module must use the module_init() module_exit() macros from linux/init.h, which
 *  identify the initialization function at insertion time and the cleanup function (as
 *  listed above)
 */
module_init(mwchar_init);
module_exit(mwchar_exit);
