#ifndef xenevent_app_common_h
#define xenevent_app_common_h

#include "threadpool.h"
#include "bufferpool.h"
#include "workqueue.h"
#include "config.h"
#include "message_types.h"

#define ONE_REQUEST_REGION_SIZE sizeof(mt_request_generic_t)

typedef struct _xenevent_globals
{
    // Current index into the buffer_items array. Only goes up, modulo
    // the size.
    volatile uint32_t  curr_buffer_idx;
    buffer_item_t      buffer_items[ BUFFER_ITEM_COUNT ];

    // The buffer of incoming requests
    byte_t       in_request_buf [ ONE_REQUEST_REGION_SIZE * BUFFER_ITEM_COUNT ];

    // Pool of worker threads
    thread_item_t worker_threads[ MAX_THREAD_COUNT ];

    // Have we been asked to shutdown? 
    bool      shutdown_pending;

    // File descriptor to the xenevent device. The dispatcher (main)
    // thread reads commands from this, the worker threads write
    // responses to it. This is split into two file descriptors for
    // debugging. In operations they will be the same.

    int        input_fd;
    int        output_fd;

    //pthread_attr_t attr;

    //struct sched_param schedparam;
    
} xenevent_globals_t;

#endif //xenevent_app_common_h
