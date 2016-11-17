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

#include <linux/init.h>           
#include <linux/module.h>         
#include <linux/device.h>         
#include <linux/kernel.h>         
#include <linux/err.h>
#include <linux/fs.h>             
#include <linux/semaphore.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <asm/uaccess.h>          
#include <linux/time.h>

#include <xen/grant_table.h>
#include <xen/page.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/interface/callback.h>
#include <xen/interface/io/ring.h>

#include <message_types.h>
#include <xen_keystore_defs.h>

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

#define XENEVENT_GRANT_REF_ORDER  6 //(2^order == page count)
#define XENEVENT_GRANT_REF_COUNT (1 << XENEVENT_GRANT_REF_ORDER)
#define XENEVENT_GRANT_REF_DELIM " "

#define  DEVICE_NAME "mwchar"    // The device will appear at /dev/mwchar using this value
#define  CLASS_NAME  "mw"        // The device class -- this is a character device driver

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark Mason");
MODULE_DESCRIPTION("A Linux char driver to do comms and to read/write to user space MW component");
MODULE_VERSION("0.2");

static int    majorNumber;                  //  Device number -- determined automatically
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

ktime_t start, end;
s64 actual_time;

static struct semaphore mw_sem;

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

/** @brief Set the callback functions for the file_operations struct
 */
static struct file_operations fops =
{
   .open = dev_open,
   .read = dev_read,
   .write = dev_write,
   .release = dev_release,
};

static int write_to_key(const char *path, const char *value)
{
   struct xenbus_transaction   txn;
   int                         err;

   err = xenbus_transaction_start(&txn);
   if (err) {
      printk(KERN_INFO "MWChar: Error starting xenbus transaction\n");
      return 1;
   }

   err = xenbus_write(txn, ROOT_NODE, path, value);

   if (err) {
      printk(KERN_INFO "MWChar: Could not write to XenStore Key\n");
      xenbus_transaction_end(txn,1);
      return 1;
   }

   err = xenbus_transaction_end(txn, 0);

   return 0;
}

static char *read_from_key(const char *path)
{
   struct xenbus_transaction   txn;
   char                       *str;
   int                         err;

   err = xenbus_transaction_start(&txn);
   if (err) {
      printk(KERN_INFO "MWChar: Error starting xenbus transaction\n");
      return NULL;
   }

   if (path) {
      str = (char *)xenbus_read(txn, ROOT_NODE, path, NULL);
   } else {
      str = (char *)xenbus_read(txn, PRIVATE_ID_PATH, "", NULL);
   }

   if (XENBUS_IS_ERR_READ(str)) {
      printk(KERN_INFO "MWChar: Could not read XenStore Key\n");
      xenbus_transaction_end(txn,1);
      return NULL;
   }

   err = xenbus_transaction_end(txn, 0);

   return str;
}

static void write_server_id_to_key(void) 
{
   const char  *dom_id_str;

   // Get my domain id
   dom_id_str = (const char *)read_from_key(NULL);

   if (!dom_id_str) {
      printk(KERN_INFO "Error: Failed to read my Dom Id Key\n");
      return;
   }

   printk(KERN_INFO "MWChar: Read my Dom Id Key: %s\n", dom_id_str);

   write_to_key(SERVER_ID_KEY, dom_id_str);

   srvr_dom_id = simple_strtol(dom_id_str, NULL, 10);

   kfree(dom_id_str);
}

static void create_unbound_evt_chn(void) 
{
   struct evtchn_alloc_unbound alloc_unbound; 
   char                        common_event_channel_str[MAX_GNT_REF_WIDTH]; 
   int                         err;

   if (!client_dom_id)
      return;

   //alloc_unbound.dom = DOMID_SELF;
   alloc_unbound.dom = srvr_dom_id;
   alloc_unbound.remote_dom = client_dom_id; 

   err = HYPERVISOR_event_channel_op(EVTCHNOP_alloc_unbound, &alloc_unbound);

   if (err) {
      pr_err("Failed to set up event channel\n");
   } else {
      printk(KERN_INFO "Event Channel Port (%d <=> %d): %d\n", alloc_unbound.dom, client_dom_id, alloc_unbound.port);
   }

   common_event_channel = alloc_unbound.port;

   printk(KERN_INFO "Event channel's local port: %d\n", common_event_channel );
   memset(common_event_channel_str, 0, MAX_GNT_REF_WIDTH);
   snprintf(common_event_channel_str, MAX_GNT_REF_WIDTH, "%u", common_event_channel);

   write_to_key(VM_EVT_CHN_PATH,common_event_channel_str);
}

static void send_evt(int evtchn_prt)
{

   struct evtchn_send send;

   send.port = evtchn_prt;

   if (HYPERVISOR_event_channel_op(EVTCHNOP_send, &send)) {
      pr_err("Failed to send event\n");
   } /*else {
      printk(KERN_INFO "Sent Event. Port: %u\n", send.port);
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
      printk(KERN_INFO "Closed Event Channel Port: %d\n", close.port);
   }
}

static int 
receive_response(void *Response, size_t Size, size_t *BytesWritten)
{
   int rc = 0; 
   bool available = false;
   mt_response_generic_t * src = NULL;

   //printk(KERN_INFO "MWChar: receive_response() called\n");

   //start = ktime_get();

   do
   {

      available = RING_HAS_UNCONSUMED_RESPONSES(&front_ring);

      if (!available) {
         //printk(KERN_INFO "MWChar: down() called\n");
         down(&mw_sem);
      }
   
   } while (!available);

   //end = ktime_get();

   //actual_time = ktime_to_ns(ktime_sub(end, start));

   //printk(KERN_INFO "Time taken for receive_response() execution (ns): %lld\n",
          //actual_time);

   //actual_time = ktime_to_ms(ktime_sub(end, start));

   //printk(KERN_INFO "Time taken for receive_response() execution (ms): %lld\n",
          //actual_time);

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

ErrorExit:
   return rc;

}

static void
send_request(void *Request, size_t Size)
{

   bool notify = false;  
   void * dest = NULL;

   if (RING_FULL(&front_ring)) {

      // Do something drastic
      printk(KERN_INFO "MWChar: Front Ring is full\n");
      return;
   }

   dest = RING_GET_REQUEST(&front_ring, front_ring.req_prod_pvt);
   
   if (!dest) 
   {
      printk(KERN_INFO "MWChar: send_request(). destination buffer is NULL\n");
      return;
   }

   memcpy(dest, Request, Size);

   //print_hex_dump(KERN_INFO, "", DUMP_PREFIX_NONE, 16, 1, dest, Size, true);

   //printk(KERN_INFO "MWChar: send_request(). Copied %lu bytes to destination buffer\n", Size);

   ++front_ring.req_prod_pvt;

   RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&front_ring, notify);

   if (notify || !notify) {
      send_evt(common_event_channel);
      //printk(KERN_INFO "MWChar: send_request(). Sent %lu bytes to UK\n", Size);
      //printk(KERN_INFO "MWChar: send_request(). notify = %u \n", notify);
      
   }
}

static void
init_shared_ring(void)
{
   if (!server_region)
   {
      printk(KERN_INFO "MWChar: init_shared_ring(). server_region is NULL\n");
      return;
   }

   shared_ring = (struct mwevent_sring *)server_region;
   SHARED_RING_INIT(shared_ring);
   FRONT_RING_INIT(&front_ring, shared_ring, shared_mem_size);
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
       printk(KERN_INFO "MWChar: Error allocating memory (0x%x pages)\n",
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
           printk(KERN_INFO "MWChar: Error obtaining Grant\n");
           return 1;
       }

       grant_refs[ i ] = ret;
       //printk(KERN_INFO "MWChar: VA: %p MFN: %p grant 0x%x\n", va, (void *)mfn, ret);
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
            printk(KERN_INFO "MWChar: Insufficient space to write grant ref.\n");
            rc = 1;
            goto ErrorExit;
        }
        if (strncat( gnt_refs, one_ref, sizeof(gnt_refs) ) >= gnt_refs + sizeof(gnt_refs) )
        {
            printk(KERN_INFO "MWChar: Insufficient space to write all grant refs.\n");
            rc = 2;
            goto ErrorExit;
        }
    }

    write_to_key( GNT_REF_KEY, gnt_refs );

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
      printk(KERN_INFO "Error reading  Message Length Key!!!\n");
      return;
   }

   if (strcmp(msg_len_str,"0") != 0) {
      kfree(msg_len_str);
      return;
   }

   //
   // Get the message length 
   // 
   printk(KERN_INFO "Message length changed value:\n");
   printk(KERN_INFO "\tRead Message Length Key: %s\n", msg_len_str);

   if (!server_region) {
      kfree(msg_len_str);
      printk(KERN_INFO "MWChar: Error allocating memory\n");
      return;
   }

   memset(server_region, 0, shared_mem_size);
   memcpy(server_region, TEST_MSG, TEST_MSG_SZ);

   // Write Msg Len to key
   write_to_key(MSG_LEN_KEY, TEST_MSG_SZ_STR);

   kfree(msg_len_str);
   msg_counter++;

}

static struct xenbus_watch msg_len_watch = {

   .node = "/unikernel/random/msg_len",
   .callback = msg_len_state_changed
};

static void client_id_state_changed(struct xenbus_watch *w,
                                    const char **v,
                                    unsigned int l)
{
   char     *client_id_str;
   int       err;

   client_id_str = (char *)read_from_key(CLIENT_ID_KEY);

   if(XENBUS_IS_ERR_READ(client_id_str)) {
      printk(KERN_INFO "Error reading  Client Id Key!!!\n");
      return;
   }

   if (strcmp(client_id_str,"0") == 0) {
      kfree(client_id_str);
      return;
   }

   //
   // Get the client Id 
   // 
   printk(KERN_INFO "Client Id changed value:\n");
   printk(KERN_INFO "\tRead Client Id Key: %s\n", client_id_str);

   client_dom_id = simple_strtol(client_id_str, NULL, 10);

   printk(KERN_INFO "\t\tuint form: %u\n", client_dom_id);

   kfree(client_id_str);

   // Create unbound event channel with client
   create_unbound_evt_chn();

   // Write Msg Len to key

   write_to_key(MSG_LEN_KEY, TEST_MSG_SZ_STR);

   // Offer Grant to Client  
   offer_grant((domid_t)client_dom_id);

   // Reset Client Id Xenstore Key
   write_to_key(CLIENT_ID_KEY, KEY_RESET_VAL);

   // Write Grant Ref to key 
   write_grant_refs_to_key();

   err = register_xenbus_watch(&msg_len_watch);
   if (err) {
      pr_err("Failed to set Message Length watcher\n");
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
    unsigned long flags;

    local_irq_save( flags );
    //printk(KERN_INFO "irq_event_handler executing: port=%d data=%p call#=%d\n",
           //port, data, irqs_handled);
   
    ++irqs_handled;

    local_irq_restore( flags );

    up(&mw_sem);

    return IRQ_HANDLED;
}

static void vm_port_is_bound(struct xenbus_watch *w,
                             const char **v,
                             unsigned int l)
{
   char     *is_bound_str; 
   int       i;

   printk(KERN_INFO "Checking whether %s is asserted\n",
          VM_EVT_CHN_BOUND);
   is_bound_str = (char *) read_from_key( VM_EVT_CHN_BOUND );
   if(XENBUS_IS_ERR_READ(is_bound_str)) {
      printk(KERN_INFO "Error reading evtchn bound key!!\n");
      return;
   }
   if (strcmp(is_bound_str,"0") == 0) {
      kfree(is_bound_str);
      return;
   }

   printk(KERN_INFO "The remote event channel is bound\n");

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

static void initialize_keys(void)
{
   write_to_key(CLIENT_ID_KEY, KEY_RESET_VAL);
   write_to_key(SERVER_ID_KEY, KEY_RESET_VAL);
   write_to_key(MSG_LEN_KEY, KEY_RESET_VAL);
   write_to_key(GNT_REF_KEY, KEY_RESET_VAL);
   write_to_key(VM_EVT_CHN_PATH, KEY_RESET_VAL);
   write_to_key(VM_EVT_CHN_BOUND, KEY_RESET_VAL);
}

/** @brief The LKM initialization function
 *  @return returns 0 if successful
 */
static int __init mwchar_init(void) {

   int   err;

   foreign_grant_ref = 0;
   server_region = NULL;
   msg_counter = 0;
   client_dom_id = 0;
   common_event_channel = 0;
   
   is_msg_len_watch = 0;
   is_client_id_watch = 0;
   is_evtchn_bound_watch = 0;

   printk(KERN_INFO "MWChar: Initializing the MWChar LKM\n");

   // Try to dynamically allocate a major number for the device -- more difficult but worth it
   majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
   if (majorNumber<0){
      printk(KERN_ALERT "MWChar failed to register a major number\n");
      return majorNumber;
   }
   printk(KERN_INFO "MWChar: registered correctly with major number %d\n", majorNumber);

   // Register the device class
   mwcharClass = class_create(THIS_MODULE, CLASS_NAME);
   if (IS_ERR(mwcharClass)){
      unregister_chrdev(majorNumber, DEVICE_NAME);
      printk(KERN_ALERT "Failed to register mwcharClass device class\n");
      return PTR_ERR(mwcharClass);
   }
   printk(KERN_INFO "MWChar: device class registered correctly\n");

   // Register the device driver
   mwcharDevice = device_create(mwcharClass, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
   if (IS_ERR(mwcharDevice)){
      class_destroy(mwcharClass);
      unregister_chrdev(majorNumber, DEVICE_NAME);
      printk(KERN_ALERT "Failed to create the device\n");
      return PTR_ERR(mwcharDevice);
   }

   printk(KERN_INFO "MWChar: device class created correctly\n");

   // Set all protocol keys to zero
   initialize_keys();

   // 1. Write Dom Id for Server to Key
   write_server_id_to_key();

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

   sema_init(&mw_sem,0);
   
   return 0;
}

/** @brief The driver cleanup function
 */
static void __exit mwchar_exit(void){

   device_destroy(mwcharClass, MKDEV(majorNumber, 0));     // remove the device
   class_unregister(mwcharClass);                          // unregister the device class
   class_destroy(mwcharClass);                             // remove the device class
   unregister_chrdev(majorNumber, DEVICE_NAME);            // unregister the major number

   for ( int i = 0; i < XENEVENT_GRANT_REF_COUNT; i++ )
   {
       if ( 0 != grant_refs[ i ] )
       {
           printk(KERN_INFO "MWChar: Ending access to grant ref 0x%x\n", grant_refs[i]);
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

   if (is_msg_len_watch) {
      unregister_xenbus_watch(&msg_len_watch);
   }

   if (irq) {
      unbind_from_irqhandler(irq, NULL);
   }

   if (!is_evt_chn_closed()) {
      free_unbound_evt_chn();
   }

   printk(KERN_INFO "MWChar: Unloading gnt_srvr LKM\n");
   printk(KERN_INFO "MWChar: Goodbye from the LKM!\n");
}

/** @brief The device open function that is called each time the device is opened
 *  This will only increment the numberOpens counter in this case.
 *  @param inodep A pointer to an inode object (defined in linux/fs.h)
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 */
static int dev_open(struct inode *inodep, struct file *filep){
   numberOpens++;
   printk(KERN_INFO "MWChar: Device has been opened %d time(s)\n", numberOpens);
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
static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset){

   int error_count = 0;
   int rc = 0; 
   mt_response_generic_t *res = NULL;

   //printk(KERN_INFO "MWChar: dev_read() called\n");

   res = kmalloc(sizeof(mt_response_generic_t), GFP_KERNEL);

   if (!res)
   {
      printk(KERN_INFO "MWChar: Could not alloc memory\n");
      rc =  -EFAULT;
      goto ErrorExit;
   }

   memset(res, 0, sizeof(mt_response_generic_t));

   rc = receive_response(res,  sizeof(mt_response_generic_t), NULL);


   if (rc) 
   {
      printk(KERN_INFO "MWChar: Error on call to receive_response(). Err Code: %d\n", rc);
      goto ErrorExit;
   }

   error_count = copy_to_user(buffer, res, sizeof(mt_response_generic_t));

   //receive_response(message,  size_of_message, NULL);
   //copy_to_user has the format ( * to, *from, size) and returns 0 on success
   //error_count = copy_to_user(buffer, message, size_of_message);


   if (error_count==0){
      //printk(KERN_INFO "MWChar: Sent %lu characters to the user\n", sizeof(mt_response_generic_t));
      goto ErrorExit;

   } else {
      printk(KERN_INFO "MWChar: Failed to send %d characters to the user\n", error_count);
      rc = -EFAULT;
      goto ErrorExit;
   }

ErrorExit:

   kfree(res);
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
   printk(KERN_INFO "MWChar: Received %u characters from the user\n", (unsigned int)len);

   send_request(message, size_of_message); 
   //send_evt(common_event_channel);
   */

   mt_request_generic_t *req;

   req = (mt_request_generic_t *)buffer;

   //printk(KERN_INFO "MWChar: Sending %lu bytes through send_request()\n", sizeof(*req));

   start = ktime_get();
   send_request(req, sizeof(*req));
   end = ktime_get();

   actual_time = ktime_to_ns(ktime_sub(end, start));
   printk(KERN_INFO "Time taken for send_request() execution (ns): %lld\n",
          actual_time);

   actual_time = ktime_to_ms(ktime_sub(end, start));

   printk(KERN_INFO "Time taken for send_request() execution (ms): %lld\n",
          actual_time);

   return len;
}

/** @brief The device release function that is called whenever the device is closed/released by
 *  the userspace program
 *  @param inodep A pointer to an inode object (defined in linux/fs.h)
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 */
static int dev_release(struct inode *inodep, struct file *filep){
   printk(KERN_INFO "MWChar: Device successfully closed\n");
   return 0;
}

/** @brief A module must use the module_init() module_exit() macros from linux/init.h, which
 *  identify the initialization function at insertion time and the cleanup function (as
 *  listed above)
 */
module_init(mwchar_init);
module_exit(mwchar_exit);
