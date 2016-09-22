//
// Application for Rump userspace that manages commands from the xen
// shared memory and the associated network connections. The
// application is designed to minimize dynamic memory allocations
// after startup.
//



#include <stdio.h> 
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <rump/rump.h>
#include <rump/rumpdefs.h>

#include <sys/fcntl.h>
#include <unistd.h>
#include <string.h>

#include <errno.h>

#include <atomic.h> // atomic_cas_32 - interlocked compare and swap
#include <sched.h>
#include <pthread.h>
#include <semaphore.h>

#include "networking.h"
#include "app_common.h"

#include "bitfield.h"

#include "threadpool.h"
#include "bufferpool.h"
#include "workqueue.h"

#include "config.h"
#include "message_types.h"


typedef struct _xenevent_globals
{
    // Current index into the buffer_items array. Only goes up, modulo
    // the size.
    uint32_t      curr_buffer_idx;
    buffer_item_t buffer_items[ BUFFER_ITEM_COUNT ];

    // The buffer of incoming requests
    byte_t       in_request_buf [ BUFFER_REQUEST_SIZE * BUFFER_ITEM_COUNT ];

    // Pool of worker threads
    thread_item_t worker_threads[ MAX_THREAD_COUNT ];

    // Have we been asked to shutdown? 
    bool      shutdown_pending;

    // File descriptor to the xenevent device. The dispatcher (main)
    // thread reads commands from this, the worker threads write
    // responses to it.
    int        xe_dev_fd;
    
} xenevent_globals_t;

static xenevent_globals_t g_state;

static int
reserve_available_buffer_item( OUT buffer_item_t ** BufferItem )
{
    int rc = EBUSY;

    *BufferItem = NULL;

    // XXXXXXXXXXX update this to start at g_state.curr_buff_idx and circle around
    
    for ( int i = 0; i < BUFFER_ITEM_COUNT; i++ )
    {
        if ( 0 == atomic_cas_32( &g_state.buffer_items[i].in_use, 0, 1 ) )
        {
            // Value was 0 (now it's 1), so it was available and we
            // have acquired it
            rc = 0;
            *BufferItem = &g_state.buffer_items[i];
            break;
        }
    }
    
    return rc;
}


static void
release_buffer_item( buffer_item_t * BufferItem )
{
    BufferItem->assigned_thread = NULL;

    uint32_t prev = atomic_cas_32( &BufferItem->in_use, 1, 0 );

    // Verify that the buffer was previously reserved
    MYASSERT( 1 == prev );
}

static int
reserve_available_worker_thread( OUT thread_item_t ** WorkerThread )
{
    int rc = EBUSY;

    *WorkerThread = NULL;
    
    for ( int i = 0; i < MAX_THREAD_COUNT; i++ )
    {
        if ( 0 == atomic_cas_32( &g_state.worker_threads[i].in_use, 0, 1 ) )
        {
            // Value was 0 (now it's 1), so it was available and we
            // have acquired it
            rc = 0;
            *WorkerThread = &g_state.worker_threads[i];
            break;
        }
    }
    
    return rc;
}

static void
release_worker_thread( thread_item_t * ThreadItem )
{
    uint32_t prev = atomic_cas_32( &ThreadItem->in_use, 1, 0 );

    // Verify that the buffer had been reserved
    MYASSERT( 1 == prev );
}


static int
process_buffer_item( buffer_item_t * BufferItem )
{
    int rc = 0;

    DEBUG_PRINT( "Processing buffer item %d\n", BufferItem->idx );

    // XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
    // Really, do stuff here!
    

    return rc;
}

static int
assign_work_to_thread( IN buffer_item_t * BufferItem, OUT thread_item_t ** AssignedThread  )
{
    int rc = 0;

    // XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX:
    
    // If the command is for a new connection, then assign a new
    // thread. Otherwise, figure out which thread is working on the
    // established connection.

    // XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX:

    // This is a new connection, so find an available thread
    rc = reserve_available_worker_thread( AssignedThread );    
    if ( rc )
    {
        goto ErrorExit;
    }

    rc = workqueue_enqueue( (*AssignedThread)->work_queue, BufferItem->idx );
    if ( rc )
    {
        goto ErrorExit;
    }

    BufferItem->assigned_thread = *AssignedThread;
    DEBUG_PRINT( "Assigned thread %d work item %d\n",
                 (*AssignedThread)->idx, BufferItem->idx );
ErrorExit:
    return rc;
}

//
// This is the function that the worker thread executes. Here's the
// basic algorith:
//
// forever:
//    wait for one work item to arrive
//    get the work item from the workqueue
//    process the work: if it's a shutdown command, then break out of loop
//
static void *
worker_thread_func( void * Arg )
{
    int rc = 0;
    
    thread_item_t * myitem = (thread_item_t *) Arg;
    buffer_item_t * currbuf;
    bool empty = false;
    
    DEBUG_PRINT( "Thread %d is executing\n", myitem->idx );
    
    while ( true )
    {
        currbuf = NULL;
        
        // Block until work arrives
        DEBUG_PRINT( "Thread %d is waiting for work\n", myitem->idx );

        sem_wait( &myitem->awaiting_work_sem );
        
        DEBUG_PRINT( "Thread %d is working\n", myitem->idx );

        work_queue_buffer_idx_t buf_idx = workqueue_dequeue( myitem->work_queue );
        empty = (INVALID_WORK_QUEUE_IDX == buf_idx);
        if ( empty )
        {
            if ( !g_state.shutdown_pending )
            {
                MYASSERT( !"Programming error: no item in work queue and not shutting down" );
            }
            goto next;
        }

        DEBUG_PRINT( "Thread %d is working on buffer %d\n", myitem->idx, buf_idx );
        
        currbuf = &g_state.buffer_items[buf_idx];
        rc = process_buffer_item( currbuf );
        if ( rc )
        {
            MYASSERT( !"Failed to process buffer item" );
            goto next;
        }
        
    next:
        if ( NULL != currbuf )
        {
            release_buffer_item( currbuf );
        }
        
        if ( g_state.shutdown_pending )
        {
            if ( empty )
            {
                DEBUG_PRINT( "Thread %d is shutting down\n", myitem->idx );
                break;
            }
            else
            {
                DEBUG_PRINT( "Thread %d has more work before shutting down\n", myitem->idx );
            }
        }
    } // while
}


static int
init_state( void )
{
    int rc = 0;
    bzero( &g_state, sizeof(g_state) );

    pthread_mutexattr_t mutex_attr;

    pthread_mutexattr_init( &mutex_attr );
    pthread_mutexattr_settype( &mutex_attr, PTHREAD_MUTEX_RECURSIVE );

    //
    // Initialize the threads
    //
    for ( int i = 0; i < MAX_THREAD_COUNT; i++ )
    {
        g_state.worker_threads[i].idx = i;

        // Alloc the work queue.
        g_state.worker_threads[i].work_queue = workqueue_alloc( BUFFER_ITEM_COUNT );
        if ( NULL == g_state.worker_threads[i].work_queue )
        {
            rc = ENOMEM;
            MYASSERT( !"work_queue: allocation failure" );
            goto ErrorExit;
        }

        sem_init( &g_state.worker_threads[i].awaiting_work_sem, BUFFER_ITEM_COUNT, 0 );
        
        rc = pthread_create( &g_state.worker_threads[ i ].self,
                             NULL,
                             worker_thread_func,
                             &g_state.worker_threads[ i ] );
        if ( rc )
        {
            MYASSERT( !"pthread_create" );
            goto ErrorExit;
        }
    }

    //
    // Init the buffer items
    //
    for ( int i = 0; i < MAX_THREAD_COUNT; i++ )
    {
        g_state.buffer_items[i].idx    = i;
        g_state.buffer_items[i].offset = BUFFER_REQUEST_SIZE * i;
        g_state.buffer_items[i].region =
            &g_state.in_request_buf[ g_state.buffer_items[i].offset ];
    }
    
ErrorExit:
    return rc;
}


static int
fini_state( void )
{
    //
    // Join the threads
    //

    DEBUG_PRINT( "Shutting down all threads\n" );
    
    g_state.shutdown_pending = true;
    
    for ( int i = 0; i < MAX_THREAD_COUNT; i++ )
    {
        sem_post( &g_state.worker_threads[i].awaiting_work_sem );
        
        pthread_join( g_state.worker_threads[i].self, NULL );
        workqueue_free( g_state.worker_threads[i].work_queue );
        sem_destroy( &g_state.worker_threads[i].awaiting_work_sem );

    } // for
}

//
// Reads commands from the xenevent device and dispatches them to
// their respective threads.
//
static int
message_dispatcher( void )
{
    int rc = 0;
    size_t size = 0;
    
    g_state.xe_dev_fd = open( XENEVENT_DEVICE, O_RDWR );
    
    if ( g_state.xe_dev_fd < 0 )
    {
        rc = errno;
        MYASSERT( !"open" );
        goto ErrorExit;
    }

    // Forever, read commands from device and dispatch them
    while( true )
    {
        thread_item_t * assigned_thread = NULL;
        buffer_item_t * myitem = NULL;
        
        // Identify the next available buffer item
        rc = reserve_available_buffer_item( &myitem );
        if ( rc )
        {
            // Failed to find an available buffer item.
            // Yield this thread and try again.
            MYASSERT( !"No buffer items are available" );
            sched_yield();
            continue;
        }

        //
        // We have a buffer item. Read the next command into its
        // buffer. Block until a command arrives.
        //
        size = read( g_state.xe_dev_fd, myitem->region, BUFFER_REQUEST_SIZE );
        if ( size < BUFFER_REQUEST_MIN_SIZE )
        {
            rc = ENOTSUP;
            MYASSERT( !"Received command with invalid size" );
            goto ErrorExit;
        }
        
        // Assign the buffer to a thread
        rc = assign_work_to_thread( myitem, &assigned_thread );
        if ( rc )
        {
            MYASSERT( !"No thread is available to work on this request." );
            goto ErrorExit;
        }

        // Tell the thread to process the buffer
        DEBUG_PRINT( "Instructing thread %d to resume\n", assigned_thread->idx );
        sem_post( &assigned_thread->awaiting_work_sem );
    } // while
    
ErrorExit:
    return rc;
    

} // message_dispatcher

//
// Simulates messages to the threads while we're testing this system
//
static int
test_message_dispatcher( void )
{
    int rc = 0;
    int devfd = 0;
    size_t size = 0;
    int ct = 0;
    
    while( ct++ < 3 )
    {
        thread_item_t * assigned_thread = NULL;
        buffer_item_t * myitem = NULL;
        
        // Identify the next available buffer item
        rc = reserve_available_buffer_item( &myitem );
        if ( rc )
        {
            // Failed to find an available buffer item. We should
            // yield this thread and try again.
            MYASSERT( !"No buffer items are available" );
            sched_yield();
            continue;
        }

        //
        // We have a buffer item. Read the next command into its
        // its region. Block until a command arrives.
        //

        // Simulated block.
        memset( myitem->region, 'a' + ct, BUFFER_REQUEST_SIZE );
        
        // Assign the buffer to a thread
        rc = assign_work_to_thread( myitem, &assigned_thread );
        if ( rc )
        {
            MYASSERT( !"No thread is available to work on this request." );
            goto ErrorExit;
        }

        // Tell the thread to process the buffer
        DEBUG_PRINT( "Instructing thread %d to resume\n", assigned_thread->idx );
        sem_post( &assigned_thread->awaiting_work_sem );
    } // while

    
ErrorExit:
    return rc;
} // test_message_dispatcher



int main(void)
{
    int rc = 0;

    rc = init_state();
    if ( rc )
    {
        goto ErrorExit;
    }

    // main thread dispatches commands to the other threads
    //rc = test_message_dispatcher();
    rc = message_dispatcher();
    
ErrorExit:
    fini_state();
    return rc;
}
