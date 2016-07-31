#ifndef __BASIC_TYPE_FAMILY_H__
#define __BASIC_TYPE_FAMILY_H__
// @MAGICWAND_HEADER@

/**
 * The "basic" vmsg type-family provides 10 bits for the message type.
 * This header specifies the pre-existing basic message type values.
 *
 * Basic Type Family Message Header
 *  * First two bits must be set to:  00
 *  * Next 10 bits specify message type [t]
 *  * Final 20 bits specify length of the Value [s]
 *
 * Basic Type Family Header Format:
 *   Bit position:  12345678 12345678 12345678 12345678
 *   Value / type:  00tttttt ttttssss ssssssss ssssssss
 */
typedef enum vmsg_basic_type {
	//! Not really a type -- just compiler tricks.
	// This makes sure the compiler will use a signed int for our enum.
	// The signed int is necessary so that enum mwchan_error_code values
	// can "sort of" occupy the same data space as the basic types.
	BMT_ERROR  = -1,

	// First are all of the pre-defined types.

	//! A flattened struct sk_buff from include/linux/skbuff.h.
	BMT_TYPE_LINUX_SK_BUFF = 1,

	//! Information about a process.
	BMT_TYPE_PROCESS_INFO = 2,

	//! Record from vmstat.
	BMT_TYPE_SYS_VMSTAT_RECORD = 3,

	// Reserved space for custom types.
	//! Custom type values should be greater than this.
	BMT_TYPE_CUSTOM_TYPES_START = 255,

	//! Valid types must be less than this.
	BMT_TYPE_MAX = 1023 	// (2 << 10) - 1
} vmsg_basic_type_t;

#endif // __BASIC_TYPE_FAMILY_H__
