// @MAGICWAND_HEADER@

/**
 * Error codes used by mwchan.
 * The error codes in mwchan are always negative numbers.
 * The mwchan error codes start at -20,000 and go up.
 * The error code space -20,000 through -19,000 is reserved for mwchan errors.
 */
enum mwchan_error_code {
	E_MWCHAN_NULL_POINTER = -20000,     // A pointer was null.
	E_MWCHAN_CLOSED,                    // The struct mwchan was closed.
	E_MWCHAN_MALFORMED,                 // The struct mwchan is malformed.
	E_MWCHAN_MALFORMED_VMSG,            // The struct vmsg is malformed.
	E_MWCHAN_ALLOC_FAILED,              // An allocation failed.

	// All error codes are > -20,000 and < E_MWCHAN_MAX_ERROR_CODE_VALUE.
	E_MWCHAN_MAX_ERROR_CODE_VALUE
};


