#ifndef bitfield_h
#define bitfield_h

// From src-netbsd/external/bsd/wpa/dist/src/utils/bitfield.c

//
// This is a basic bitfield implementation. It does not use
// interlocked operations and is not thread-safe.
//

#include <stddef.h> // size_t

struct _bitfield;
typedef struct _bitfield bitfield_t;

bitfield_t * bitfield_alloc(size_t max_bits);
void bitfield_free(bitfield_t *bf);
void bitfield_set(bitfield_t *bf, size_t bit);
void bitfield_clear(bitfield_t *bf, size_t bit);
int bitfield_is_set(bitfield_t *bf, size_t bit);
int bitfield_get_first_zero(bitfield_t *bf);

// Implement me: Get a non-zero bit and return it position. -1 ==> all
// bits are zero.
//int bitfield_get_and_clear_nonzero(bitfield_t *bf);

#endif // bitfield_h
