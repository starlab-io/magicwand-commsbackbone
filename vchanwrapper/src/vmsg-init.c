// @MAGICWAND_HEADER@

/*
 * vmsg creation and initialization functionality.
 *
 */

#include <stdint.h>
#include "bithelpers.h"
#include "mwchan-error-codes.h"


/**
 * The vmsg control flags are a bit field.
 * An initialized vmsg has both VCTL_SIZE_SET and VCTL_TYPE_SET.
 */
enum vmsg_ctrl_code {
	//! Set when the vmsg, including .data is a contiguous block of memory.
	VCTL_CONTIGUOUS   = BIT(0),

	//! Set when the vmsg owns the .data member.
	VCTL_DATA_OWNED   = BIT(1),

	//! Set once the vmsg bytes_used has been set.
	VCTL_SIZE_SET     = BIT(2),

	//! Set once the vmsg type has been set.
	VCTL_TYPE_SET     = BIT(3),

	//! The vmsg is intialized.
	VCTL_INITIALIZED  = (BIT(3) & BIT(2))
};


/* This is the on-the-wire format of a vmsg header.  It's not really the
 * wire format, since it's just being written into a shared memory buffer.
 * If we did want to use it as the on-the wire format, we'd need to make sure
 * we dealt with endianness.
 * We don't use it as the in-memory format -- that would just be extra work.
 *
 * Each type-family gets its own struct entry in the anonymous union.
 */
typedef struct vmsg_hdr {
	union {
		uint32_t raw;
		//! The header format of the basic message family.
		struct {
			uint32_t family : 2;
			uint32_t type   : 10;
			uint32_t size   : 20;
		} basic;
	};
} vmsg_hdr_t;


// Library users get an opaque pointer to instances of this struct.
typedef struct vmsg {
	uint32_t vmsg_ctrl_flags;
	size_t allocated_size;
	uint32_t type_family;
	uint32_t type;
	size_t bytes_used;
	size_t read_offset;
	size_t write_offset;
	//! The value part of the TLV message.
	char * data;
} vmsg_t;


