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

/* Domain IDs are generally one digit */
#define MAX_DOMID_SZ          5
/* Grant References are generally five digits */
#define MAX_GNT_REF_SZ        15
/* Default reset value for protocol keys */
#define KEY_RESET_VAL         "0"
/* Default number of Grant Refs */
#define DEFAULT_NMBR_GNT_REF  1
/* Default Stride */
#define DEFAULT_STRIDE        1
/* Write access to shared memory */
#define WRITE_ACCESS_ON       1
/* First Domain Slot */
#define FIRST_DOM_SLOT        0 
/* First Grant Ref */
#define FIRST_GNT_REF         0 

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

void *page;
static struct gntmap *gntmap_map;

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

static grant_ref_t offer_gnttab_test(unsigned int my_domu_id,
			             unsigned int domu_friend_id) 
{
    	unsigned long mfn;
	grant_ref_t ref;

	page = bmk_pgalloc_one();
    	minios_printk("\tPage address: %p\n", page);
    	mfn = virt_to_mfn(page);
    	minios_printk("\tBase Frame: %x\n", mfn);

	ref = gnttab_grant_access(domu_friend_id, mfn, 0);
	
    	minios_printk("\tGrant Ref for Interdomain: %u\n", ref);

	return ref;
}

static int map_gnttab_test(domid_t     server_id, 
		           grant_ref_t client_grant_ref)
{

	uint32_t       count;
	uint32_t       domids[DEFAULT_NMBR_GNT_REF];
	int            domids_stride;
	grant_ref_t    grant_refs[DEFAULT_NMBR_GNT_REF];
	int            write;
	unsigned long* addr;

	gntmap_map = (struct gntmap *)bmk_pgalloc_one();
	
	gntmap_init(gntmap_map);

	count = DEFAULT_NMBR_GNT_REF;
	domids[FIRST_DOM_SLOT] = server_id;
	domids_stride = DEFAULT_STRIDE;
	grant_refs[FIRST_GNT_REF] = client_grant_ref;
	write = WRITE_ACCESS_ON;

	addr = NULL;
	addr = (unsigned long *)gntmap_map_grant_refs(gntmap_map,
		                                      count,
			                              domids,
						      domids_stride,
						      grant_refs,
						      write);
	if (!addr) {
		minios_printk("\tMapping in the Memory bombed out!\n");
		return 1;
	}

	minios_printk("\tShared Mem: %p\n",addr);
	return 0;
}

static int run_server(void)
{
	unsigned int 		  domid;
	char                      domid_str[MAX_DOMID_SZ];
    	struct xenbus_event_queue events;
    	char*                     err;
    	char*                     msg;
	int                       res;
	char*                     client_id_path;
	char*                     server_id_path;
	char*                     private_server_id_path;
	char*                     grant_ref_path;
	unsigned int              client_id;
	grant_ref_t               grant_ref;
	char                      grant_ref_str[MAX_GNT_REF_SZ];

	client_id_path = "/unikernel/random/clientid";
	server_id_path = "/unikernel/random/serverid";
	private_server_id_path = "domid";
	grant_ref_path =  "/unikernel/random/gnt_state";

        err = NULL;
        msg = NULL;
	client_id = 0;
	res = 0;

	/* Get DomId for the Server */

	msg = read_from_key(private_server_id_path);
	if (!msg) {
		return 1;
	}
	domid = bmk_strtoul(msg, NULL, 10);
	bmk_memfree(msg, BMK_MEMWHO_WIREDBMK);

	minios_printk("\tRead my DomId from Key: %u\n", domid);

	/* Write Server DomId to Key */

	bmk_memset(domid_str, 0, MAX_DOMID_SZ);
	bmk_snprintf(domid_str, MAX_DOMID_SZ, "%u", domid);

	res = write_to_key(server_id_path, domid_str);
	if (res) {
		return res;
	}
	
	/* Wait on Client DomId */
	
    	xenbus_event_queue_init(&events);

        xenbus_watch_path_token(XBT_NIL, client_id_path, client_id_path, &events);
        while ((err = xenbus_read(XBT_NIL, client_id_path, &msg)) != NULL ||  msg[0] == '0') {
            bmk_memfree(msg, BMK_MEMWHO_WIREDBMK);
            bmk_memfree(err, BMK_MEMWHO_WIREDBMK);
            xenbus_wait_for_watch(&events);
        }

    	xenbus_unwatch_path_token(XBT_NIL, client_id_path, client_id_path);

	minios_printk("\tAction on Client Id key\n");
	
	/* Get Client DomId */
	
	msg = NULL;
	msg = read_from_key(client_id_path);
	if (!msg) {
		return 1;
	}
	client_id = bmk_strtoul(msg, NULL, 10);
	bmk_memfree(msg, BMK_MEMWHO_WIREDBMK);

	minios_printk("\tRead Cliient Id from Key: %u\n", client_id);
	
	/* Reset Client Id */
	res = write_to_key(client_id_path, KEY_RESET_VAL);
	if (res) {
		return res;
	}

	/* Execute the Grant */
	grant_ref = offer_gnttab_test(domid, client_id);

	bmk_memset(grant_ref_str, 0, MAX_GNT_REF_SZ);
	bmk_snprintf(grant_ref_str, MAX_GNT_REF_SZ, "%u", grant_ref);
	
	/* Set Grant State Key */
	write_to_key(grant_ref_path, grant_ref_str);
	if (res) {
		return res;
	}

	return 0;
}

static int run_client(void)
{
	domid_t                   domid;
	char                      domid_str[MAX_DOMID_SZ];
    	char*                     err;
    	char*                     msg;
	int                       res;
	char*                     client_id_path;
	char*                     private_client_id_path;
	char*                     server_id_path;
	char*                     gnt_path;
	domid_t                   server_id;
    	struct xenbus_event_queue events;
	grant_ref_t		  client_grant_ref;

	server_id_path = "/unikernel/random/serverid";
	client_id_path = "/unikernel/random/clientid";
	private_client_id_path = "domid";
	gnt_path = "/unikernel/random/gnt_state";

        err = NULL;
        msg = NULL;
	res = 0;
	server_id = 0;
	domid = 0;

	/* Get DomId for the Client */

	msg = read_from_key(private_client_id_path);
	if (!msg) {
		return 1;
	}
	domid = bmk_strtoul(msg, NULL, 10);
	bmk_memfree(msg, BMK_MEMWHO_WIREDBMK);

	minios_printk("\tRead my DomId from Key: %u\n", domid);

	/* Write Client DomId to Key */

	bmk_memset(domid_str, 0, MAX_DOMID_SZ);
	bmk_snprintf(domid_str, MAX_DOMID_SZ, "%u", domid);

	res = write_to_key(client_id_path, domid_str);
	if (res) {
		return res;
	}
	
	/* Read Server Id from Key */

	msg = read_from_key(server_id_path);
	if (!msg) {
		return 1;
	}
	server_id = bmk_strtoul(msg, NULL, 10);
	bmk_memfree(msg, BMK_MEMWHO_WIREDBMK);

	minios_printk("\tRead Server Id from Key: %u\n", server_id);
	
	/* Wait on Grant Ref */

    	xenbus_event_queue_init(&events);

        xenbus_watch_path_token(XBT_NIL, gnt_path, gnt_path, &events);
        while ((err = xenbus_read(XBT_NIL, gnt_path, &msg)) != NULL ||  msg[0] == '0') {
            bmk_memfree(msg, BMK_MEMWHO_WIREDBMK);
            bmk_memfree(err, BMK_MEMWHO_WIREDBMK);
            xenbus_wait_for_watch(&events);
        }

    	xenbus_unwatch_path_token(XBT_NIL, gnt_path, gnt_path);

	minios_printk("\tAction on Grant State Key\n");

	/* Read in Grant Ref */

	msg = read_from_key(gnt_path);
	if (!msg) {
		return 1;
	}
	client_grant_ref = bmk_strtoul(msg, NULL, 10);
	bmk_memfree(msg, BMK_MEMWHO_WIREDBMK);
	
	minios_printk("\tRead Grant Ref from Key: %u\n", client_grant_ref);
	
	/* Reset Grant State Key */
	res = write_to_key(gnt_path, KEY_RESET_VAL);
	if (res) {
		return res;
	}

	/* Map the Grant */
	res =  map_gnttab_test(server_id, 
			       client_grant_ref); 
	return 0;
}

int offer_accept_gnt_init(void)
{
	/* Initialize the Globals */
        page = NULL;
	gntmap_map = NULL;

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

	if(page)
    		bmk_memfree(page, BMK_MEMWHO_WIREDBMK);
}

