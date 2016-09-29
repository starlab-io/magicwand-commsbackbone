#include <stdio.h>
#include <pthread.h>
#include "app_common.h"

#include <stddef.h> // size_t
#include <stdint.h>

#include <stdlib.h>
#include <string.h>

#include <errno.h>

#include "workqueue.h"

struct _workqueue
{
    // work queue id (for debugging)
    int    id;
    size_t item_ct;
    size_t available_slots;
    size_t read_idx;
    size_t write_idx;
    pthread_mutex_t lock;
    work_queue_buffer_idx_t items[1];
};

workqueue_t *
workqueue_alloc( size_t item_count )
{
    static int id = 0;
    
    workqueue_t * wq = NULL;
    int rc = 0;
    
    wq = (workqueue_t *) malloc( sizeof(workqueue_t) +
                                 (item_count * sizeof(work_queue_buffer_idx_t)) );
    if ( NULL == wq )
    {
        MYASSERT( !"malloc" );
        goto ErrorExit;
    }
    
    bzero( wq, sizeof(*wq) );

    wq->id = id++;

    rc = pthread_mutex_init( &wq->lock, NULL );
    if ( rc )
    {
        MYASSERT( !"pthread_mutex_init" );
        workqueue_free( wq );
        wq = NULL;
        goto ErrorExit;
    }

    wq->item_ct = item_count;
    wq->available_slots = wq->item_ct;
    
ErrorExit:
    return wq;
}

void workqueue_free( workqueue_t * wq )
{
    if ( NULL != wq )
    {
        // Verify that there's no work waiting
        MYASSERT( wq->item_ct == wq->available_slots );

        pthread_mutex_destroy( &wq->lock );
        free( wq );
    }
}

int workqueue_enqueue( workqueue_t * wq, work_queue_buffer_idx_t item )
{
    int rc = 0;

    pthread_mutex_lock( &wq->lock );
    
    // Ensure there's space available.
    if ( 0 == wq->available_slots )
    {
        MYASSERT( !"No spots available. Increase size!");
        rc = EBUSY;
        goto ErrorExit;
    }

    // First write to the current idx, then advance it.
    wq->items[ wq->write_idx ] = item;

    ++wq->write_idx;
    wq->write_idx %= wq->item_ct;

    --wq->available_slots;

    DEBUG_PRINT("%d - Enqueued item: %d\n", wq->id, item );

ErrorExit:
    pthread_mutex_unlock( &wq->lock );
    return rc;
}

work_queue_buffer_idx_t
workqueue_dequeue( workqueue_t * wq )
{
    work_queue_buffer_idx_t item = WORK_QUEUE_UNASSIGNED_IDX;

    pthread_mutex_lock( &wq->lock );
    
    if ( wq->item_ct == wq->available_slots )
    {
        // Ensure there's something to get.
        DEBUG_PRINT( "Requested item not available in workqueue %d\n", wq->id);
        goto ErrorExit;
    }

    // First read from the current index, then advance it.
    item = wq->items[ wq->read_idx ];

    ++wq->read_idx;
    wq->read_idx %= wq->item_ct;

    ++wq->available_slots;

    DEBUG_PRINT("%d - dequeued item: %d\n", wq->id, item );

ErrorExit:
    pthread_mutex_unlock( &wq->lock );
    
    return item;
}

int
workqueue_get_contents( workqueue_t * wq,
                        work_queue_buffer_idx_t * items,
                        size_t size )
{
    int rc = 0;
    int i = 0;

    // Unassigned slots filled with 0
    for ( i = 0; i < size; i++ )
    {
        items[i] = WORK_QUEUE_UNASSIGNED_IDX;
    }
    
    pthread_mutex_lock( &wq->lock );
    
    if ( size < wq->item_ct )
    {
        rc = ENOSPC;
        goto ErrorExit;
    }

    int idx = wq->read_idx;
    int ct = wq->item_ct - wq->available_slots;

    i = 0;
    while ( ct > 0 )
    {
        items[i] = wq->items[idx];

        --ct;

        ++idx;
        idx %= wq->item_ct;
    }

ErrorExit:
    pthread_mutex_unlock( &wq->lock );
    
}


bool
workqueue_is_empty( workqueue_t * wq )
{
    pthread_mutex_lock( &wq->lock );
    bool res =  ( wq->item_ct == wq->available_slots );
    pthread_mutex_unlock( &wq->lock );

    return res;
}
