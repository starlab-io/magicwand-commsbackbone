#include "offer_grant_table.h"
#include "rndzvs.h"

#include <xen/sched.h>
#include <mini-os/events.h>
#include <mini-os/os.h>
#include <mini-os/mm.h>
#include <mini-os/gnttab.h>
#include <mini-os/gntmap.h>
#include <bmk-core/string.h>
#include <bmk-core/printf.h>
#include <bmk-core/memalloc.h>
#include <bmk-core/pgalloc.h>

static grant_entry_t *gnttab_table;
static void *page;

static int offer_gnttab_test(unsigned int domu_friend_id) 
{
    	unsigned long mfn;
	struct gnttab_setup_table setup;
	unsigned long frames[1];

	setup.dom = DOMID_SELF;
	setup.nr_frames = 1;
	setup.frame_list = frames;

	HYPERVISOR_grant_table_op(GNTTABOP_setup_table, &setup, 1);
	
	page = bmk_memalloc(PAGE_SIZE,0,BMK_MEMWHO_WIREDBMK);
    	mfn = virt_to_mfn(page);
    	minios_printk("Shared Frame: %p\n", mfn);

	gnttab_table = bmk_memalloc(sizeof(grant_entry_t),0,BMK_MEMWHO_WIREDBMK);
	minios_printk("gnttab_table mapped at %p.\n", gnttab_table);
	
	gnttab_table[0].frame = mfn;
	gnttab_table[0].domid = domu_friend_id;
	wmb();
	gnttab_table[0].flags = GTF_permit_access;

	return 0;
}

int offer_gnttab_init(void)
{
	offer_gnttab_test(55);

	return 0;
}

void offer_gnttab_fini(void)
{

	if(page)
    		bmk_memfree(page, BMK_MEMWHO_WIREDBMK);

	if(gnttab_table)
    		bmk_memfree(page, BMK_MEMWHO_WIREDBMK);
}

