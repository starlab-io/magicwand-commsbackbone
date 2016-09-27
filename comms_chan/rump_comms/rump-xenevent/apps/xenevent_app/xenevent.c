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

#include "rumpdeps.h"

#ifndef NORUMP
#endif // NORUMP

#include <sys/fcntl.h>
#include <unistd.h>
#include <string.h>

#include <errno.h>


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

#ifdef NORUMP
#  define DEBUG_OUTPUT_FILE "outgoing_responses.bin"
#  define DEBUG_INPUT_FILE  "incoming_requests.bin"
#endif

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
    
} xenevent_globals_t;

static xenevent_globals_t g_state;

static int
open_device( void )
{
    int rc = 0;
    size_t size = 0;
    DEBUG_BREAK();    
    g_state.input_fd = open( XENEVENT_DEVICE, O_RDWR );
    
    if ( g_state.input_fd < 0 )
    {
        rc = errno;
        MYASSERT( !"open" );
        goto ErrorExit;
    }

    g_state.output_fd = g_state.input_fd;
    
ErrorExit:
    return rc;
}

#ifdef NORUMP
// Works only outside of Rump until file mapping is understood
static int
DEBUG_open_device( void )
{
    int rc = 0;
    size_t size = 0;

    g_state.input_fd = open( DEBUG_INPUT_FILE, O_RDONLY );
    if ( g_state.input_fd < 0 )
    {
        rc = errno;
        MYASSERT( !"open" );
        goto ErrorExit;
    }

    DEBUG_PRINT( "Opened %s <== FD %d\n", DEBUG_INPUT_FILE, g_state.input_fd );
    
    g_state.output_fd = open( DEBUG_OUTPUT_FILE,
                              O_WRONLY | O_CREAT | O_TRUNC,
                              S_IRUSR | S_IWUSR );
    if ( g_state.output_fd < 0 )
    {
        rc = errno;
        MYASSERT( !"open" );
        goto ErrorExit;
    }

    DEBUG_PRINT( "Opened %s <== FD %d\n", DEBUG_OUTPUT_FILE, g_state.output_fd );
ErrorExit:
    return rc;
}
#endif

static int
reserve_available_buffer_item( OUT buffer_item_t ** BufferItem )
{
    int rc = EBUSY;

    *BufferItem = NULL;

    DEBUG_PRINT( "Looking for availble buffer item\n" );
    // XXXXXXXXXXX update this to start at g_state.curr_buff_idx and circle around
    
    for ( int i = 0; i < BUFFER_ITEM_COUNT; i++ )
    {
        if ( 0 == atomic_cas_32( &g_state.buffer_items[i].in_use, 0, 1 ) )
        {
            DEBUG_PRINT( "Reserving unused buffer %d\n", g_state.buffer_items[i].idx );
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
    //MYASSERT( 1 == prev );
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
            DEBUG_PRINT( "Reserving unused worker thread %d\n",
                         g_state.worker_threads[i].idx );
            // Value was 0 (now it's 1), so it was available and we
            // have acquired it
            rc = 0;
            *WorkerThread = &g_state.worker_threads[i];
            break;
        }
    }
    
    return rc;
}

// XXXXXXXXXXXXXXXX combine above and below functions into one,
// taking into account MT_INVALID_SOCKET_FD

static int
get_worker_thread_for_socket( IN mt_socket_fd_t Socket,
                              OUT thread_item_t ** WorkerThread )
{
    int rc = EBUSY;

    *WorkerThread = NULL;

    DEBUG_PRINT( "Looking for worker thread for socket %d\n", Socket );
    
    for ( int i = 0; i < MAX_THREAD_COUNT; i++ )
    {
        DEBUG_PRINT( "Worker thread %d: busy %d sock %d\n", i,
                     g_state.worker_threads[i].in_use, 
                     g_state.worker_threads[i].sock_fd );
        
        // Look for thread that is already busy and is working on the socket we want
        if ( 1 == atomic_cas_32( &g_state.worker_threads[i].in_use, 1, 1 ) &&
             ( Socket == g_state.worker_threads[i].sock_fd ) )
        {
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
    // Verify that the thread's workqueue is empty
    MYASSERT( workqueue_is_empty( ThreadItem->work_queue ) );
    
    uint32_t prev = atomic_cas_32( &ThreadItem->in_use, 1, 0 );
    
    // Verify that the thread was previously reserved
    //MYASSERT( 1 == prev );
}


//
// Write a response for an internal error encountered by the dispatch
// thread. This runs in the context of the dispatch thread because a
// worker thread couldn't be identified.
//
static int
send_dispatch_error_response( mt_request_generic_t * Request )
{
    mt_response_base_t  response = {0};
    int rc = 0;
    
    // Keep the 0 size
    response.type   = MT_RESPONSE( Request->base.type );
    response.id     = Request->base.id;
    response.sockfd = Request->base.sockfd;

    response.status = MT_STATUS_INTERNAL_ERROR;
    
    rc = write( g_state.output_fd, &response, sizeof(response) );
    MYASSERT( 0 == rc );

    return rc;
}


//
// Processes one request and issues one response. A response MUST be
// issued for every request.
//
static int
process_buffer_item( buffer_item_t * BufferItem )
{
    int rc = 0;
    mt_response_generic_t  response;
    
    mt_request_generic_t * request = (mt_request_generic_t *) BufferItem->region;
    thread_item_t * worker = BufferItem->assigned_thread;

    MYASSERT( NULL != worker );
    
    mt_request_id_t reqtype = request->base.type;

    // Do cleanup after processing this?
    bool cleanup = ( MtRequestSocketClose == reqtype );
    
    DEBUG_PRINT( "Processing buffer item %d\n", BufferItem->idx );
    int req = MT_RESPONSE_MASK & reqtype;
    MYASSERT( MT_IS_REQUEST( reqtype ) );
    
    switch( reqtype )
    {
    case MtRequestSocketCreate:
        rc = xe_net_create_socket( (mt_request_socket_create_t *) request,
                                   (mt_response_socket_create_t *) &response,
                                   worker );
        break;
    case MtRequestSocketConnect:
        rc = xe_net_connect_socket( (mt_request_socket_connect_t *) request,
                                    (mt_response_socket_connect_t *) &response,
                                    worker );
        break;
    case MtRequestSocketClose:
        rc = xe_net_close_socket( (mt_request_socket_close_t *) request,
                                  (mt_response_socket_close_t *) &response,
                                  worker );
        break;
    case MtRequestSocketRead:
        rc = xe_net_read_socket( (mt_request_socket_read_t *) request,
                                 (mt_response_socket_read_t *) &response,
                                 worker );
        break;
    case MtRequestSocketWrite:
        rc = xe_net_write_socket( (mt_request_socket_write_t *) request,
                                  (mt_response_socket_write_t *) &response,
                                  worker );
        break;
    case MtRequestInvalid:
    default:
        MYASSERT( !"Invalid request type" );
    }

    // No matter the results of the network operation, we sent the
    // response back over the device. Write the entire response.
    // XXXXXXXXXX Optimize data written - we don't need to write all of it!
    
    MYASSERT( 0 == rc );

    size_t written = write( g_state.output_fd,
                            &response,
                            sizeof(mt_request_generic_t) );
    if ( written != sizeof(mt_request_generic_t) )
    {
        rc = errno;
        MYASSERT( !"write" );
    }

    if ( cleanup )
    {
        release_buffer_item( BufferItem );
        release_worker_thread( worker );
    }

    return rc;
}

static int
assign_work_to_thread( IN buffer_item_t   * BufferItem,
                       OUT thread_item_t ** AssignedThread,
                       OUT bool           * ProcessFurther )
{
    int rc = 0;

    mt_request_generic_t * request = (mt_request_generic_t *) BufferItem->region;

    mt_request_id_t request_type = MT_RESPONSE_GET_TYPE( request );

    *ProcessFurther = true;
    
    DEBUG_PRINT( "Looking for thread for request\n" );
    DEBUG_BREAK();

    // Any failure here is an "internal" failure. In such a case, we
    // must make sure that we issue a response to the request, since
    // it won't be processed further.
    if ( MtRequestSocketCreate == request_type )
    {
        // Request is for a new socket, so we assign the task to an
        // unassigned thread. However, we must also complete this
        // request so that future work for this socket goes to the
        // right thread.

        *ProcessFurther = false;
        
        rc = reserve_available_worker_thread( AssignedThread );
        if ( rc )
        {
            goto ErrorExit;
        }

        BufferItem->assigned_thread = *AssignedThread;
        rc = process_buffer_item( BufferItem );
    }
    else
    {
        // This request is for an existing connection. Find the thread
        // that services the connection and assign it.
        rc = get_worker_thread_for_socket( request->base.sockfd, AssignedThread );
        if ( rc )
        {
            MYASSERT( !"Unable to find thread for this socket" );
            goto ErrorExit;
        }

        BufferItem->assigned_thread = *AssignedThread;
        rc = workqueue_enqueue( (*AssignedThread)->work_queue, BufferItem->idx );
        if ( rc )
        {
            goto ErrorExit;
        }
    }

    DEBUG_PRINT( "Assigned thread %d work item %d\n",
                 (*AssignedThread)->idx, BufferItem->idx );

ErrorExit:
    // Something here failed. Report the error.
    if ( rc )
    {
        DEBUG_PRINT( "An internal failure occured. Sending error reponse.\n" );
        (void) send_dispatch_error_response( request );
    }

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
    DEBUG_BREAK();
    
    bzero( &g_state, sizeof(g_state) );

    DEBUG_BREAK();

    //
    // Init the buffer items
    //
    for ( int i = 0; i < MAX_THREAD_COUNT; i++ )
    {
        g_state.buffer_items[i].idx    = i;
        g_state.buffer_items[i].offset = ONE_REQUEST_REGION_SIZE * i;
        g_state.buffer_items[i].region =
            &g_state.in_request_buf[ g_state.buffer_items[i].offset ];
    }

    //
    // Init the threads' state, so that posting to the semaphores
    // works later.
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

        sem_init( &g_state.worker_threads[i].awaiting_work_sem,
                  BUFFER_ITEM_COUNT, 0 );
    }

    //
    // Open the device
    //

#ifdef NORUMP
    // DEBUG DEBUG
    rc = DEBUG_open_device();
#else
    rc = open_device();
#endif
    
    if ( rc )
    {
        goto ErrorExit;
    }
#if 0
    for ( int i = 0; i < MAX_THREAD_COUNT; i++ )
    {
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
#endif 
    //
    // Initialize the threads
    //
    
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

    if ( g_state.input_fd > 0 )
    {
        close( g_state.input_fd );
    }

#ifdef NORUMP
    if ( g_state.output_fd > 0 )
    {
        close( g_state.output_fd );
    }
#endif
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
    bool more_processing = false;

    // Forever, read commands from device and dispatch them
    while( true )
    {
        thread_item_t * assigned_thread = NULL;
        buffer_item_t * myitem = NULL;

        DEBUG_PRINT( "Dispatcher looking for available buffer\n" );
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

        DEBUG_PRINT( "Reading %ld bytes from input FD\n", ONE_REQUEST_REGION_SIZE );
        size = read( g_state.input_fd, myitem->region, ONE_REQUEST_REGION_SIZE );
        if ( size != ONE_REQUEST_REGION_SIZE )
        {
            rc = ENOTSUP;
            MYASSERT( !"Received request with invalid size" );
            goto ErrorExit;
        }

        // Assign the buffer to a thread
        rc = assign_work_to_thread( myitem, &assigned_thread, &more_processing );
        if ( rc )
        {
            MYASSERT( !"No thread is available to work on this request." );
            goto ErrorExit;
        }

        if ( more_processing )
        {
            // Tell the thread to process the buffer
        
            DEBUG_PRINT( "Instructing thread %d to resume\n", assigned_thread->idx );
            sem_post( &assigned_thread->awaiting_work_sem );
        }
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
    bool more_processing = false;
    
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
        memset( myitem->region, 'a' + ct, ONE_REQUEST_REGION_SIZE );
        
        // Assign the buffer to a thread
        rc = assign_work_to_thread( myitem, &assigned_thread, &more_processing );
        if ( rc )
        {
            MYASSERT( !"No thread is available to work on this request." );
            goto ErrorExit;
        }

        if ( more_processing )
        {
            // Tell the thread to process the buffer
            DEBUG_PRINT( "Instructing thread %d to resume\n", assigned_thread->idx );
            sem_post( &assigned_thread->awaiting_work_sem );
        }
    } // while

    
ErrorExit:
    return rc;
} // test_message_dispatcher

static int
test_message_dispatcher2( void )
{
    int rc = 0;
    int devfd = 0;
    size_t size = 0;
    int ct = 0;
    bool more_processing = false;
    char buf[100];
    while( ct++ < 10 )
    {
        // Read just sends an event -
        size = read( g_state.input_fd, buf, sizeof(buf) );

        DEBUG_PRINT( "Read %d bytes from input_fd. Waiting a bit\n", 0 );
        sleep(5);
        
    } // while

ErrorExit:
    return rc;
} // test_message_dispatcher2



int main(void)
{
    int rc = 0;
    DEBUG_BREAK();
    
    rc = init_state();
    if ( rc )
    {
        goto ErrorExit;
    }

    // main thread dispatches commands to the other threads
    rc = test_message_dispatcher2();
    //rc = message_dispatcher();
    
ErrorExit:
    fini_state();
    return rc;
}
