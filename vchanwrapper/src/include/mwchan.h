// @MAGICWAND_HEADER@

/**
 * Public header for the MAGICWAND comm channel: mwchan.
 *
 * mwchan is a message oriented wrapper over Xen's libvchan.
 * This library provides functions that simplify the process of
 * reading and writing vmsg instance from/to wmchan instances.
 *
 * Outstanding Questions:
 *   ___ Do our blocking calls need to support timeouts?
 *   ___ Do we need to provide any other raw libvchan functions?
 *   ___ Can libvchan be run in kernel-context on Linux?
 *
 * TODO List
 *   ___ Define the set of error codes.
 *
 */


// An opaque declaration is good enough for the callers.
struct mwchan;

// Don't include the entire vmsg.h header just to get an opaque declaration.
// #include "vmsg.h"
struct vmsg;


/*
 * The Rendezvous Protocol handles the creation of new wmchan instances.
 * The functionality declared in this header is for use with an existing
 * wmchan instance.
 */



/**
 * Check whether or not the mwchan is open.
 *
 * The mwchan has 3 possible "openness" states:
 *   0 - The mwchan is open in both directions.
 *   1 - The mwchan is open only on the server
 */
int32_t mwchan_is_open(const struct mwchan* mwchan);


/**
 * Close an mwchan.
 *
 * This closes @p mwchan but does not delete it.
 * All future read and write operations on @p mwchan will fail with an
 * error.
 * TODO:  Specify the error message.
 *
 * NOTE:  The caller is responsible for calling @c mwchan_delete
 *
 * @param wmchan The mwchan instance to close.
 * @return 0 on success, < 0 on error.
 */
int32_t mwchan_close(const struct mwchan* mwchan);


/**
 * Delete an mwchan, closing it if it is not already closed.
 *
 * While this function will close an open mwchan, it is best to 
 * close it first using @c mwchan_close.
 *
 * @param mwchan The mwchan instance to close.
 */
void mwchan_delete(const struct mwchan* mwchan);


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
 * If the caller wishes to ensure that the call will not block internally
 * waiting for @p wmchan to be read in order to make room for the entirity
 * of @p vmsg, then it can use @c mchan_get_non_blocking_write_size
 * and @c vmsg_get_write_size to check.
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
 *
 * TODO: Implement non-blocking vmsg write if necessary.
 * ssize_t mwchan_write_vmsg_nonblock(const struct mwchan* mwchan,
 *		const struct vmsg* vmsg);
 *
 */


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
 *
 * TODO:  Implement non-blocking vmsg read if necessary.
 *
 * ssize_t mwchan_read_vmsg_nonblock(const struct mwchan* mwchan,
 * 		const struct vmsg* vmsg);
 */


/**
 * Get the value size of the next vmsg in the wmchan.
 *
 * If @p wmchan is not empty, get the size of the next vmsg in @p wmchan.
 * If @p wmchan is empty, return 0.
 *
 * @param mwchan The mwchan instance to examine.
 * @return > 0 when a vmsg is available, 0 if no vmsg, < 0 on error.
 */
ssize_t mwchan_read_peek_vmsg_size_nonblock(const struct mwchan* mwchan);



/* ************* Get size information of the mwchan ************** */

/*
 * These size information functions are used to determine the amount of data
 * that can be read from or written to a wmchan without blocking.
 * Given the current state of the wmchan:
 *   @c mwchan_get_non_blocking_write_size
 *   @c mwchan_get_non_blocking_read_size
 * Given a completely empty wmchan:
 *   @c mwchan_get_max_write_msg_size
 *   @c mwchan_get_max_read_msg_size
 */


/**
 * Get the number of bytes that can be written to @p wmchan without blocking.
 *
 * This returns the number of bytes of available "room" in @p wmchan that
 * can be written to without waiting for a reader to read from @p wmchan.
 *
 * @param wmchan The wmchan instance to examine.
 * @return > 0 if @p wmchan has space, 0 if @p wmchan has no space, < 0 on error
 */
ssize_t mwchan_get_non_blocking_write_size(const struct mwchan* mwchan);


/**
 * Get the nubmer of bytes that can be read from @p wmchan without blocking.
 *
 * This returns the number of bytes of data that is "ready" in @p wmchan
 * that can be read without waiting for a writer to write more to @p wmchan.
 * If @p wmchan is empty, a 0 is returned.
 *
 * @param wmchan The wmchan instance to examine.
 * @return > 0 when data is available, 0 if no data is available, < 0 on error.
 */
ssize_t mwchan_get_non_blocking_read_size(const struct mwchan* mwchan);


/**
 * Get the maximum possible non-blocking write size for a mwchan.
 *
 * This returns the size of the internal write-ring-buffer of @p mwchan.
 * Under no circumstances will an mwchan be able to accept a non-blocking
 * write of more than the returned size.
 *
 * @param mwchan The mwchan instance to examine.
 * @return Number bytes in maximum message size, < 0 on error.
 */
ssize_t mwchan_get_max_write_msg_size(const struct mwchan* mwchan);


/**
 * Get the maximum possible non-blocking read size for a mwchan.
 *
 * This returns the size of the internal read-ring-buffer of @p wmchan.
 * This is the size of the largest possible message that can be read in
 * a non-blocking manner from @p wmchan.
 *
 * @param wmchan The wmchan instance to examine.
 * @return Number of bytes in maximum message size, < 0 on error.
 */
ssize_t mwchan_get_max_read_msg_size(const struct mwchan* mwchan);


/* ************** Raw vchan IO operations ************* */
/*
 * If implemented, these would be thin wrappers over the underlying
 * libvchan functions.
 *
 * If a library user wants to, they could access the low-level libvchan API
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


