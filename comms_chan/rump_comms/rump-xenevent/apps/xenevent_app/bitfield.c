#include "bitfield.h"

#include <stdio.h> 
#include "app_common.h"

#include <stddef.h> // size_t
#include <stdint.h>

#include <stdlib.h>
#include <string.h>

#ifndef BIT
#   define BIT(x) (1 << (x))
#endif

struct _bitfield
{
    size_t max_bits;
    uint32_t curr_idx;
    uint8_t bits[1];
};


bitfield_t *
bitfield_alloc(size_t max_bits)
{
    bitfield_t *bf;
    size_t sz = sizeof(*bf) + (max_bits + 7) / 8;
    
    bf = (bitfield_t *) malloc(sz);
    if (bf == NULL)
    {
        MYASSERT( !"malloc" );
        return NULL;
    }
    
    bzero( bf, sz );

    bf->max_bits = max_bits;

    // ???
    //bf->bits = (uint8_t *) (bf + 1);

    return bf;
}


void bitfield_free(bitfield_t *bf)
{
    free(bf);
}


void bitfield_set(bitfield_t *bf, size_t bit)
{
    if (bit >= bf->max_bits)
        return;
    bf->bits[bit / 8] |= BIT(bit % 8);
}


void bitfield_clear(bitfield_t *bf, size_t bit)
{
    if (bit >= bf->max_bits)
        return;
    bf->bits[bit / 8] &= ~BIT(bit % 8);
}


int bitfield_is_set(bitfield_t *bf, size_t bit)
{
    if (bit >= bf->max_bits)
        return 0;
    return !!(bf->bits[bit / 8] & BIT(bit % 8));
}


static int first_zero(uint8_t val)
{
    int i;
    for (i = 0; i < 8; i++) {
        if (!(val & 0x01))
            return i;
        val >>= 1;
    }
    return -1;
}


int bitfield_get_first_zero(bitfield_t *bf)
{
    size_t i;
    for (i = 0; i < (bf->max_bits + 7) / 8; i++) {
        if (bf->bits[i] != 0xff)
            break;
    }
    if (i == (bf->max_bits + 7) / 8)
        return -1;
    i = i * 8 + first_zero(bf->bits[i]);
    if (i >= bf->max_bits)
        return -1;
    return i;
}

/*
  BROKEN
static int first_one(uint8_t val)
{
    int i;
    for (i = 0; i < 8; i++) {
        if (!(val & 0x01))
            return i;
        val >>= 1;
    }
    return -1;
}
    
int bitfield_get_and_clear_nonzero(bitfield_t *bf)
{
    size_t i;
    for (i = 0; i < (bf->max_bits + 7) / 8; i++)
    {
        if (bf->bits[i] != 0x0)
        {
            break;
        }
    }
    if ( i == (bf->max_bits + 7) / 8)
        return -1;
    
    i = i * 8 + first_zero(bf->bits[i]);
    if (i >= bf->max_bits)
        return -1;
    return i;

    }

*/
