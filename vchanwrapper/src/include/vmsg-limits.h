// @MAGICWAND_HEADER@

/*
 * Limits and constants associated with the vmsg type.
 *
 * All vmsg messages have a TLV (Type, Length, Value) format.
 * The minimum message length is four bytes.
 *
 * The currently specified format for TLV messages uses the first four
 * bytes to specify the Type and Length (of the Value part of the message).
 * This is the message "header"
 *
 * The first two bits of the Type provide a "coarse" type specification
 * that controls how the remaining bits of the header are interpreted.
 *
 * Only one of the four possible coarse header formats is currently fully
 * specified and implemented.  For want of a better term, the format that
 * is specified is called the "normal" format.  It is quite likely that
 * this format will be sufficient for currently anticipated uses of vmsg
 * objects by the communication channel.
 * 
 */

 /*
 *
 * Normal Message Header
 *  * First two bits must be set to:  00
 *  * Next 10 bits specify message type [t]
 *  * Final 20 bits specify length of the Value [s]
 *
 * Normal Header Format:
 *   Bit position:  12345678 12345678 12345678 12345678
 *   Value / type:  00tttttt ttttssss ssssssss ssssssss
 *
 */

#include <stdint.h>
#include <stdbool.h>

// Coarse header type values
#define COARSE_TYPE_NORMAL      0
#define COARSE_TYPE_UNUSED_1    1
#define COARSE_TYPE_UNUSED_2    2
#define COARSE_TYPE_UNUSED_3    3

// Bitmask for the coarse type bits.
#define COARSE_TYPE_MASK_BIT_COUNT 2
#define COARSE_TYPE_MASK_OFFSET (32 - COARSE_MASK_TYPE_BIT_COUNT)
#define COARSE_TYPE_MASK (0L | ((2 << (COARSE_TYPE_MASK_BIT_COUNT - 1) - 1) \
		<< COARSE_TYPE_MASK_OFFSET))

/* Normal message headers use bits 3-12 (inclusive) for the type.
 * This allows for 1024 (2**10) normal message types.
 */
// TODO: Some message types should be provided by the library.
#define NORMAL_TYPE_MASK_BIT_COUNT 10
#define NORMAL_TYPE_MASK_OFFSET (32 - \
		(COARSE_TYPE_MASK_BIT_COUNT + NORMAL_TYPE_MASK_BIT_COUNT))
#define NORMAL_TYPE_MASK (0L | ((2 << (NORMAL_TYPE_MASK_BIT_COUNT - 1) - 1) \
		<< NORMAL_TYPE_MASK_OFFSET))


/* Normal messages use bits 13-32 (inclusive) for the size of the value.
 * However, the the maximum possible value size for a non-blocking write
 * is ((2**20) - (4 + 1)).  This is because the current maximum possible shared
 * vchan ringbuffer size is 2**20 and the message hader is 4 bytes long.
 */
#define NORMAL_SIZE_MASK_BIT_COUNT 20
#define NORMAL_SIZE_MASK_OFFSET 0
#define NORMAL_SIZE_MASK (0L | (2 << (NORMAL_SIZE_MASK_BIT_COUNT - 1) - 1))

#define MAX_NORMAL_VALUE_SIZE ((2 << NORMAL_SIZE_MASK_BIT_COUNT) - \
		(sizeof(struct vmsg_hdr) + 1))


