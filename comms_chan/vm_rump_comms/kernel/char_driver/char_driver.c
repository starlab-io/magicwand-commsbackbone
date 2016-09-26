
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
#include <asm/uaccess.h>          

#include <xen/grant_table.h>
#include <xen/page.h>
#include <xen/xenbus.h>
#include <xen/events.h>

#include <xen/evtchn.h>

//#include <asm/sync_bitops.h>
//#include <asm/xen/hypervisor.h>
//#include <xen/xen-ops.h>

#define ROOT_NODE             "/unikernel/random"
#define SERVER_ID_KEY         "server_id" 
#define CLIENT_ID_KEY         "client_id" 
#define GNT_REF_KEY           "gnt_ref" 

#define MAX_GNT_REF_WIDTH     15
#define MSG_LEN_KEY           "msg_len"
#define PRIVATE_ID_PATH       "domid"

#define SELF_EVT_CHN       "vm_evt_chn_prt"
#define PAL_EVT_CHN        "uk_evt_chn_prt"

#define KEY_RESET_VAL      "0"
#define TEST_MSG_SZ         64
#define TEST_MSG_SZ_STR    "64"
#define TEST_MSG           "The abyssal plain is flat.\n"
#define MAX_MSG             1

#define  DEVICE_NAME "mwchar"    // The device will appear at /dev/mwchar using this value
#define  CLASS_NAME  "mw"        // The device class -- this is a character device driver

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark Mason");
MODULE_DESCRIPTION("A Linux char driver to do comms and to read/write to user space MW component");
MODULE_VERSION("0.2");

static int    majorNumber;                  //  Device number -- determined automatically
static char   message[PAGE_SIZE] = {0};     //  Memory for the message sent from userspace
static short  size_of_message;              //  Size of message sent from userspace 
static int    numberOpens = 0;              //  Counts the number of times the device is opened
static struct class*  mwcharClass  = NULL;  //  The device-driver class struct pointer
static struct device* mwcharDevice = NULL;  //  The device-driver device struct pointer

static void          *server_page;
static grant_ref_t   foreign_grant_ref;
static unsigned int  msg_counter;
static unsigned int  is_client;
static domid_t       client_dom_id;
static domid_t       srvr_dom_id;
static int           self_event_channel;
static int           pal_event_channel;


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

typedef struct char_driver_dev {

   evtchn_port_t evtchn;

} char_driver_dev_t;

static char_driver_dev_t *c_dev;

static int write_to_key(const char *path, const char *value)
{
   struct xenbus_transaction   txn;
   int                         err;

   err = xenbus_transaction_start(&txn);
   if (err) {
      printk(KERN_INFO "GNT_SRVR: Error starting xenbus transaction\n");
      return 1;
   }

   err = xenbus_write(txn, ROOT_NODE, path, value);

   if (err) {
      printk(KERN_INFO "GNT_SRVR: Could not write to XenStore Key\n");
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
      printk(KERN_INFO "GNT_SRVR: Error starting xenbus transaction\n");
      return NULL;
   }

   if (path) {
      str = (char *)xenbus_read(txn, ROOT_NODE, path, NULL);
   } else {
      str = (char *)xenbus_read(txn, PRIVATE_ID_PATH, "", NULL);
   }

   if (XENBUS_IS_ERR_READ(str)) {
      printk(KERN_INFO "GNT_SRVR: Could not read XenStore Key\n");
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

   printk(KERN_INFO "GNT_SRVR: Read my Dom Id Key: %s\n", dom_id_str);

   write_to_key(SERVER_ID_KEY, dom_id_str);

   srvr_dom_id = simple_strtol(dom_id_str, NULL, 10);

   kfree(dom_id_str);
}

static void create_unbound_evt_chn(void) 
{
   struct evtchn_alloc_unbound alloc_unbound; 
   char                        self_event_channel_str[MAX_GNT_REF_WIDTH]; 
   int                         err;
   //struct evtchn_mask          mask;

   if (!client_dom_id)
      return;

   alloc_unbound.dom = DOMID_SELF;
   alloc_unbound.remote_dom = client_dom_id; 

   err = HYPERVISOR_event_channel_op(EVTCHNOP_alloc_unbound, &alloc_unbound);

   if (err) {
      pr_err("Failed to set up event channel\n");
   } else {
      printk(KERN_INFO "Event Channel Port: %d\n", alloc_unbound.port);
   }
   

   self_event_channel = alloc_unbound.port;

   memset(self_event_channel_str, 0, MAX_GNT_REF_WIDTH);
   snprintf(self_event_channel_str, MAX_GNT_REF_WIDTH, "%u", self_event_channel);

   write_to_key(SELF_EVT_CHN,self_event_channel_str);

}

static void send_evt(int evtchn_prt)
{

   struct evtchn_send send;

   send.port = evtchn_prt;

   if (HYPERVISOR_event_channel_op(EVTCHNOP_send, &send)) {
      pr_err("Failed to send event\n");
   } else {
      printk(KERN_INFO "Sent Event. Port: %u\n", send.port);
   }

}

static void free_unbound_evt_chn(void)
{

   struct evtchn_close close;
   int err;

   if (!self_event_channel)
      return;
      
   close.port = self_event_channel;

   err = HYPERVISOR_event_channel_op(EVTCHNOP_close, &close);

   if (err) {
      pr_err("Failed to close event channel\n");
   } else {
      printk(KERN_INFO "Closed Event Channel Port: %d\n", close.port);
   }

}

static grant_ref_t offer_grant(domid_t domu_client_id) 
{

   int ret;
   unsigned int mfn;

   ret = 0;
   mfn = 0;

   server_page = (void *)__get_free_page(GFP_KERNEL);

   if (!server_page) {

      printk(KERN_INFO "GNT_SRVR: Error allocating memory\n");
      return 1;
   }

   memset(server_page, 0, PAGE_SIZE);
   memcpy(server_page, TEST_MSG, TEST_MSG_SZ);

   mfn = virt_to_mfn(server_page);

   printk(KERN_INFO "GNT_SRVR: MFN: %u\n", mfn);

   ret = gnttab_grant_foreign_access(domu_client_id, mfn, 0);

   if (ret < 0) {

      printk(KERN_INFO "GNT_SRVR: Error obtaining Grant\n");
      return 0;
   }

   printk(KERN_INFO "Grant Ref: %u \n", ret);

   foreign_grant_ref = ret;

   return ret;
}

static void msg_len_state_changed(struct xenbus_watch *w,
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

   if (!server_page) {
      kfree(msg_len_str);
      printk(KERN_INFO "GNT_SRVR: Error allocating memory\n");
      return;
   }

   memset(server_page, 0, PAGE_SIZE);
   memcpy(server_page, TEST_MSG, TEST_MSG_SZ);

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
   char      gnt_ref_str[MAX_GNT_REF_WIDTH]; 
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

   is_client = 1;

   // Create unbound event channel with client
   create_unbound_evt_chn();

   // Write Msg Len to key

   write_to_key(MSG_LEN_KEY, TEST_MSG_SZ_STR);

   // Offer Grant to Client  
   offer_grant((domid_t)client_dom_id);

   write_to_key(CLIENT_ID_KEY, KEY_RESET_VAL);

   // Write Grant Ref to key 
   memset(gnt_ref_str, 0, MAX_GNT_REF_WIDTH);
   snprintf(gnt_ref_str, MAX_GNT_REF_WIDTH, "%u", foreign_grant_ref);

   write_to_key(GNT_REF_KEY, gnt_ref_str);

   err = register_xenbus_watch(&msg_len_watch);
   if (err) {
      pr_err("Failed to set Message Length watcher\n");
   }
}

static struct xenbus_watch client_id_watch = {

   .node = "/unikernel/random/client_id",
   .callback = client_id_state_changed
};

static irqreturn_t test_handler(int port, void * data)
{

   printk(KERN_INFO "Test handler called\n");

   return 0;
}

static void remote_evt_chn_state_changed(struct xenbus_watch *w,
                                         const char **v,
                                         unsigned int l)
{
   int       err;
   char     *remote_evt_chn_str;
   unsigned long irqflags;

   /*
   struct evtchn_unmask unmask;
   struct evtchn_bind_interdomain bind_interdomain;
   */

   irqflags = 24;
   remote_evt_chn_str = (char *)read_from_key(PAL_EVT_CHN);

   if(XENBUS_IS_ERR_READ(remote_evt_chn_str)) {
      printk(KERN_INFO "Error reading remote event channel key!!!\n");
      return;
   }

   if (strcmp(remote_evt_chn_str,"0") == 0) {
      kfree(remote_evt_chn_str);
      return;
   }

   //
   // Get the remote evt chn 
   // 
   printk(KERN_INFO "Remote evt chn changed value:\n");
   printk(KERN_INFO "\tRead evt chn key: %s\n", remote_evt_chn_str);

   pal_event_channel = simple_strtol(remote_evt_chn_str, NULL, 10);

   printk(KERN_INFO "\t\tuint form: %u\n", pal_event_channel);

   kfree(remote_evt_chn_str);

   err = bind_interdomain_evtchn_to_irqhandler(client_dom_id,
                                               pal_event_channel,
                                               test_handler,
                                               irqflags,
                                               DEVICE_NAME,
                                               c_dev);
                                               
   printk(KERN_INFO "\tBind Evt Chn Call returned: %d\n", err);

   /*
   err = bind_interdomain_evtchn_to_irqhandler(client_dom_id,
                                               pal_event_channel,
                                               test_handler,
                                               irqflags,
                                               DEVICE_NAME,
                                               &majorNumber);
                                               
   printk(KERN_INFO "\tBind Evt Chn Call returned: %d\n", err);
   */

   /*
   err = bind_interdomain_evtchn_to_irqhandler(client_dom_id,
                                               pal_event_channel,
                                               test_handler,
                                               irqflags,
                                               NULL,
                                               NULL);

   */

   /*
   // Bind to the evt chn
   bind_interdomain.remote_dom = client_dom_id;
   bind_interdomain.remote_port = pal_event_channel;

   err = HYPERVISOR_event_channel_op(EVTCHNOP_bind_interdomain,
                                     &bind_interdomain);

   if (err) {
      printk(KERN_INFO "\tError binding to Evt Chn\n");
      return;
   }

   err = bind_evtchn_to_irq(bind_interdomain.local_port);

   if (err) {
      printk(KERN_INFO "\tError binding Evt Chn to IRQ\n");
      return;
   }

   // Unmask port
   unmask.port = bind_interdomain.local_port; 
   HYPERVISOR_event_channel_op(EVTCHNOP_unmask, &unmask);

   c_dev->evtchn = bind_interdomain.local_port;

   // Request IRQ
   err = request_irq(bind_interdomain.local_port, test_handler, irqflags, DEVICE_NAME, c_dev);

   if (err) {
      printk(KERN_INFO "\tError binding handler to IRQ\n");
      return;
   }

   */ 

   /*
   // Clear evt chn
   sync_clear_bit(bind_interdomain.local_port, &xen_dummy_shared_info->evtchn_pending[0]);
   
   // Request IRQ
   err = request_irq(bind_interdomain.local_port, test_handler, irqflags, DEVICE_NAME, &majorNumber);

   if (err) {
      printk(KERN_INFO "\tError binding handler to Evt Chn\n");
      return;
   }
   */

   send_evt(self_event_channel);

}

static struct xenbus_watch remote_evt_chn_watch = {

   .node = "/unikernel/random/uk_evt_chn_prt",
   .callback = remote_evt_chn_state_changed
};

static void initialize_keys(void)
{
   write_to_key(CLIENT_ID_KEY, KEY_RESET_VAL);
   write_to_key(SERVER_ID_KEY, KEY_RESET_VAL);
   write_to_key(MSG_LEN_KEY, KEY_RESET_VAL);
   write_to_key(GNT_REF_KEY, KEY_RESET_VAL);
   write_to_key(SELF_EVT_CHN, KEY_RESET_VAL);
   write_to_key(PAL_EVT_CHN, KEY_RESET_VAL);
}

/** @brief The LKM initialization function
 *  @return returns 0 if successful
 */
static int __init mwchar_init(void) {

   int   err;

   foreign_grant_ref = 0;
   server_page = NULL;
   msg_counter = 0;
   is_client = 0;
   client_dom_id = 0;
   self_event_channel = 0;
   

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
   }

   // 3. Watch Remote Event Channel XenStore Key
   err = register_xenbus_watch(&remote_evt_chn_watch);
   if (err) {
      pr_err("Failed to set client evt chn watcher\n");
   }

   
   c_dev = kmalloc(sizeof(char_driver_dev_t), GFP_KERNEL);
   if (c_dev)
       memset(c_dev,0,sizeof(char_driver_dev_t));

   return 0;
}

/** @brief The LKM cleanup function
 */
static void __exit mwchar_exit(void){

   device_destroy(mwcharClass, MKDEV(majorNumber, 0));     // remove the device
   class_unregister(mwcharClass);                          // unregister the device class
   class_destroy(mwcharClass);                             // remove the device class
   unregister_chrdev(majorNumber, DEVICE_NAME);            // unregister the major number

   gnttab_end_foreign_access(foreign_grant_ref, 0, (unsigned long)server_page); 

   unregister_xenbus_watch(&client_id_watch);

   unregister_xenbus_watch(&remote_evt_chn_watch);

   free_unbound_evt_chn();

   initialize_keys();

   if (is_client) {

      unregister_xenbus_watch(&msg_len_watch);
   }

   kfree(c_dev);

   printk(KERN_INFO "GNT_SRVR: Unloading gnt_srvr LKM\n");
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

   // copy_to_user has the format ( * to, *from, size) and returns 0 on success
   error_count = copy_to_user(buffer, message, size_of_message);

   if (error_count==0){
      printk(KERN_INFO "MWChar: Sent %d characters to the user\n", size_of_message);
      return (size_of_message=0);

   } else {
      printk(KERN_INFO "MWChar: Failed to send %d characters to the user\n", error_count);
      return -EFAULT;
   }
}

/** @brief This function is called whenever the device is being written to from user space i.e.
 *  data is sent to the device from the user. The data is copied to the message[] array in this
 *  LKM using the sprintf() function along with the length of the string.
 *  @param filep A pointer to a file object
 *  @param buffer The buffer to that contains the string to write to the device
 *  @param len The length of the array of data that is being passed in the const char buffer
 *  @param offset The offset if required
 */
static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset){
   sprintf(message, "%s(%u letters)", buffer, (unsigned int)len);   // appending received string with its length
   size_of_message = strlen(message);                 // store the length of the stored message
   printk(KERN_INFO "MWChar: Received %u characters from the user\n", (unsigned int)len);

   
   send_evt(self_event_channel);

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
