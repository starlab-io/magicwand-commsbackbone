// @MAGICWAND_HEADER@

/**
 * Implementation of the mwchan comm protocol.
 *
 * TODO List
 * ___ How will we log errors?
 *     * Can we log the errors for a channel into a XenStore path?
 *       * If we do that, what will clean the errors up?
 */

#include <stdlib.h>
#include <stdint.h>
#include <libvchan.h>

#include "mwchan.h"
#include "vmsg.h"


typedef struct mwchan {
	enum mwchan_state state;
	enum mwchan_role role;
	int32_t vmsg_header_cache;
	int32_t have_cached_vmsg_header;  // boolean
	struct libxenvchan *vchan;
	xentoollog_logger logger;	// We have to implement this.

	// Do we need to track stuff about the rendezvous point?
	// Probably.

} mwchan_t;

/* NOTE: Header caching when doing blocking read.
 * The blocking read of @c mwchan_read_vmsg first reads the 4-byte header
 * from the vchan and then can check whether or not the passed in vmsg
 * instance has a large enough buffer to hold the vmsg that is waiting in the
 * vmsg or coming across the vmsg.  If it isn't, the caller will need to
 * call the function again supplying it with a larger vmsg.  When the call
 * is retried, the 4 bytes of the vmsg header still need to be available.
 * So, on failed reads, we need to store the vmsg header, but only on
 * failed reads.
 * 
 */
