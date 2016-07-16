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

// TODO: Split the server/client specific operations into separate headers.
// Let this file focus soley on the IO operations.
// Is that really a good idea?
// Perhaps, document all the IO ops first and then move to the creation
// functions.

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


/*
 * TODO:  Should our blocking calls support a timeout?
 */

/*
 * How can we make it so the caller doesn't have to care about the fact
 * that vmsg objects have a header?  How can we keep them from having to
 * deal with the fact that the header's 4 bytes must be read and written
 * from/to the wmchan.  Especially in non-blocking IO operations, if the
 * wmchan only has enough room for part of the header, then if we're returning
 * the number of bytes read/written the caller needs to know that the
 * total number of bytes that needs to be read/written is vmsg value size
 * plus 4 bytes for the header.   That's not particularly helpful.
 *
 * Perhaps it is better to insulate the caller entirely from the number of
 * vmsg bytes read or written.  For blocking read/write, the call either
 * succeeds or doesn't -- no partial read/write is possible.
 * For non-blocking read/write, we could have the call return indicating
 * whether or not there was more vmsg remaining (either to read from the
 * wmchan or write to the wmchan), or an error -- but not return the number
 * of bytes read/written from/to the wmchan.
 *
 * To do that, the vmsg needs something like a read_prep or write_prep call
 * that can clear the counters that are needed for supporting this interface.
 * The wmchan library needs to read the vmsg header from the wmchan into
 * a private, internal, buffer -- so that when it writes a header into the
 * vmsg, it always writes a complete header -- the vmsg should never have a
 * partial or incomplete header.
 *
 */

/*
 * Open question about the portability of vchan:
 * Can we use the libvchan code in a Linux kernel module?
 * Or, if we want to do kernel-level monitoring on Linux will we
 * need to have the kernel module interact with a userland process
 * that handles the vchan interaction?
 */



/* *********** Write vmsg objects to a wmchan ************** */

/**
 * Perform a blocking write of a vmsg to a wmchan.
 *
 * Returns 0 on success, or a negative number indicating an error if the
 * write fails.
 *
 * Call blocks until entirity of @p vmsg is written to @p wmchan, or an
 * error occurs.
 *
 * @param mwchan The mwchan to write @p vmsg's contents to.
 * @param vmsg The vmsg object (header and value) to write to @p mwchan.
 * @return 0 on success, or < 0 on error.
 */
ssize_t mwchan_write_vmsg(const struct mwchan* mwchan, const struct vmsg* vmsg);


/**
 * Perform a non-blocking write of a vmsg to a wmchan.
 *
 * Call writes as much of @p vmsg to the @p vmchan as @p vmchan can accept
 * without blocking.
 *
 * The return value indicates whether or not @p vmsg has been completely
 * written to @p mwchan or not:
 *   > 0   -- The write of @p vmsg is not yet complete
 *   == 0  -- The write of @p vmsg has completed successfully
 *   < 0   -- An error occured
 *
 * Successive calls (without calling @c vmsg_write_prep on @p vmsg) will
 * continue to write the remaining data from @p vmsg to @p mwchan.
 *
 * @param mwchan The mwchan to write @p vmsg (header and value) to.
 * @param vmsg The vmsg object (header and value) to write to @p mwchan.
 * @return > 0 while incomplete, 0 on completion, or < 0 on an error.
 */
ssize_t mwchan_write_vmsg_nonblock(const struct mwchan* mwchan,
		const struct vmsg* vmsg);


/* ************* Read vmsg objects from a wmchan ************** */

/**
 * Perform a blocking read from a wmchan into a vmsg.
 *
 * @p vmsg must be large enough to hold the message (header and value)
 * read from @p wmchan.  If it is not, the read will fail and the message
 * will not be read from the wmchan -- a partial read will not be performed.
 *
 * The caller can use @c mwchan_read_peek_vmsg_size_nonblock() to find
 * the minimal vmsg size (vmsg value buffer size) sufficient to hold the
 * next vmsg from @p wmchan.
 *
 * The call blocks until one complete vmsg has been read from @p wmchan
 * into @p vmsg.
 *
 * Returns 0 on success, or a negative number indicating an error.
 *
 * @param mwchan The mwchan to read from into @p vmsg.
 * @param vmsg The vmsg to write the data read from @p vmsg into
 * @return 0 on success, or < 0 on an error.
 */
ssize_t mwchan_read_vmsg(const struct mwchan* const struct vmsg* vmsg);


// Non-blocking read of a vmsg from a wmchan.
// This returns the number of bytes written into the vmsg from the vchan.
// We need to be able to call this in a loop until the vmsg is full.
// How will the caller know that the vmsg has been filled?
/**
 * Peform a non-blocking read from a wmchan into a vmsg.
 *
 * The return value indicates whether or not the next vmsg from @p wmchan
 * has been completely read and written into @p vmsg, or that an error has
 * occured:
 *   > 0   -- The read of a vmsg into @p vmsg is not yet complete
 *  == 0   -- A complete vmsg has been read into @p vmsg
 *   < 0   -- An error occured
 *
 * Successive calls (without calling @c vmsg_read_prep on @p vmsg) will
 * continue to read a partially complete vmsg from @p wmchan into @p vmsg.
 *
 * @param wmchan The wmchan to read a vmsg from.
 * @param vmchan The vmsg to write the next vmsg from @p wmchan into.
 * @param > 0 while incomplete, 0 on completion, < 0 on an error.
 */
ssize_t mwchan_read_vmsg_nonblock(const struct mwchan* mwchan,
		const struct vmsg* vmsg);


/**
 * Get the value size of the next vmsg in the wmchan.
 *
 * Returns the size of the next wmsg in @p wmchan.  When calling
 * @c mwchan_read_vmsg(), the vmsg must be able to hold a value at least
 * this large.
 */
ssize_t mwchan_read_peek_vmsg_size_nonblock(const struct mwchan* mwchan);


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



/* ************** Raw vchan IO operations ************* */
/*
 * These operations are bare wrappers over the the underlying libvchan
 * operations.
 *
 * If a library user wants to, they can access the low-level libvchan API
 * this way.  We should consider whether each of the public functions from
 * libvchan/io.c makes sense to have here, and whether or not it makes
 * sense to document in the header file that the function is a thin wrapper
 * around the vchan call.
 *
 * If there is any chance of implementing this library on something other
 * than libvchan, then it doesn't make sense to call those methods out
 * specially.
 *
 * One key note is that callers can potentially get their mwchan in a
 * state that is difficult to recover from by mixing vmsg-based read/write
 * operations with the byte-oriented _read/_write operations.
 *
 * These are the operations from io.c:
 *   libxenvchan_read, libxenvchan_recv, libxenvchan_write, libxenvchan_send,
 *   libxenvchan_close, libxenvchan_is_open, libxenvchan_fd_for_select,
 *   libxenvchan_wait, libxenvchan_buffer_space, libxenvchan_data_ready,
 */


