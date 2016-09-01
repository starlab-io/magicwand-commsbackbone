/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

#include <mini-os/offer_accept_gnt.h>

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

/************************************
*
* XenStore Keys and Specs 
*
*************************************/
/* DomId max digit width */ 
#define MAX_DOMID_WIDTH           5
/* Grant ref max digit width */ 
#define MAX_GNT_REF_WIDTH         15
/* Default key reset value */
#define KEY_RESET_VAL             "0"
/* Define out-of-band keys */
#define SERVER_ID_PATH            "/unikernel/random/server_id" 
#define CLIENT_ID_PATH            "/unikernel/random/client_id" 
#define PRIVATE_ID_PATH           "domid"
#define GRANT_REF_PATH            "/unikernel/random/gnt_ref"
#define MSG_LENGTH_PATH           "/unikernel/random/msg_len"
#define EVT_CHN_PRT_PATH          "/unikernel/random/evt_chn_port"
#define LOCAL_PRT_PATH            "/unikernel/random/client_local_port"

/************************************
*
*  Grant Mapping Variables 
*
*************************************/
/* Default Nmbr of Grant Refs */
#define DEFAULT_NMBR_GNT_REF      1
/* Default Stride */
#define DEFAULT_STRIDE            1
/* Write access to shared mem */
#define WRITE_ACCESS_ON           1
/* First Domain Slot */
#define FIRST_DOM_SLOT            0 
/* First Grant Ref */
#define FIRST_GNT_REF             0 

/************************************
*
* Test Message Variables and Specs 
*
*************************************/
/* Test Message Size */
#define TEST_MSG_SZ               64
/* Max Message Width  */
#define MAX_MSG_WIDTH             5
/* Test Message */
#define TEST_MSG                  "The abyssal plain is flat.\n"

/*
* For testing purposes, the same module is used 
* both as a server and client. 
* IS_SERVER 1 ==> Server Mode
* IS_SERVER 0 ==> Client Mode
*
* Toggling this variable and recompiling the 
* unikernel generates a server or a client. In 
* testing the server comes up first, so it is 
* compiled and spun up first.  The client comes up 
* second, so it is compiled and spun up second. 
* When this ordering is followed, the server and 
* client follow a simple protocol to establish 
* shared memory between them. 
*/
#define IS_SERVER            0 

/* Shared memory */
static void *server_page;
static void *client_page;

/* Event channel page */
static void *evtchn_page;

/* Event channel local port */
static evtchn_port_t local_evtchn_prt;

/* Grant map */
static struct gntmap *gntmap_map;

/* Key writer utility function
*
*  path -  Specifies path to XenStore key where data is written
*  value - Value to write to XenStore key
*
*  return - 0 if successful, or 1 if not
*/
static int write_to_key(const char *path, const char *value)
{

	xenbus_transaction_t txn;
	int                  retry;
	char                *err;
	int                  res;

	res = 0;

	err = xenbus_transaction_start(&txn);
	if (err) {
		minios_printk("\tError. xenbus_transaction_start(): %s\n", err);
		bmk_memfree(err, BMK_MEMWHO_WIREDBMK);
		return 1;
	}
	
	err = xenbus_write(txn, path, value);
	if (err) {
		minios_printk("\tError. xenbus_write(): %s\n", err);
		bmk_memfree(err, BMK_MEMWHO_WIREDBMK);
		res = 1;
	} 

	err = xenbus_transaction_end(txn, 0, &retry);
	if (err) {
		minios_printk("\tError. xenbus_transaction_read(): %s\n", err);
		bmk_memfree(err, BMK_MEMWHO_WIREDBMK);
		return 1;
	}

	return res;
}
	
/* Key reader utility function 
*
*  path - Path to XenStore key where data is read
*
*  return - Dynamically allocated memory where 
*           value for key is stored, if successful.
*           If not, the function returns a NULL pointer. 
*
*  note   - Caller is responsible for freeing char pointer
            upon successful execution.
*             
*/
static char *read_from_key(const char *path)
{
	xenbus_transaction_t txn;
	char                *cfg;
	int                  retry;
	char                *err;

	cfg = NULL;

	err = xenbus_transaction_start(&txn);
	if (err) {
		minios_printk("\tError. xenbus_transaction_start(): %s\n", err);
		bmk_memfree(err, BMK_MEMWHO_WIREDBMK);
		return NULL;
	}
	
	err = xenbus_read(txn, path, &cfg);
	if (err) {
		minios_printk("\tError. xenbus_read(): %s\n", err);
		bmk_memfree(err, BMK_MEMWHO_WIREDBMK);
	}

	err = xenbus_transaction_end(txn, 0, &retry);
	if (err) {
		minios_printk("\tError. xenbus_transaction_read(): %s\n", err);
		bmk_memfree(err, BMK_MEMWHO_WIREDBMK);
	}
	/* 
	 * When there is an error on xenbus_read(), cfg is NULL ==> 
	 * Function returns NULL
	*/
	return cfg;
}

/* Server-side function that grants a page of memory 
*  to share with the client. 
*
*  domu_client_id - Domain Id of the client
*
*  return - Grant reference to shared memory
*/
static grant_ref_t offer_grant(unsigned int domu_client_id)
{
    	unsigned long  mfn;
	grant_ref_t    ref;


	server_page = bmk_pgalloc_one();

	if (!server_page) {
		return 0;
		minios_printk("\tFailed to alloc page\n");
	}

    	minios_printk("\tServer page address: %p\n", server_page);

	bmk_memset(server_page, 0, PAGE_SIZE);	
    	bmk_memcpy(server_page, TEST_MSG, TEST_MSG_SZ);

    	mfn = virt_to_mfn(server_page);
    	minios_printk("\tBase Frame: %x\n", mfn);

	ref = gnttab_grant_access(domu_client_id, mfn, 0);
	
    	minios_printk("\tGrant Ref for Interdomain: %u\n", ref);

	return ref;
}

static void read_from_dedicated_channel(void)
{
	unsigned char              read_buf[TEST_MSG_SZ];
    	char                      *err;
	int                        res;
    	char                      *msg;
	unsigned int               msg_len;
    	struct xenbus_event_queue  events;
	unsigned int               count;

    	xenbus_event_queue_init(&events);
	count = 0;

	while (count < 2) {

		/* Wait on Msg Len */
		xenbus_watch_path_token(XBT_NIL, MSG_LENGTH_PATH, MSG_LENGTH_PATH, &events);
		while ((err = xenbus_read(XBT_NIL, MSG_LENGTH_PATH, &msg)) != NULL ||  msg[0] == '0') {
		    bmk_memfree(msg, BMK_MEMWHO_WIREDBMK);
		    bmk_memfree(err, BMK_MEMWHO_WIREDBMK);
		    xenbus_wait_for_watch(&events);
		}

		/* Read in the Message Length */
		msg = read_from_key(MSG_LENGTH_PATH);
		if (!msg) {
			return;
		}
		msg_len = bmk_strtoul(msg, NULL, 10);
		bmk_memfree(msg, BMK_MEMWHO_WIREDBMK);

		minios_printk("\tRead Msg Length from Key: %u\n", msg_len);

		if (!client_page) {
			minios_printk("\Error: Shared Mem Buf is NULL\n");
			return;
		}

		bmk_memset(read_buf, 0, msg_len);
		bmk_memcpy(read_buf, client_page, msg_len);

		minios_printk("\tBuffer: %s\n",read_buf);

		/* Clear value of message length key */
		res = write_to_key(MSG_LENGTH_PATH, KEY_RESET_VAL);
		if (res) {
			return;
		}
		
		count++;
	}

    	xenbus_unwatch_path_token(XBT_NIL, MSG_LENGTH_PATH, MSG_LENGTH_PATH);
}

/* Client-side function that maps in a page of memory 
*  granted by the server. 
*
*  domu_server_id   - Domain Id of the server 
*  client_grant_ref - Client grant reference to 
*                     shared memory
*  msg_len          - Length of data the server writes
*                     to shared memory
*
*  return - 0 if successful, 1 if not 
*/
static int accept_grant(domid_t      domu_server_id, 
	                grant_ref_t  client_grant_ref,
			unsigned int msg_len)
{

	uint32_t       count;
	uint32_t       domids[DEFAULT_NMBR_GNT_REF];
	int            domids_stride;
	grant_ref_t    grant_refs[DEFAULT_NMBR_GNT_REF];
	int            write;

	if (msg_len > PAGE_SIZE)
		return 1;

	unsigned char  read_buf[TEST_MSG_SZ];

	bmk_memset(read_buf, 0, TEST_MSG_SZ);

	gntmap_map = (struct gntmap *)bmk_pgalloc_one();
	
	gntmap_init(gntmap_map);

	count = DEFAULT_NMBR_GNT_REF;
	domids[FIRST_DOM_SLOT] = domu_server_id;
	domids_stride = DEFAULT_STRIDE;
	grant_refs[FIRST_GNT_REF] = client_grant_ref;
	write = WRITE_ACCESS_ON;

	client_page = (unsigned char *)gntmap_map_grant_refs(gntmap_map,
		                                      	     count,
			                                     domids,
						             domids_stride,
						             grant_refs,
						             write);
	if (!client_page) {
		minios_printk("\tMapping in the Memory bombed out!\n");
		return 1;
	}

	minios_printk("\tShared Mem: %p\n", client_page);

    	bmk_memcpy(read_buf, client_page, msg_len);

	minios_printk("\tBuffer: %s\n",read_buf);

	return 0;
}

static int run_server(void)
{
	unsigned int 		  domid;
	char                      domid_str[MAX_DOMID_WIDTH];
    	struct xenbus_event_queue events;
    	char*                     err;
    	char*                     msg;
	int                       res;
	unsigned int              client_id;
	grant_ref_t               grant_ref;
	char                      grant_ref_str[MAX_GNT_REF_WIDTH];
	char                      msg_len_str[MAX_MSG_WIDTH];

        err = NULL;
        msg = NULL;
	client_id = 0;
	res = 0;

	/* Get DomId for the Server */

	msg = read_from_key(PRIVATE_ID_PATH);
	if (!msg) {
		return 1;
	}
	domid = bmk_strtoul(msg, NULL, 10);
	bmk_memfree(msg, BMK_MEMWHO_WIREDBMK);

	minios_printk("\tRead Server DomId from Key: %u\n", domid);

	/* Write Server DomId to Key */

	bmk_memset(domid_str, 0, MAX_DOMID_WIDTH);
	bmk_snprintf(domid_str, MAX_DOMID_WIDTH, "%u", domid);

	res = write_to_key(SERVER_ID_PATH, domid_str);
	if (res) {
		return res;
	}
	
	/* Wait on Client DomId */
	
    	xenbus_event_queue_init(&events);

        xenbus_watch_path_token(XBT_NIL, CLIENT_ID_PATH, CLIENT_ID_PATH, &events);
        while ((err = xenbus_read(XBT_NIL, CLIENT_ID_PATH, &msg)) != NULL ||  msg[0] == '0') {
            bmk_memfree(msg, BMK_MEMWHO_WIREDBMK);
            bmk_memfree(err, BMK_MEMWHO_WIREDBMK);
            xenbus_wait_for_watch(&events);
        }

    	xenbus_unwatch_path_token(XBT_NIL, CLIENT_ID_PATH, CLIENT_ID_PATH);

	minios_printk("\tAction on Client Id key\n");
	
	/* Get Client DomId */
	
	msg = NULL;
	msg = read_from_key(CLIENT_ID_PATH);
	if (!msg) {
		return 1;
	}
	client_id = bmk_strtoul(msg, NULL, 10);
	bmk_memfree(msg, BMK_MEMWHO_WIREDBMK);

	minios_printk("\tRead Cliient Id from Key: %u\n", client_id);
	
	/* Reset Client Id */
	res = write_to_key(CLIENT_ID_PATH, KEY_RESET_VAL);
	if (res) {
		return res;
	}

	/* Write Message Length */
	bmk_memset(msg_len_str, 0, MAX_MSG_WIDTH);
	bmk_snprintf(msg_len_str, MAX_MSG_WIDTH, "%u", TEST_MSG_SZ);
	
	res = write_to_key(MSG_LENGTH_PATH, msg_len_str);
	if (res) {
		return res;
	}

	/* Execute the Grant */
	grant_ref = offer_grant(client_id);

	bmk_memset(grant_ref_str, 0, MAX_GNT_REF_WIDTH);
	bmk_snprintf(grant_ref_str, MAX_GNT_REF_WIDTH, "%u", grant_ref);
	
	/* Set Grant Ref Key */
	write_to_key(GRANT_REF_PATH, grant_ref_str);
	if (res) {
		return res;
	}

	return 0;
}

static void test_handler(evtchn_port_t port, struct pt_regs *regs, void *data)
{
	minios_printk("\tEvent Channel handler called\n");
	/*minios_wake_up(&blkfront_queue);*/
}

static evtchn_port_t bind_to_interdom_chn(domid_t srvr_id, evtchn_port_t remote_prt_nmbr)
{

        int           err;
	evtchn_port_t local_port;
	char          local_prt_str[MAX_DOMID_WIDTH];

	evtchn_page = bmk_pgalloc_one();

	if (!evtchn_page) {
    		minios_printk("\tFailed to alloc Event Channel page\n");
		return 0;
	}
    	minios_printk("\tEvent Channel page address: %p\n", evtchn_page);
	bmk_memset(evtchn_page, 0, PAGE_SIZE);	


	err = minios_evtchn_bind_interdomain(srvr_id,
                                             remote_prt_nmbr,
				             test_handler,
                                             evtchn_page,
				             &local_port);
        if(err) {
    		minios_printk("\tCould not bind to event channel\n");
		return 0;
	}

	minios_printk("\tLocal port for event channel: %u\n", local_port);

	local_evtchn_prt = local_port;	

	minios_unmask_evtchn(local_port);
	//minios_unmask_evtchn(remote_prt_nmbr);

	bmk_memset(local_prt_str, 0, MAX_DOMID_WIDTH);
	bmk_snprintf(local_prt_str, MAX_DOMID_WIDTH, "%u", local_port);

	err = write_to_key(LOCAL_PRT_PATH, local_prt_str);
	if (err) {
		return 0;
	}
	
	return local_port;
}

static int run_client(void)
{
	domid_t                   domid;
	char                      domid_str[MAX_DOMID_WIDTH];
    	char*                     err;
    	char*                     msg;
	int                       res;
	domid_t                   server_id;
    	struct xenbus_event_queue events;
	grant_ref_t		  client_grant_ref;
	unsigned int              msg_len;
	evtchn_port_t             evt_chn_prt_nmbr;

    	//struct xenbus_event_queue events_2;

        err = NULL;
        msg = NULL;
	res = 0;
	server_id = 0;
	domid = 0;
	msg_len = 0;

	/* Get DomId for the Client */

	msg = read_from_key(PRIVATE_ID_PATH);
	if (!msg) {
		return 1;
	}
	domid = bmk_strtoul(msg, NULL, 10);
	bmk_memfree(msg, BMK_MEMWHO_WIREDBMK);

	minios_printk("\tRead Client DomId from Key: %u\n", domid);

	/* Write Client DomId to Key */

	bmk_memset(domid_str, 0, MAX_DOMID_WIDTH);
	bmk_snprintf(domid_str, MAX_DOMID_WIDTH, "%u", domid);

	res = write_to_key(CLIENT_ID_PATH, domid_str);
	if (res) {
		return res;
	}
	
	/* Read Server Id from Key */

	msg = read_from_key(SERVER_ID_PATH);
	if (!msg) {
		return 1;
	}
	server_id = bmk_strtoul(msg, NULL, 10);
	bmk_memfree(msg, BMK_MEMWHO_WIREDBMK);

	minios_printk("\tRead Server Id from Key: %u\n", server_id);
	
	/* Wait on Grant Ref */

    	xenbus_event_queue_init(&events);

        xenbus_watch_path_token(XBT_NIL, GRANT_REF_PATH, GRANT_REF_PATH, &events);
        while ((err = xenbus_read(XBT_NIL, GRANT_REF_PATH, &msg)) != NULL ||  msg[0] == '0') {
            bmk_memfree(msg, BMK_MEMWHO_WIREDBMK);
            bmk_memfree(err, BMK_MEMWHO_WIREDBMK);
            xenbus_wait_for_watch(&events);
        }

    	xenbus_unwatch_path_token(XBT_NIL, GRANT_REF_PATH, GRANT_REF_PATH);

	minios_printk("\tAction on Grant State Key\n");

	/* Read in Grant Ref */

	msg = read_from_key(GRANT_REF_PATH);
	if (!msg) {
		return 1;
	}
	client_grant_ref = bmk_strtoul(msg, NULL, 10);
	bmk_memfree(msg, BMK_MEMWHO_WIREDBMK);
	
	minios_printk("\tRead Grant Ref from Key: %u\n", client_grant_ref);
	
	/* Reset Grant State Key */
	res = write_to_key(GRANT_REF_PATH, KEY_RESET_VAL);
	if (res) {
		return res;
	}

	/* Read in the Message Length */

	msg = read_from_key(MSG_LENGTH_PATH);
	if (!msg) {
		return 1;
	}
	msg_len = bmk_strtoul(msg, NULL, 10);
	bmk_memfree(msg, BMK_MEMWHO_WIREDBMK);

	minios_printk("\tRead Msg Length from Key: %u\n", msg_len);

	/* Map the Grant */
	res =  accept_grant(server_id, 
		            client_grant_ref,
			    msg_len); 

	/* Clear value of message length key */
	res = write_to_key(MSG_LENGTH_PATH, KEY_RESET_VAL);
	if (res) {
		return res;
	}

	/* Read some strings from shared memory */
	read_from_dedicated_channel();

	/*
        // Wait for Event Channel Port 
        xenbus_watch_path_token(XBT_NIL, EVT_CHN_PRT_PATH, EVT_CHN_PRT_PATH, &events);
        while ((err = xenbus_read(XBT_NIL, EVT_CHN_PRT_PATH, &msg)) != NULL ||  msg[0] == '0') {
            bmk_memfree(msg, BMK_MEMWHO_WIREDBMK);
            bmk_memfree(err, BMK_MEMWHO_WIREDBMK);
            xenbus_wait_for_watch(&events);
        }

    	xenbus_unwatch_path_token(XBT_NIL, EVT_CHN_PRT_PATH, EVT_CHN_PRT_PATH);

	minios_printk("\tAction on Event Channel Port Key\n");
	*/

	/* Read in Event Channel Port */

	msg = read_from_key(EVT_CHN_PRT_PATH);
	if (!msg) {
		return 1;
	}
	evt_chn_prt_nmbr = bmk_strtoul(msg, NULL, 10);
	bmk_memfree(msg, BMK_MEMWHO_WIREDBMK);
	
	minios_printk("\tRead Event Channel Port from Key: %u\n", evt_chn_prt_nmbr);
	
	/*
	// Reset Event Channel Key 
	res = write_to_key(EVT_CHN_PRT_PATH, KEY_RESET_VAL);
	if (res) {
		return res;
	}
	*/

        bind_to_interdom_chn(server_id, evt_chn_prt_nmbr);

	return 0;
}

int offer_accept_gnt_init(void)
{
	/* Initialize the Globals */
        server_page = NULL;
	client_page = NULL;
	gntmap_map = NULL;
	evtchn_page = NULL;

        local_evtchn_prt = 0;
	//unbind_all_ports();

	/* Execute mode: Server or Client */
	if (IS_SERVER == 1) {
		run_server();
	} else {
		run_client();
	}

	return 0;
}

void offer_accept_gnt_fini(void)
{
	if(gntmap_map) {
		gntmap_fini(gntmap_map);	
    		bmk_memfree(gntmap_map, BMK_MEMWHO_WIREDBMK);
	}

	if(server_page)
    		bmk_memfree(server_page, BMK_MEMWHO_WIREDBMK);

	if(client_page)
    		bmk_memfree(client_page, BMK_MEMWHO_WIREDBMK);

	
	minios_printk("\tUnbinding from Local Interdomain Event Channel Port: %u ...\n", local_evtchn_prt);

	minios_mask_evtchn(local_evtchn_prt);
        minios_unbind_evtchn(local_evtchn_prt);

	if(evtchn_page)
    		bmk_memfree(evtchn_page, BMK_MEMWHO_WIREDBMK);

}

