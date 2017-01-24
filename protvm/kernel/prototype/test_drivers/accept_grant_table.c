#include "accept_grant_table.h"
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
#define DOMU_SELF_NAME    "rumprun-test_gnttab_client-rumprun.bin"
#define DOMU_FRIEND_NAME  "rumprun-test_gnttab_srvr-rumprun.bin"

static int gntmap_test(void) 
{
	struct gntmap *gntmap_map = NULL;
	uint32_t       count;
	uint32_t       domids;
	int            domids_stride;
	grant_ref_t    refs;
    	unsigned long mfn;
	
	count = 1;
	domids = 34;
	domids_stride = 0;
	refs = 1;

	gntmap_map = bmk_memalloc(sizeof(struct gntmap),0,BMK_MEMWHO_WIREDBMK);

	gntmap_init(gntmap_map);


	gntmap_map_grant_refs(gntmap_map,
		              count,
			      &domids,
			      domids_stride,
			      &refs,
			      1);
			      	      
	
	gntmap_fini(gntmap_map);	

	
    	mfn = virt_to_mfn(page);
    	minios_printk("Shared Frame: %p\n", mfn);

	init_gnttab();

	fini_gnttab();

	if(page)
    		bmk_memfree(page, BMK_MEMWHO_WIREDBMK);


	return 0;
}

int accept_gnttab_init(void)
{
	gntmap_test();

	return find_friend_domu_id();
}

void accept_gnttab_fini(void)
{

	return;
}

