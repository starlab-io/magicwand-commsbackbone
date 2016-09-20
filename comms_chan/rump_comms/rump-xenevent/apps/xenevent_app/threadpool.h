#ifndef thread_pool_h
#define thread_pool_h

//
// Describes a single thread available for incoming work.
//

#include <pthread.h>
#include <semaphore.h>
#include <stddef.h>

#include "networking.h"
#include "workqueue.h"
#include "config.h"


typedef struct _worker_thread
{
    //
    // Is this item working on an open or opening socket? Use interlocked operators.
    //
    volatile uint32_t in_use;

    //
    // Index of this item in its array - it's ID
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
    // A fixed-size queue of indices into the buffer item array. The
    // items in this queue are ones that this thread must process. The
    // queue's space is allocated here but it is managed by the
    // workqueue API. Access to it is protected by a mutex.
    //
    
    work_queue_buffer_idx_t work_queue_space[ BUFFER_ITEM_COUNT ];
    
    workqueue_t * work_queue;

    //
    // The socket under management.
    //
    socket_t sockfd;
    
} thread_item_t;


#endif // thread_pool_h
