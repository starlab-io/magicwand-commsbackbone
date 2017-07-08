#ifndef xenevent_app_common_h
#define xenevent_app_common_h

#include "threadpool.h"
#include "bufferpool.h"
#include "workqueue.h"
#include "config.h"
#include "message_types.h"

#define ONE_REQUEST_REGION_SIZE sizeof(mt_request_generic_t)

//typedef domid_t uint16_t;
typedef uint16_t domid_t;

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

    domid_t   client_id;
    pthread_t heartbeat_thread;
    bool continue_heartbeat;

    // Network statistics put in XenStore every time the heartbeat is
    // updated
    int        network_stats_socket_ct;
    uint64_t   network_stats_bytes_recv;
    uint64_t   network_stats_bytes_sent;

    struct timeval elapsed;

} xenevent_globals_t;


// Convert errno value to MW standard
int xe_get_mwerrno( int NativeErrno );

//
// Get -1*errno, provided the result will be negative. Otherwise return -1.
//
#define XE_GET_NEG_ERRNO_VAL(__err)                              \
    ( (__err) > 0 ? -(mt_status_t)xe_get_mwerrno((__err)) : (mt_status_t)(-1) )

#define XE_GET_NEG_ERRNO()   XE_GET_NEG_ERRNO_VAL( errno )

// Given mwsocket, find its thread_item_t structure
int
get_worker_thread_for_fd( IN mw_socket_fd_t Fd,
                          OUT thread_item_t ** WorkerThread );


#endif //xenevent_app_common_h
