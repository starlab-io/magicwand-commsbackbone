#ifndef __BITHELPERS_H__
#define __BITHELPERS_H__
// @MAGICWAND_HEADER@

/**
 * Simple bitfield and bitmask manipulation helpers.
 *
 * After writing more than I wanted by hand, I opted to simplify
 * with some macros based on:
 *   http://www.coranac.com/documents/working-with-bits-and-bitfields/
 */

//! Get the bit at position @a n.
#define BIT(n)  ( 1 << (n))

//! Make a bit mask of length @a len.
#define BIT_MASK(len) (BIT(len) - 1)

//! Make a bit field mask of length @a len, starting at bit position @a start.
#define BITFIELD_MASK(len, start)   (BIT_MASK(len) << (start))

//! Extract a bitfield of length @a from @a value, starting at the @a start bit.
#define BITFIELD_GET(value, len, start) (((value) >> (start)) & BIT_MASK((len)))

//! Prepare a value for insertion or merging into a bitfield.
#define BITFIELD_PREP(value, len, start) \
		( ((value) & BIT_MASK((len)))  <<  (start) )

//! Insert a value @a part into a bitfield @a whole, @q len size, at @a start.
#define BITFIELD_SET(part, whole, len, start)  \
		( whole  =	\
			((whole) &~ BITFIELD_MASK((len), (start))) |	\
				BITFIELD_PREP((part), (len), (start)))

#endif // @MAGICWAND_HEADER@
