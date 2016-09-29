/**
 * @file    gnt_srvr.c
 * @author  Mark Mason 
 * @date    19 July 2016
 * @version 0.1
 * @brief   A loadable kernel module (LKM)generating grants to clients 
*/

#include <linux/init.h>
#include <linux/module.h>         
#include <linux/kernel.h>         
#include <linux/err.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/skbuff.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/ip.h>

#include <xen/grant_table.h>
#include <xen/page.h>
#include <xen/xenbus.h>

#include <xen/events.h>
#include <xen/interface/callback.h>

#define ROOT_NODE             "/unikernel/random"
#define SERVER_ID_KEY         "server_id" 
#define CLIENT_ID_KEY         "client_id" 
#define GNT_REF_KEY           "gnt_ref" 

#define EVT_CHN_PRT_KEY       "vm_evt_chn_prt"
#define VM_EVT_CHN_IS_BOUND   "vm_evt_chn_is_bound"
#define CLIENT_LOCAL_PRT_KEY  "client_local_prt"

#define MAX_GNT_REF_WIDTH     15
#define MSG_LEN_KEY           "msg_len"
#define PRIVATE_ID_PATH       "domid"

#define KEY_RESET_VAL      "0"
#define TEST_MSG_SZ         64
#define TEST_MSG_SZ_STR    "64"
#define TEST_MSG           "The abyssal plain is flat.\n"
#define MAX_MSG             1

#define SSH_DST_PORT        22

MODULE_LICENSE("GPL");              
MODULE_AUTHOR("Mark Mason");      
MODULE_DESCRIPTION("A Linux driver interfacing with Xen to execute grants.");
MODULE_VERSION("0.1"); 

static char *name = "gnt_srvr";
module_param(name, charp, S_IRUGO);
MODULE_PARM_DESC(name, "Name for tracking in sys log");

static void          *server_page;
static grant_ref_t   foreign_grant_ref;
static unsigned int  msg_counter;
static unsigned int  is_client;
static domid_t       client_dom_id;
static int           event_channel_port;
static int           remote_channel_port;

struct nf_hook_ops  hook_options;
struct udphdr      *udp_header;
struct iphdr       *ip_header;
struct tcphdr      *tcp_header;

static int          irqs_handled = 0;


static void send_evt(int evtchn_prt)
{
   // note also: notify_remote_via_irq()
   struct evtchn_send send = { .port = evtchn_prt };
   printk(KERN_INFO "Sending event to port %d\n", evtchn_prt );

   if (HYPERVISOR_event_channel_op(EVTCHNOP_send, &send))
      pr_err("Failed to send event\n");
}


static unsigned int process_packet(unsigned int            hooknum,
                                   struct sk_buff          *skb,
                                   const struct net_device *in,
                                   const struct net_device *out,
                                   int                     (*okfn)(struct sk_buff *))
{

   unsigned int dst_port;
   unsigned int src_port;

static int callct = 0;

   ip_header = (struct iphdr *)skb_network_header(skb);

   if (!ip_header)
      return NF_ACCEPT;

   if (callct++ % 1000 == 0)
      printk(KERN_INFO "*** event handler called %d times\n", irqs_handled );

   if ( callct % 20 == 0 )
      send_evt(event_channel_port);

   // UDP
   if (ip_header->protocol == IPPROTO_UDP) {

      udp_header = (struct udphdr *)skb_transport_header(skb);

      dst_port = ntohs(udp_header->dest);
      src_port = ntohs(udp_header->source);

      printk(KERN_INFO "GNT_SRVR: Hooked UDP Packet\n");
      printk(KERN_INFO "\tGNT_SRVR: Src Port: %u\n", src_port);
      printk(KERN_INFO "\tGNT_SRVR: Dest Port: %u\n", dst_port);

   } else if (ip_header->protocol == IPPROTO_TCP) {

      tcp_header = (struct tcphdr *)skb_transport_header(skb);

      dst_port = ntohs(tcp_header->dest);
      src_port = ntohs(tcp_header->source);

      if (dst_port != SSH_DST_PORT) {

         printk(KERN_INFO "GNT_SRVR: Hooked TCP Packet\n");
	 printk(KERN_INFO "\tGNT_SRVR: Src Port: %u\n", src_port);
	 printk(KERN_INFO "\tGNT_SRVR: Dest Port: %u\n", dst_port);
      }
   }

   return NF_ACCEPT;

}

static int initialize_net_fltr_hook(void)
{

   printk(KERN_INFO "GNT_SRVR: Initializing Netfilter Hook\n");

   hook_options.hook = process_packet;

   hook_options.hooknum = NF_INET_LOCAL_IN;

   hook_options.pf = PF_INET;

   hook_options.priority = NF_IP_PRI_FIRST;

   nf_register_hook(&hook_options);

   return 0;
}

static void finalize_net_fltr_hook(void)
{

   nf_unregister_hook(&hook_options);

   printk(KERN_INFO "GNT_SRVR: Finalizing Netfilter Hook\n");
}

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
      printk(KERN_INFO "Read %s from path %s %s\n", str, ROOT_NODE, path );
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

   kfree(dom_id_str);
}

// xen_callback_t
static long event_callback( int Cmd, void * Arg )
{
    printk(KERN_INFO "GNT_SRVT: event_callback invoked: cmd=%d arg=%p\n", Cmd, Arg);
    return 0;
}

static void create_unbound_evt_chn(void) 
{
   struct evtchn_alloc_unbound alloc_unbound; 
   char                        event_channel_port_str[MAX_GNT_REF_WIDTH]; 
   int                         err;

   if (!client_dom_id)
      return;

   alloc_unbound.dom = DOMID_SELF;
   alloc_unbound.remote_dom = client_dom_id; 

   err = HYPERVISOR_event_channel_op(EVTCHNOP_alloc_unbound, &alloc_unbound);

   if (err) {
      pr_err("Failed to set up event channel\n");
   } else {
      printk(KERN_INFO "Event Channel Port (%d <=> %d): %d\n", DOMID_SELF, client_dom_id, alloc_unbound.port);
   }
   event_channel_port = alloc_unbound.port;

   printk(KERN_INFO "Event channel's local port: %d\n", event_channel_port );
   memset(event_channel_port_str, 0, MAX_GNT_REF_WIDTH);
   snprintf(event_channel_port_str, MAX_GNT_REF_WIDTH, "%u", event_channel_port);

   // The event channel is done setting up on this side by the time it is published
   write_to_key(EVT_CHN_PRT_KEY,event_channel_port_str);

   //unmask.port = event_channel_port;
   //(void)HYPERVISOR_event_channel_op(EVTCHNOP_unmask, &unmask);

}

static void free_unbound_evt_chn(void)
{

   struct evtchn_close close;
   int err;

   if (!event_channel_port)
      return;
      
   close.port = event_channel_port;

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

   initialize_net_fltr_hook();

}

static struct xenbus_watch client_id_watch = {

   .node = "/unikernel/random/client_id",
   .callback = client_id_state_changed
};

static void remote_port_state_changed(struct xenbus_watch *w,
                                      const char **v,
                                      unsigned int l)
{
   char     *remote_prt_str;

   remote_prt_str = (char *)read_from_key(CLIENT_LOCAL_PRT_KEY);

   if(XENBUS_IS_ERR_READ(remote_prt_str)) {
      printk(KERN_INFO "Error reading  Client Port Key!!!\n");
      return;
   }

   if (strcmp(remote_prt_str,"0") == 0) {
      kfree(remote_prt_str);
      return;
   }

   //
   // Get the remote port 
   // 
   printk(KERN_INFO "Remote port changed value:\n");
   printk(KERN_INFO "\tRead remote port key: %s\n", remote_prt_str);

   remote_channel_port = simple_strtol(remote_prt_str, NULL, 10);

   printk(KERN_INFO "\t\tuint form: %u\n", remote_channel_port);

   kfree(remote_prt_str);

   //send_evt(remote_channel_port);
}

static struct xenbus_watch remote_port_watch = {

   .node = "/unikernel/random/client_local_port",
   .callback = remote_port_state_changed
};


static irqreturn_t irq_event_handler( int port, void * data )
{
    unsigned long flags;

    local_irq_save( flags );
    printk(KERN_INFO "irq_event_handler executing: port=%d data=%p call#=%d\n",
           port, data, irqs_handled);
   
    ++irqs_handled;

//enum irqreturn {
//          IRQ_NONE                = (0 << 0),
//          IRQ_HANDLED             = (1 << 0),
//          IRQ_WAKE_THREAD         = (1 << 1),
//  };

    local_irq_restore( flags );
    return IRQ_HANDLED;
}


static void vm_port_is_bound(struct xenbus_watch *w,
                             const char **v,
                             unsigned int l)
{
   char     *is_bound_str; 

   //struct evtchn_unmask unmask;
   //struct evtchn_bind_virq bindev;
   struct callback_register cbreg;
   int irq = 0;
   int rc = 0;
   int i = 0;

   printk(KERN_INFO "Checking whether %s is asserted\n",
          VM_EVT_CHN_IS_BOUND);
   is_bound_str = (char *) read_from_key( VM_EVT_CHN_IS_BOUND );
   if(XENBUS_IS_ERR_READ(is_bound_str)) {
      printk(KERN_INFO "Error reading evtchn bound key!!\n");
      return;
   }
   if (strcmp(is_bound_str,"0") == 0) {
      kfree(is_bound_str);
      return;
   }

   printk(KERN_INFO "The remote event channel is bound\n");

   cbreg.type = CALLBACKTYPE_event;
   cbreg.flags = CALLBACKF_mask_events;
   cbreg.address = (xen_callback_t) event_callback;

#define is_canonical_address(x) (((long)(x) >> 47) == ((long)(x) >> 63))

   printk(KERN_INFO "Registering event callback %p (%d)\n", 
          (void *)event_callback, is_canonical_address(event_callback) );

   // returns err = -38 (ENOSYS)
   rc = HYPERVISOR_callback_op( CALLBACKOP_register, &cbreg );
   if ( rc )
      pr_err("Failed to set event callback: %d\n", rc);

  
   // bind the event channel to a VIRQ
   //bindev.virq = VIRQ_ARCH_0;
   //bindev.vcpu = 0;
   //irq = bind_evtchn_to_irq( event_channel_port );
   irq = bind_evtchn_to_irqhandler( event_channel_port,
                                    irq_event_handler,
                                    0, NULL, NULL );
   printk( KERN_INFO "Bound event channel %d to irq: %d\n",
           event_channel_port, irq );


   for ( i = 0; i < 1; i++ )
   {
      // 
      printk( KERN_INFO "NOT Sending event via IRQ %d\n", irq );
      //notify_remote_via_irq( irq );
      //notify_remote_via_evtchn( event_channel_port );
      //send_evt(event_channel_port);
   }
}

static struct xenbus_watch evtchn_bound_watch = {

   .node = ROOT_NODE "/" VM_EVT_CHN_IS_BOUND,
   .callback = vm_port_is_bound
};


static void initialize_keys(void)
{
   write_to_key(CLIENT_ID_KEY, KEY_RESET_VAL);
   write_to_key(SERVER_ID_KEY, KEY_RESET_VAL);
   write_to_key(MSG_LEN_KEY, KEY_RESET_VAL);
   write_to_key(GNT_REF_KEY, KEY_RESET_VAL);
   write_to_key(EVT_CHN_PRT_KEY, KEY_RESET_VAL);
   write_to_key(VM_EVT_CHN_IS_BOUND, KEY_RESET_VAL);
   write_to_key(CLIENT_LOCAL_PRT_KEY, KEY_RESET_VAL);
}

/** @brief The LKM initialization function
 *  The static keyword restricts the visibility of the function to within this C file. The __init
 *  macro means that for a built-in driver (not a LKM) the function is only used at initialization
 *  time and that it can be discarded and its memory freed up after that point.
 *  @return returns 0 if successful
 */
static int __init gnt_srvr_init(void) {

   int   err;

   foreign_grant_ref = 0;
   server_page = NULL;
   msg_counter = 0;
   is_client = 0;
   client_dom_id = 0;
   event_channel_port = 0;
   remote_channel_port = 0;

   printk(KERN_INFO "GNT_SRVR: Loading gnt_srvr LKM.\n");

   // Set all protocol keys to zero
   initialize_keys();

   // 1. Write Dom Id for Server to Key
   write_server_id_to_key();

   // 2. Watch Client Id XenStore Key
   err = register_xenbus_watch(&client_id_watch);
   if (err) {
      pr_err("Failed to set client id watcher\n");
   }

   // 3. Watch Remote Port XenStore Key
   err = register_xenbus_watch(&remote_port_watch);
   if (err) {
      pr_err("Failed to set client local port watcher\n");
   }

   // 4. Watch for our port being bound
   err = register_xenbus_watch(&evtchn_bound_watch);
   if (err) {
      pr_err("Failed to set client local port watcher\n");
   }


   return 0;
}

/** @brief The LKM cleanup function
 *  Similar to the initialization function, it is static. The __exit macro notifies that if this
 *  code is used for a built-in driver (not a LKM) that this function is not required.
 */
static void __exit gnt_srvr_exit(void){

   gnttab_end_foreign_access(foreign_grant_ref, 0, (unsigned long)server_page); 

   unregister_xenbus_watch(&client_id_watch);

   unregister_xenbus_watch(&remote_port_watch);

   unregister_xenbus_watch(&evtchn_bound_watch);

   free_unbound_evt_chn();

   initialize_keys();

   if (is_client) {

      unregister_xenbus_watch(&msg_len_watch);

      finalize_net_fltr_hook();
   }

   printk(KERN_INFO "GNT_SRVR: Unloading gnt_srvr LKM\n");
}

/** @brief A module must use the module_init() module_exit() macros from linux/init.h, which
 *  identify the initialization function at insertion time and the cleanup function (as
 *  listed above)
 */
module_init(gnt_srvr_init);
module_exit(gnt_srvr_exit);

