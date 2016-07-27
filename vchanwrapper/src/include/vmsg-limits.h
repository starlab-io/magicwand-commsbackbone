#ifndef __VMSG_LIMITS_H__
#define __VMSG_LIMITS_H__
// @MAGICWAND_HEADER@

/*
 * Limits and constants associated with the vmsg type.
 *
 * All vmsg messages have a TLV (Type, Length, Value) format.
 *
 * The currently specified format for TLV messages uses the first four
 * bytes as  header to specify the message Type and the Length of the
 * Value part of the message).
 *
 * The minimum message length is four bytes.
 *
 * The first two bits of the Type specify the type family of the message.
 * Each type family has its own interpretation of the message header.
 *
 * Of the four possible type families, only one is currently fully
 * specified and implemented.  Lacking a better term for this type family,
 * it is called "basic".
 * The basic type family will probably be sufficient for the currently
 * anticipated uses of vmsg messages and the mwchan communication channel.
 */

/*
 *
 * Basic Type Family Message Header
 *  * First two bits must be set to:  00
 *  * Next 10 bits specify message type [t]
 *  * Final 20 bits specify length of the Value [s]
 *
 * Basic Type Family Header Format:
 *   Bit position:  12345678 12345678 12345678 12345678
 *   Value / type:  00tttttt ttttssss ssssssss ssssssss
 *
 */

#include "bithelpers.h"

#define VMSG_HEADER_SIZE    4

// vmsg type family header type values
#define VMSG_TYPE_FAMILY_BASIC	   0
#define VMSG_TYPE_FAMILY_UNUSED_1  1
#define VMSG_TYPE_FAMILY_UNUSED_2  2
#define VMSG_TYPE_FAMILY_UNUSED_3  3

// Bitmask for the type family bits.
#define TF_MASK_SIZE 2
#define TF_MASK_OFFSET (32 - TF_MASK_SIZE)
#define TF_MASK ( BITFIELD_MASK(TF_MASK_SIZE, TF_MASK_OFFSET) )  
#define VMSG_GET_TYPE_FAMILY(hdr)   \
	(BITFIELD_GET((hdr), TF_MASK_SIZE, TF_MASK_OFFSET))
#define VMSG_SET_TYPE_FAMILY(hdr, family) 	\
	( BITFIELD_SET((hdr), (family), TF_MASK_SIZE, TF_MASK_OFFSET) )

/* The "basic" type family headers use bits 3-12 (inclusive) for the type.
 * This allows for 1024 (2**10) distinct types.
 * When setting a basic type header, we must also set the type family
 * header so that the final Type value is correct.
 */

#define BTF_FLAGS_MASK_SIZE 10
#define BTF_FLAGS_MASK_OFFSET (32 - \
		(TF_MASK_SIZE + BTF_FLAGS_MASK_SIZE))

#define BTF_MASK ( BITFIELD_MASK(BTF_FLAGS_MASK_SIZE, BTF_FLAGS_MASK_OFFSET) )

#define BTF_GET_FLAGS(hdr) 	\
	( BITFIELD_GET((hdr), BTF_FLAGS_MASK_SIZE, BTF_FLAGS_MASK_OFFSET) )
	
#define BTF_SET_FLAGS(hdr, flags)	\
	( VMST_SET_TYPE_FAMILY((hdr), VMSG_TYPE_FAMILY_BASIC)  | \
	BITFIELD_SET((hdr), (flags), BTF_FLAGS_MASK_SIZE,  \
		BTF_FLAGS_MASK_OFFSET))


/* Basic type family messages use bits 13-32 (inclusive) for the value size.
 * However, the the maximum possible value size for a non-blocking write
 * is ((2**20) - (4 + 1)).  This is because the current maximum possible shared
 * vchan ringbuffer size is 2**20 and the message hader is 4 bytes long.
 */

#define BTF_SIZE_MASK_SIZE (32 - ( TF_MASK_SIZE + BTF_FLAGS_MASK_SIZE))
#define BTF_SIZE_MASK_OFFSET 0
#define BTF_SIZE_MASK ( BITFIELD_MASK(BTF_SIZE_MASK_SIZE, BTF_SIZE_MASK_OFFSET))
#define BTF_GET_SIZE(hdr)   \
	( BITFIELD_GET((hdr), BTF_SIZE_MASK_SIZE, BTF_SIZE_MASK_OFFSET) )
#define BTF_SET_SIZE(hdr, new_size)   \
	( BITFIELD_SET((new_size), (hdr), BTF_SIZE_MASK_SIZE,   \
		BTF_SIZE_MASK_OFFSET))
#define BTF_SIZE_MAX ( (2 << BTF_SIZE_MASK_SIZE) - (VMSG_HDR_SIZE + 1) )

#define BTF_MAKE_HEADER(flags, size)   \
	( BTF_SET_FLAGS(0, (flags)) | BTF_SET_SIZE(0, (size)) )

#define BTF_SET_HEADER(hdr, flags, size) \
	( hdr =  BTF_MAKE_HEADER((flags), (size)) )

#endif // __VMSG_LIMITS_H__

