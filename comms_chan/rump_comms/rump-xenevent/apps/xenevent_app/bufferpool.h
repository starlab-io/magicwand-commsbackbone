#ifndef buffer_pool_h
#define buffer_pool_h

//
// Describes pool of buffers available for incoming work.
//


#include <stddef.h> // size_t
#include <stdint.h>

#include "threadpool.h"

typedef uint8_t byte_t;


typedef struct _buffer_item {
    //
    // Is this item in use? Use interlocked operators.
    //
    volatile uint32_t in_use;

    //
    // Which thread has been assigned to work on this buffer?
    //
    thread_item_t * assigned_thread;
    
    //
    // Offset into the buffer where this item resides. Buffers are of
    // fixed length so this is more for debugging.
    //
    size_t offset;

    //
    // Pointer into the buffer where this item resides. Equivalent to &buf[offset]
    //
    byte_t * region;
    
    //
    // This item's index in array of these items.
    //
    int idx;
    
} buffer_item_t;


#endif // buffer_pool_h
