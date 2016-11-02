#include "rndzvs.h"

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

#define DOM0_ID           0
#define DOMID_KEY_LEN     64 
#define FIRST_DOMU_DOMID  1
#define MAX_DOMU_DOMID    100
#define DOMU_NAME_LEN     256 
#define DOMU_SELF_NAME      "rumprun-test_gnttab_srvr-rumprun.bin"
#define DOMU_FRIEND_NAME    "rumprun-test_gnttab_client-rumprun.bin"


static char *get_domu_name_by_domu_id(unsigned int domu_id)
{
	xenbus_transaction_t txn;
	char                *cfg;
	int                  retry;
	char                *err;
	char 		     domid_key[DOMID_KEY_LEN];

	bmk_memset(domid_key, 0, DOMID_KEY_LEN);
	bmk_snprintf(domid_key, DOMID_KEY_LEN, "/local/domain/%u/name", domu_id);

	err = xenbus_transaction_start(&txn);
	if (err) {
		minios_printk("\tError. xenbus_transaction_start(): %s\n", err);
		bmk_memfree(err, BMK_MEMWHO_WIREDBMK);
		return NULL;
	}
	
	err = xenbus_read(txn, domid_key, &cfg);
	if (err) {
		minios_printk("\tError. xenbus_read(): %s\n", err);
		minios_printk("\tValue: %s\n", cfg);
		bmk_memfree(err, BMK_MEMWHO_WIREDBMK);
		return NULL;
	} else {
		minios_printk("\tSuccess. xenbus_read(). Value: %s\n", cfg);
		minios_printk("\tDomId: %u\n", domu_id);
	}

	err = xenbus_transaction_end(txn, 0, &retry);
	if (err) {
		minios_printk("\tError. xenbus_transaction_read(): %s\n", err);
	}

	return cfg;
}

static unsigned int get_next_domu_id(unsigned int dom_id)
{

	xenbus_transaction_t txn;
	char                *cfg;
	int                  retry;
	char                *err;
	unsigned int         domu_id;
	char 		     domid_key[DOMID_KEY_LEN];
	unsigned int         found_domu;

	found_domu = 0;
	err = NULL;
	dom_id++;

	for(domu_id = dom_id; domu_id < MAX_DOMU_DOMID; domu_id++)
	{
		
		bmk_memset(domid_key, 0, DOMID_KEY_LEN);

		bmk_snprintf(domid_key, DOMID_KEY_LEN, "/local/domain/%u/name", domu_id);

		minios_printk("DomId: %u\n", domu_id);
		minios_printk("DomId Key: %s\n", domid_key);

		err = xenbus_transaction_start(&txn);
		if (err) {
			minios_printk("\tError. xenbus_transaction_start(): %s\n", err);
			bmk_memfree(err, BMK_MEMWHO_WIREDBMK);
			break;
		}

		/* Read the value associated with a path.  Returns a malloc'd error
		   string on failure and sets *value to NULL.  On success, *value is
		   set to a malloc'd copy of the value. */

		/*err = xenbus_read(txn, "/local/domain/42/name", &cfg);*/
		err = xenbus_read(txn, domid_key, &cfg);
		if (err) {

			minios_printk("\tError. xenbus_read(): %s\n", err);
			minios_printk("\tValue: %s\n", cfg);
			bmk_memfree(err, BMK_MEMWHO_WIREDBMK);

		} else {

			minios_printk("\tSuccess. xenbus_read(). Value: %s\n", cfg);
			minios_printk("\tDomId: %u\n", domu_id);
			found_domu = 1;
			
			if (cfg)
				bmk_memfree(cfg, BMK_MEMWHO_WIREDBMK);
		}

		err = xenbus_transaction_end(txn, 0, &retry);
		if (err) {
			minios_printk("\tError. xenbus_transaction_read(): %s\n", err);
		}

		if (found_domu)
			break;
	}

	if (!found_domu)
		domu_id = 0;

	return domu_id;
}

unsigned int find_friend_domu_id(void)
{
	unsigned int domu_id;
	char        *domu_name;
	int          friend_domu_found;

	domu_name = NULL;
	friend_domu_found = 0;
	domu_id = DOM0_ID;

	while (!friend_domu_found) {

		domu_id = get_next_domu_id(domu_id);

		if (domu_id) {

			domu_name = get_domu_name_by_domu_id(domu_id);
			
			minios_printk("\tFirst DomU Name: %s\n", domu_name);

			if (!bmk_strcmp(domu_name, DOMU_SELF_NAME)) {
				minios_printk("\tFound Me!!!\n");

			} else if (!bmk_strcmp(domu_name, DOMU_FRIEND_NAME)) {
				minios_printk("\tFound my Friend!!!\n");
				friend_domu_found = 1;
			}
				
			if (domu_name)
				bmk_memfree(domu_name, BMK_MEMWHO_WIREDBMK);

		} else {
		
			minios_printk("\tFriend DomU not found. Bailing out!!!\n");
			domu_id = 0;
			break;
		}
	}
	return domu_id;
}

