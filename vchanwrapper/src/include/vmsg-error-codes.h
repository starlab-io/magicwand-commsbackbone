// @MAGICWAND_HEADER@

/**
 * Error codes used by vmsg.
 * The error codes are always negative numbers.
 * The vmsg error codes start at -16,000 and go up.
 * The error code space -16,000 through -15,000 is reserved for vmsg errors.
 */
enum vmsg_error_code {
	E_VMSG_NULL_POINTER = -16000,	// A pointer was NULL.
	E_VMSG_UNINITIALIZED,		// A vmsg was used before it was ready.
	E_VMSG_WRONG_TYPE_FAMILY,	// Did not have expected type family.
	E_VMSG_INVALID_TYPE,		// Supplied vmsg type not valid.
	E_VMSG_INVALID_SIZE_FOR_TYPE,	// Supplied size not valid for the type.
	E_VMSG_SIZE_TOO_LARGE,		// Supplied size exceeds max size.

	E_VMSG_MAX_ERROR_CODE_VALUE,
	E_SUCCESS = 0,
}
