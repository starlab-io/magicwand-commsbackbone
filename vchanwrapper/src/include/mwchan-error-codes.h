#ifndef __MWCHAN_ERROR_CODES_H__
#define __MWCHAN_ERROR_CODES_H__
// @MAGICWAND_HEADER@

/**
 * mwchan, the comm channel for the MAGICWAND system uses several numeric
 * error codes to indicate various error conditions.
 *
 * Each of the different major mwchan components has a range of error code
 * values reserved for its use.  The min and max values for each range, and
 * the maximum value currently used within each range are all available
 * with standardized names for ease of use.  The naming convention is:
 *
 *   E_<subsystem>_ERROR_CODE_MIN    -- All error codes related to subsystem
 *                                      will be greater than this value.
 *   E_<subsystem>_ERROR_CODE_MAX    -- All error codes related to subsystem
 *                                      will be less than this value.
 *   E_<subsystem>_ERROR_CODE_LIMIT  -- 1 greater than the largest error code
 *                                      currently defined for subsystem.
 *
 * Therefore, the currently defined error codes for a subsystem are:
 *    > E_<subsystem>_ERROR_CODE_MIN  and
 *    < E_<subsystem>_ERROR_CODE_LIMIT
 *
 * Each subsystems has 1000 error codes available to it.  That's obviously
 * overkill, but integers are cheap.
 *
 * Because these error codes are frequently used when a function returns an
 * int (either as a size or as an enum of some other type), we use large
 * negative values for the errors to reduce the likelihood of collisions.
*
 * Finally, the error code value of 0 is used to indicate success rather than
 * an error.
 */

// The range for the mwchan subsystem.
// Sadly, it has the same name as the overall comm channel, but in this case,
// the name refers to errors related to operations that access the underlying
// communication channel's vchan.
#define E_MWCHAN_RANGE_MIN -10999
#define E_MWCHAN_RANGE_MAX -10000

// The range for the vmsg subsystem.
// These error codes are for operations operating on vmsg structures.
#define E_VMSG_RANGE_MIN -11999
#define E_VMSG_RANGE_MAX -11000

// The range for the mwrp (the rendezvous protocol) subsystem.
// These error codes are for operations on mwrp structures.
#define E_MWRP_RANGE_MIN -12999
#define E_MWRP_RANGE_MAX -12000


enum mwchan_error_code {

	//  *********** Error codes for the mwchan subsystem.

	E_MWCHAN_ERROR_CODE_MIN = E_MWCHAN_RANGE_MIN,

	//! A pointer argument was null.
	E_MWCHAN_NULL_POINTER,

	//! The mwchan instance was closed.
	E_MWCHAN_CLOSED,

	//! The mwchan instance is malformed.
	E_MWCHAN_MALFORMED,

	//! An allocation failed.
	E_MWCHAN_ALLOC_FAILED,

	//! Writing to the underlying vchan failed.
	E_MWCHAN_WRITE_FAILED,

	//! Reading from the underlying vchan failed.
	E_MWCHAN_READ_FAILED,

	//! Message too large for destination.
	E_MWCHAN_TOO_LARGE,

	//! No data available in underlying vchan.
	E_MWCHAN_NO_DATA,

	E_MWCHAN_ERROR_CODE_LIMIT,
	E_MWCHAN_ERROR_CODE_MAX = E_MWCHAN_RANGE_MAX,


	//  *********** Error codes for the vmsg subsystem.

	E_VMSG_ERROR_CODE_MIN = E_VMSG_RANGE_MIN,

	//! A pointer was NULL.
	E_VMSG_NULL_POINTER,

	//! A vmsg was used before it was ready.
	E_VMSG_UNINITIALIZED,

	//! A vmsg did not have the expected type family.
	E_VMSG_WRONG_TYPE_FAMILY,

	//! Supplied vmsg type not valid.
	E_VMSG_INVALID_TYPE,

	//! Supplied size not valid for the type.
	E_VMSG_INVALID_SIZE_FOR_TYPE,

	//! Supplied size exceeds max size.
	E_VMSG_SIZE_TOO_LARGE,

	E_VMSG_ERROR_CODE_LIMIT,
	E_VMSG_ERROR_CODE_MAX = E_VMSG_RANGE_MAX,


	//  *********** Error codes for the mwrp subsystem.

	E_MWRP_ERROR_CODE_MIN = E_MWRP_RANGE_MIN,

        //! A pointer argument was NULL.
	E_MWRP_NULL_POINTER,

        //! Access to a XenStore path denied.
        E_MWRP_XENSTORE_ACCESS_DENIED,

        //! Connection to the XenStore database failed.
        E_MWRP_XENSTORE_CONNECT_FAILED,

        //! Read from a XenStore path failed.
        E_MWRP_XENSTORE_READ_FAILED,

        //! Write to a XensStore path failed.
        E_MWRP_XENSTORE_WRITE_FAILED,

        //! WATCH operation on a XenStore path failed.
        E_MWRP_XENSTORE_WATCH_FAILED,

        //! Delete of a XenStore path failed.
        E_MWRP_XENSTORE_DELETE_FAILED,

        //! Attempted to use a closed XenStore connection.
        E_MWRP_XENSTORE_CLOSED,

        //! Necessary memory allocation failed.
        E_MWRP_ALLOCATION_FAILED,

        //! Rendevous Point not started (WATCH not registered).
        E_MWRP_NOT_STARTED,

        //! Unable to create vchan connection
        E_MWRP_VCHAN_CREATE_FAILED,

        //! Buffer size too large.
        E_MWRP_BUFFER_TOO_LARGE,

        //! Invalid/unknown Rendezvous Point path.
        E_MWRP_BAD_RENDEZVOUS_POINT,

	E_MWRP_ERROR_CODE_LIMIT,
	E_MWRP_ERROR_CODE_MAX = E_MWRP_RANGE_MAX,

	// Success -- not an error, but in the same domain.
	E_SUCCESS = 0
};

#endif // __MWCHAN_ERROR_CODES_H__

