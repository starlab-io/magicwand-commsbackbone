// @MAGICWAND_HEADER@

/**
 * Public header for the MAGICWAND comm channel.
 *
 * The comm channel is a message oriented wrapper over Xen's libvchan.
 * TODO: Give this a real name.
 */


#include "vmsg.h"

struct mwchan;

/* A key question is how each end of an mwchan gets its mwchan instance.
 * The "server" side allocates the channel, and the "client" needs info
 * from the "server" in order to obtain the grant table index and event channel
 * port used for the ring buffers and notifications respectively.
 *
 * The Rendezvous Protocol describes this process at a conceptual level.
 * What does it look like at the API level?
 * The Rendezvou Protocol code provides an instance watching a rendezvous
 * point that will create the server end of the vchan we wrap -- which
 * creates the entries in XenStore that are used by the client to get
 * access to the vchan.
 *
 */

struct mwchan* mwchan_connect_server():

struct mwchan* mwchan_connect_rp(const char* rendezvous_point);

struct mwchan* mwchan_connect_rp_full(const char* rendezvous_point,
		const size_t to_server_buffer_size,
		const size_t from_server_buffer_size);

// This is called by the rendezvous protocol on behalf of the client.
struct mwchan* mwchan_server_connect(const char* rendezvous_request_path,
		const size_t to_server_buffer_size,
		const size_t from_server_buffer_size);


// The client calls this to create a request at the rendezvous point.
// This call will block while the Server creates the underlying vchan.
// It will poll the expected path location ever 100 ms up to timeout_seconds.
struct mwchan* mwchan_client_connect(const char* rendezvous_point,
		const size_t to_server_buffer_size,
		const size_t from_server_buffer_size,
		const size_t timeout_seconds);


/* *********** Write vmsg objects to a wmchan ************** */

// Blocking write of a vmsg to an mwchan.
ssize_t mwchan_write_vmsg(const struct mwchan* mwchan, const struct vmsg* vmsg,
		const struct timeval* timeout);

// Non-blocking write of a vmsg to an mwchan.
// This returns the number of bytes written.
// It also updates the vmsg read position -- you can pass the same vmsg to
// this function until the entire vmsg has be written to the wmchan.
// But how -- do we really want the read position in the vmsg to be updated?
// There needs to be a vmsg function to update the read position and one that
// will return the amount of readable data remaining -- the write-in-a-loop
// is done when that returns 0.
ssize_t mwchan_write_vmsg_nonblock(const struct mwchan* mwchan,
		const struct vmsg* vmsg);


/* ************* Read vmsg objects from a wmchan ************** */

// Blocking read of the next vmsg from wmchan.
// Timeout to give up if sufficient data isn't available in a certain time.
ssize_t mwchan_read_vmsg(const struct mwchan* const struct vmsg* vmsg,
		const struct timeval* timeout);


// Non-blocking read of a vmsg from a wmchan.
// This returns the number of bytes written into the vmsg from the vchan.
// We need to be able to call this in a loop until the vmsg is full.
// How will the caller know that the vmsg has been filled?
ssize_t mwchan_read_vmsg_nonblock(const struct mwchan* mwchan,
		const struct vmsg* vmsg);


/* ************* Get size information of the mwchan ************** */

// Get the number of bytes that can be written to this mwchan without blocking.
// This value depends on the amount of data already in the write ring buffer.
ssize_t mchan_get_non_blocking_write_size(const struct mwchan* mwchan);

// Get the number of bytes that can be read from this mwchan without blocking.
// This value depends on the amount of data already in the read ring buffer.
ssize_t mchan_get_non_blocking_read_size(const struct mwchan* mwchan);


// Get the maximum possible non-blocking write size for this mwchan.
// Since mwchan allows different sized to-server and from-server ring buffer
// sizes (and ring buffer size is what determines the maximum non-blocking
// message size), this value may be different depending on whether the
// server or the client is calling it.
// The maximum write for the server is the maximum read for the client and
// vice versa.
ssize_t mwchan_get_max_write_msg_size(const struct mwchan* mwchan);

// Get the maximum possible non-blocking read size for this mwchan.
// See notes for mchan_get_non_blocking_write_size.
ssize_t mwchan_get_max_read_msg_size(const struct mwchan* mwchan);



