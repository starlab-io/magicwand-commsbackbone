#ifndef buffer_pool_h
#define buffer_pool_h

//
// Describes pool of buffers available for incoming work.
//


#include <stddef.h> // size_t
#include <stdint.h>

#include "threadpool.h"
#include "message_types.h"

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
    union
    {
        byte_t * region;
        mt_request_generic_t * request;
    };
    
    //
    // This item's index in array of these items.
    //
    int idx;
    
} buffer_item_t;


#endif // buffer_pool_h
