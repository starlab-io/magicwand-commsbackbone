#ifndef thread_pool_h
#define thread_pool_h

//
// Describes a single thread available for incoming work.
//

#include <pthread.h>
#include <semaphore.h>
#include <stddef.h>

#include "message_types.h"
#include "workqueue.h"
#include "config.h"
#include "mwsocket.h"

typedef struct _thread_item
{
    //
    // Is this item working on an open or opening socket? Use interlocked operators.
    //
    volatile uint32_t in_use;

    //
    // Index of this item in its array - its ID
    //
    int idx;
    
    //
    // The thread that owns this item.
    //
    pthread_t self;

    //
    // How many work items await this thread? Use a semaphore so we
    // can wait for work to arrive.
    //
    sem_t awaiting_work_sem;

    //
    // Synchronizes operations on the thread item during certain
    // operations, e.g. prevents a close() to occur while a send() is
    // in progress.
    //
    sem_t oplock;
    bool  oplock_acquired;

    
    //
    // A fixed-size queue of indices into the buffer item array. The
    // items in this queue are ones that this thread must process. The
    // queue's space is allocated here but it is managed by the
    // workqueue API. Access to it is protected by a mutex.
    //
    
    //work_queue_buffer_idx_t work_queue_space[ BUFFER_ITEM_COUNT ];

    workqueue_t * work_queue;

    //
    // The exported socket value. We do not export our native socket
    // value. See mwsocket.h for details.
    //
    mw_socket_fd_t  sock_fd;

    //
    // Is the socket blocking? If it's non-blocking: (1) it has been
    // set to O_NONBLOCK via fcntl(), (2) it is this socket in the
    // active pollset?
    //
    bool           blocking;

    //
    // For pollset's internal usage
    //
    void         * pollset_data;

    
    //
    // The native socket under management - save it's metadata for
    // easy state lookup.
    //
    int            native_sock_fd;

    //
    // This socket metadata is the native NetBSD values, e.g. for AF_INET6.
    //
    int          sock_domain;
    int          sock_type;
    int          sock_protocol;

    mt_port_t    port_num;

    uint8_t      remote_host[MESSAGE_TYPE_MAX_HOSTNAME_BYTE_LEN];
    
} thread_item_t;


#endif // thread_pool_h
