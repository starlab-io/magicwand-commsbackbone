#ifndef __VMST_ERROR_CODES_H__
#define __VMST_ERROR_CODES_H__
// @MAGICWAND_HEADER@

/**
 * Error codes used by vmsg.
 * The error codes are always negative numbers.
 * 0 indicates success rather than an error.
 * The available range of error code values is from E_VMSG_RANGE_MIN to
 * E_VMSG_RANGE_MAX.  The defined values range from E_VMSG_RANGE_MIN + 1 to
 * E_VMSG_ERROR_CODE_LIMIT - 1.
 */
#define E_VMSG_RANGE_MIN -10999
#define E_VMSG_RANGE_MAX -10000
enum vmsg_error_code {
	E_VMSG_ERROR_CODE_MIN = E_VMSG_RANGE_MIN,
	E_VMSG_NULL_POINTER,         	// A pointer was NULL.
	E_VMSG_UNINITIALIZED,		// A vmsg was used before it was ready.
	E_VMSG_WRONG_TYPE_FAMILY,	// Did not have expected type family.
	E_VMSG_INVALID_TYPE,		// Supplied vmsg type not valid.
	E_VMSG_INVALID_SIZE_FOR_TYPE,	// Supplied size not valid for the type.
	E_VMSG_SIZE_TOO_LARGE,		// Supplied size exceeds max size.

	E_VMSG_ERROR_CODE_LIMIT,
	E_VMSG_ERROR_CODE_MAX = E_VMSG_RANGE_MAX,
	E_SUCCESS = 0
}

#endif // __VMST_ERROR_CODES_H__

