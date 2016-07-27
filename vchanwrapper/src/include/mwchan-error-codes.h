#ifndef __MWCHAN_ERROR_CODES_H__
#define __MWCHAN_ERROR_CODES_H__
// @MAGICWAND_HEADER@

/**
 * Error codes used by mwchan.
 * The error codes in mwchan are always negative numbers.
 * 0 indicates success rather than an error.
 * The available range of error code values is from E_VMSG_RANGE_MIN to
 * E_VMSG_RANGE_MAX.  The defined values range from E_VMSG_RANGE_MIN + 1
 * to E_MWCHAN_ERROR_CODE_LIMIT - 1.
 */
#define E_VMSG_RANGE_MIN -20999
#define E_VMSG_RANGE_MAX -20000
enum mwchan_error_code {
	E_MWCHAN_ERROR_CODE_MIN = E_VMSG_RANGE_MIN,
	E_MWCHAN_NULL_POINTER,    // A pointer was null.
	E_MWCHAN_CLOSED,          // The struct mwchan was closed.
	E_MWCHAN_MALFORMED,       // The struct mwchan is malformed.
	E_MWCHAN_MALFORMED_VMSG,  // The struct vmsg is malformed.
	E_MWCHAN_ALLOC_FAILED,    // An allocation failed.
	E_MWCHAN_WRITE_FAILED,    // Writing to the underlying vchan failed.
	E_MWCHAN_READ_FAILED,     // Reading from the underlying vchan failed.
	E_MWCHAN_TOO_LARGE,       // Message too large for destination.
	E_MWCHAN_NO_DATA,         // No data available in underlying vchan.

	E_MWCHAN_ERROR_CODE_LIMIT,
	E_MWCHAN_ERROR_CODE_MAX = E_VMSG_RANGE_MAX,
	E_SUCCESS = 0
};

#endif // __MWCHAN_ERROR_CODES_H__
