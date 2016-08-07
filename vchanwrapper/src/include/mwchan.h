#ifndef __MWCHAN_H__
#define __MWCHAN_H__
// @MAGICWAND_HEADER@

/**
 * Public header for the MAGICWAND comm channel IO operations.
 *
 * mwchan is a message oriented wrapper over Xen's libvchan.
 * This library provides functions that simplify the process of
 * reading and writing vmsg instance from/to wmchan instances.
 *
 * Outstanding Questions:
 *   ___ Do our blocking calls need to support timeouts?
 *   ___ Can libvchan be run in kernel-context on Linux?
 *
 *
 */

#include <stdint.h>

#include "vmsg.h"

// These are the error codes that our functions return.
#include "mwchan-error-codes.h"

// An opaque declaration is good enough for the callers.
struct mwchan;


/*
 * The Rendezvous Protocol handles the creation of new wmchan instances
 * for both servers and clients.
 * The functionality declared in this header is for use with an existing
 * wmchan instance.
 */

/*
 * TODO List
 * ___ Should we allow the caller to obtain a file descriptor for the mwchan?
 *     This would let the caller add the mwchan to their event loop.
 *     The libvchan library supports this.
 */

// The role of the mwchan.
typedef enum mwchan_role {
	// Not a real type -- just compiler tricks.
	// This ensures the compiler will use a signed int for the enum.
	// That lets enum mwchan_error_code "sort of" occupy the smae data
	// space.
	MWCHAN_ROLE_INVALID_ERROR = -1,

	//! The mwchan instance is the server side of the connection.
	MWCHAN_ROLE_SERVER        = 0,

	//! The mwchan instance is the client side of the connection.
	MWCHAN_ROLE_CLIENT        = 1
} mwchan_role_t;


// The state of an mwchan.
typedef enum mwchan_state {
	// Not a real type -- just compiler tricks.
	// Ensures that a signed int will be used for the enum.
	// That lets enum mwchan_error_code "sort of" occupy the smae data
	// space.
	MWCHAN_STATE_INVALID_ERROR = -1,

	//! The mwchan is open in both directions.
	MWCHAN_STATE_OPEN = 1,

	//! The mwchan is closed in either direction -- all IO ops will fail.
	MWCHAN_STATE_CLOSED = 2
} mwchan_open_state_t;


/**
 * Check whether or not the mwchan is open.
 *
 * @p mwchan is closed as soon as either side (client or server) closes it.
 *
 * @param The mwchan instance to check for openness.
 * retval MWCHAN_STATE_OPEN if @p mwchan is open in both directions.
 * retval MWCHAN_STATE_CLOSED if @p mwchan is closed in either direction.
 * @retval E_MWCHAN_NULL_POINTER if @p mwchan is NULL.
 */
enum mwchan_state mwchan_is_open(const struct mwchan* mwchan);


/**
 * Close an mwchan.
 *
 * This closes @p mwchan but does not delete it.
 *
 * All future read and write operations on @p mwchan will fail with the
 * error E_MWCHAN_CLOSED.
 *
 * NOTE:  The caller is responsible for calling @c mwchan_delete
 *
 * @param wmchan The mwchan instance to close.
 * @retval 0 on success.
 * @retval E_MWCHAN_NULL_POINTER if @p mwchan is NULL.
 * @retval E_MWCHAN_CLOSED if @p mwchan is already closed.
 */
enum mwchan_error_code mwchan_close(const struct mwchan* mwchan);


/**
 * Delete an mwchan, closing it if it is not already closed.
 *
 * While this function will close an open mwchan, it is prudent to 
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
 * @retval 0 on success.
 * @retval E_MWCHAN_NULL_POINTER if @p mwchan is NULL.
 * @retval E_VMSG_NULL_POINTER if @p vmsg is NULL.
 * @retval E_MWCHAN_CLOSED if @p mwchan is closed.
 * @retval E_MWCHAN_WRITE_FAILED if the write to the channel failed.
 * @retval E_VMSG_INVALID_TYPE if the type of @p vmsg is unsupported.
 */
ssize_t mwchan_write_vmsg(const struct mwchan* mwchan, const struct vmsg* vmsg);


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
 * @retval 0 on success.
 * @retval E_MWCHAN_NULL_POINTER if @p mwchan is NULL.
 * @retval E_VMSG_NULL_POINTER if @p vmsg is NULL.
 * @retval E_MWCHAN_CLOSED if @p mwchan is closed.
 * @retval E_MWCHAN_READ_FAILED if the read from the channel failed.
 * @retval E_MWCHAN_TOO_LARGE if @p vmsg is too small for the message.
 * @retval E_VMSG_INVALID_TYPE if the type of the message read is unsupported.
 */
ssize_t mwchan_read_vmsg(const struct mwchan* mwchan, const struct vmsg* vmsg);


/**
 * Get the value size of the next vmsg in the wmchan without blocking.
 *
 * If @p wmchan is not empty, get the size of the next vmsg in @p wmchan.
 * If @p wmchan is empty, returns E_MWCHAN_NO_DATA.
 * Return a negative number to indicate an error.
 *
 * NOTE: A return value of 0 is valid.
 * It is legitimate for a vmsg to contain no data only a header.
 *
 * @param mwchan The mwchan instance to examine.
 * @retval >= 0 when a vmsg is available.
 * @retval E_MWCHAN_NULL_POINTER if @p mwchan is NULL.
 * @retval E_MWCHAN_CLOSED if @p mwchan is closed.
 * @retval E_MWCHAN_NO_DATA if there is no data in the channel.
 * @retval E_MWCHAN_READ_FAILED if the read from the channel failed.
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
 * Returns a negative number on error.
 *
 * @param wmchan The wmchan instance to examine.
 * @retval >= 0 On success -- the amount of non-blocking space available.
 * @retval E_MWCHAN_NULL_POINTER if @p mwchan is NULL.
 * @retval E_MWCHAN_CLOSED if @p mwchan is closed.
 */
ssize_t mwchan_get_non_blocking_write_size(const struct mwchan* mwchan);


/**
 * Get the nubmer of bytes that can be read from @p wmchan without blocking.
 *
 * This returns the number of bytes of data that are "ready" in @p wmchan
 * to be read without waiting for a writer to write more to @p wmchan.
 * If @p wmchan is empty, a 0 is returned.
 *
 * @param wmchan The wmchan instance to examine.
 * @retval >= 0 when data is available in the channel.
 * @retval E_MWCHAN_NULL_POINTER if @p mwchan is NULL.
 * @retval E_MWCHAN_CLOSED if @p mwchan is closed.
 */
ssize_t mwchan_get_non_blocking_read_size(const struct mwchan* mwchan);


/**
 * Get the maximum possible non-blocking write size for a mwchan.
 *
 * This returns the size of the internal write ring-buffer of @p mwchan.
 * Under no circumstances will an mwchan be able to accept a non-blocking
 * write of more than the returned size.
 * Returns a negative number on error.
 *
 * @param mwchan The mwchan instance to examine.
 * @retval >= 0 on success -- size of the channel's write ring-buffer.
 * @retval E_MWCHAN_NULL_POINTER if @p mwchan is NULL.
 * @retval E_MWCHAN_CLOSED if @p mwchan is closed.
 */
ssize_t mwchan_get_max_write_msg_size(const struct mwchan* mwchan);


/**
 * Get the maximum possible non-blocking read size for a mwchan.
 *
 * This returns the size of the internal read ring-buffer of @p wmchan.
 * This is the size of the largest possible message that can be read in
 * a non-blocking manner from @p wmchan.
 * Returns a negative number on error.
 *
 * @param wmchan The wmchan instance to examine.
 * @retval >= 0 on success -- size of the channel's read ring-buffer.
 * @retval E_MWCHAN_NULL_POINTER if @p mwchan is NULL.
 * @retval E_MWCHAN_CLOSED if @p mwchan is closed.
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
 * That probably points to the need to make mwchan instances either
 * message or stream oriented and either not let the caller mix and match,
 * or give them a way to safely transition a mwchan from one to the other.
 *
 * These are the operations from io.c:
 *   libxenvchan_read, libxenvchan_recv, libxenvchan_write, libxenvchan_send,
 *   libxenvchan_close, libxenvchan_is_open, libxenvchan_fd_for_select,
 *   libxenvchan_wait, libxenvchan_buffer_space, libxenvchan_data_ready,
 */


#endif // __MWCHAN_H__
