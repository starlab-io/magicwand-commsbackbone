/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

//
// Application for Rump userspace that manages commands from the
// protected virtual machine (PVM) over xen shared memory, as well as
// the associated network connections. The application is designed to
// minimize dynamic memory allocations after startup and to handle
// multiple blocking network operations simultaneously.
//
//
// This application processes incoming requests as follows:
//
// 1. The dispatcher function yields its own thread to allow worker
//    threads to process and free up their buffers. We do this because
//    Rump uses a non-preemptive scheduler.
//
// 2. The dispatcher function selects an available buffer from the
//     buffer pool and reads an incoming request into it.
//
// 3. The dispatcher examines the request:
//
//    (a) If the request is for a new socket request, it selects an
//        available thread for the socket.
//
//    (b) Otherwise, the request is for an existing connection. It
//        finds the thread that is handling the connection and assigns
//        the request to that thread.
//
//    A request is assigned to a thread by placing its associated
//    buffer index into the thread's work queue and signalling to the
//    thread that work is available via a semaphore.
//
// 4. Worker threads are initialized on startup and block on a
//    semaphore until work becomes available. When there's work to do,
//    the thread is given control and processes the oldest request in
//    its queue against the socket file descriptor it was assigned. In
//    case the request is for a new socket, it is processed
//    immediately by the dispatcher thread so subsequent requests
//    against that socket can find the thread that handled it.
// 
// The primary functions for a developer to understand are:
//
// worker_thread_func:
// One runs per worker thread. It waits for requests processes them
// against the socket that the worker thread is assigned.
//
// message_dispatcher:
// Finds an available buffer and reads a request into it. Finds the
// thread that will process the request, and, if applicable, adds the
// request to its work queue and signals to it that work is there.
//

#include <stdio.h> 
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "rumpdeps.h"

#include <sys/time.h>

#include <sys/fcntl.h>
#include <unistd.h>
#include <string.h>

#include <errno.h>

#include <sched.h>
#include <pthread.h>
#include <semaphore.h>

#include "networking.h"
#include "app_common.h"

//#include "bitfield.h"

#include "threadpool.h"
#include "bufferpool.h"
#include "workqueue.h"

#include "config.h"
#include "message_types.h"

#ifdef NODEVICE // for debugging outside of Rump
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

    //pthread_attr_t attr;

    //struct sched_param schedparam;
    
} xenevent_globals_t;

static xenevent_globals_t g_state;

/*
static struct timespec 
diff(struct timespec start, struct timespec end)
{
    struct timespec temp;
    if ((end.tv_nsec-start.tv_nsec)<0) {
        temp.tv_sec = end.tv_sec-start.tv_sec-1;
        temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
    } else {
        temp.tv_sec = end.tv_sec-start.tv_sec;
        temp.tv_nsec = end.tv_nsec-start.tv_nsec;
    }
    return temp;
}
*/

static inline void
xe_yield( void )
{
    struct timespec ts = {0, 1}; // 0 seconds, N nanoseconds
 
    // Call nanosleep() until the sleep has occured for the required duration
    while ( ts.tv_nsec > 0 )
    {
        (void) nanosleep( &ts, &ts );
    }
}


static void
debug_print_state( void )
{
#ifdef MYDEBUG
    static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    
    pthread_mutex_lock( &m );
    
    printf("Buffers:\n------------------------------\n");
    for ( int i = 0; i < BUFFER_ITEM_COUNT; i++ )
    {
        buffer_item_t * curr = &g_state.buffer_items[i];
        printf("  %d: used %d thread %d\n",
                    curr->idx, curr->in_use,
                    (curr->assigned_thread ? curr->assigned_thread->idx : -1) );
    }
    printf("\n");

    printf("Threads:\n------------------------------\n");
    for ( int i = 0; i < MAX_THREAD_COUNT; i++ )
    {
        int pending = 0;
        thread_item_t * curr = &g_state.worker_threads[i];
        work_queue_buffer_idx_t queue[ BUFFER_ITEM_COUNT ];
        
        (void) sem_getvalue( &curr->awaiting_work_sem, &pending );
        
        printf("  %d: used %d sock %d, pending items %d\n",
               curr->idx, curr->in_use, curr->sock_fd, pending );

        if ( curr->in_use )
        {
            workqueue_get_contents( curr->work_queue, queue, NUMBER_OF(queue) );
            printf("    queue: " );
            for ( int j = 0; j < NUMBER_OF(queue); j++ )
            {
                printf( "  %d", (signed int)queue[j] );
            }
            printf("\n");
        }
    }
    printf("\n");

    pthread_mutex_unlock( &m );
#endif
}


static int
open_device( void )
{
    int rc = 0;

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


#ifdef NODEVICE
// Works only outside of Rump until file mapping is understood
static int
DEBUG_open_device( void )
{
    int rc = 0;

    g_state.input_fd = open( DEBUG_INPUT_FILE, O_RDONLY );
    if ( g_state.input_fd < 0 )
    {
        rc = errno;
        MYASSERT( !"open: are you running this within Rump?" );
        goto ErrorExit;
    }

    DEBUG_PRINT( "Opened %s <== FD %d\n", DEBUG_INPUT_FILE, g_state.input_fd );
    
    g_state.output_fd = open( DEBUG_OUTPUT_FILE,
                              O_WRONLY | O_CREAT | O_TRUNC,
                              S_IRUSR | S_IWUSR );
    if ( g_state.output_fd < 0 )
    {
        rc = errno;
        MYASSERT( !"open: are you running this within Rump?" );
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

    buffer_item_t * curr = NULL;
    
    *BufferItem = NULL;

    // XXXXXXXXXXX update this to start at g_state.curr_buff_idx and circle around
    
    for ( int i = 0; i < BUFFER_ITEM_COUNT; i++ )
    {
        curr =  &g_state.buffer_items[i];
        MYASSERT( NULL != curr->region );

        if ( 0 == atomic_cas_32( &(curr->in_use), 0, 1 ) )
        {
            DEBUG_PRINT( "Reserving unused buffer %d\n", curr->idx );
            // Value was 0 (now it's 1), so it was available and we
            // have acquired it
            MYASSERT( 1 == curr->in_use );
            rc = 0;
            *BufferItem = curr;
            break;
        }
        else
        {
            DEBUG_PRINT( "NOT reserving used buffer %d\n", curr->idx );
        }
    }

    //MYASSERT( 0 == rc );
    if ( rc )
    {
        DEBUG_PRINT( "All buffers are in use!!\n" );
    }

    return rc;
}


//
// Release the buffer item: unassign its thread and mark it as
// available. It need not have been assigned.
//
static void
release_buffer_item( buffer_item_t * BufferItem )
{
    BufferItem->assigned_thread = NULL;

    DEBUG_PRINT( "Releasing buffer %d\n", BufferItem->idx );

    (void) atomic_cas_32( &BufferItem->in_use, 1, 0 );
}


//
// Find a thread for the given socket. If Socket is
// MT_INVALID_SOCKET_FD, we find an unused thread. Otherwise, we find
// the thread that has already been assigned to work on Socket.
//
static int
get_worker_thread_for_socket( IN mw_socket_fd_t Socket,
                              OUT thread_item_t ** WorkerThread )
{
    int rc = EBUSY;
    thread_item_t * curr = NULL;
    bool found = false;
    
    *WorkerThread = NULL;

    DEBUG_PRINT( "Looking for worker thread for socket %d\n", Socket );
    
    for ( int i = 0; i < MAX_THREAD_COUNT; i++ )
    {
        curr = &g_state.worker_threads[i];

        DEBUG_PRINT( "Worker thread %d: busy %d sock %d\n",
                     i, curr->in_use, curr->sock_fd );

        if ( MT_INVALID_SOCKET_FD == Socket )
        {
            // Look for any available thread and secure ownership of it.
            if ( 0 == atomic_cas_32( &(curr->in_use), 0, 1 ) )
            {
                found = true;
                break;
            }
        }
        else
        {
            // Look for the busy thread that is working on the socket we want
            if ( 1 == atomic_cas_32( &(curr->in_use), 1, 1 ) &&
                 ( Socket == curr->sock_fd ) )
            {
                found = true;
                break;
            }
        }
    } // for

    if ( found )
    {
        rc = 0;
        *WorkerThread = curr;
    }
    else
    {
        if ( MT_INVALID_SOCKET_FD == Socket )
        {
            DEBUG_PRINT( "No thread is available to process this new socket\n" );
        }
        else
        {
            MYASSERT( !"Programming error: cannot find thread that is processing established socket" );
        }
    }
    
    return rc;
}


static void
release_worker_thread( thread_item_t * ThreadItem )
{
    // Verify that the thread's workqueue is empty

    DEBUG_PRINT( "Releasing worker thread %d\n", ThreadItem->idx );

    if ( !workqueue_is_empty( ThreadItem->work_queue ) )
    {
        debug_print_state();
        MYASSERT( !"Releasing thread with non-empty queue!" );
    }
    
    (void)atomic_cas_32( &ThreadItem->in_use, 1, 0 );
    
    // N.B. The thread might have never been reserved
}



static void
set_response_to_internal_error( IN  mt_request_generic_t * Request,
                                OUT mt_response_generic_t * Response )
{
    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );

    Response->base.sig    = MT_SIGNATURE_RESPONSE;
    Response->base.type   = MT_RESPONSE( Request->base.type );
    Response->base.id     = Request->base.id;
    Response->base.sockfd = Request->base.sockfd;

    Response->base.status = MT_STATUS_INTERNAL_ERROR;
}
                                
//
// Write a response for an internal error encountered by the dispatch
// thread. This runs in the context of the dispatch thread because a
// worker thread couldn't be identified.
//
static int
send_dispatch_error_response( mt_request_generic_t * Request )
{
    mt_response_generic_t response = {0};
    int rc = 0;
    
    set_response_to_internal_error( Request, &response );
    
    ssize_t written = write( g_state.output_fd, &response, Request->base.size );
    if ( written != Request->base.size )
    {
        rc = errno;
        MYASSERT( !"Failed to send response" );
    }

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

    mt_request_type_t reqtype = MT_REQUEST_GET_TYPE( request );
    
    //struct timespec t1,t2,t3;
        
    MYASSERT( NULL != worker );
    
    DEBUG_PRINT( "Processing buffer item %d\n", BufferItem->idx );
    MYASSERT( MT_IS_REQUEST( request ) );

    switch( request->base.type )
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
//    case MtRequestSocketRead:
//        rc = xe_net_read_socket( (mt_request_socket_read_t *) request,
//                                 (mt_response_socket_read_t *) &response,
//                                 worker );
//        break;
    case MtRequestSocketSend:
        rc = xe_net_send_socket( (mt_request_socket_send_t *)  request,
                                  (mt_response_socket_send_t *) &response,
                                  worker );
        break;
    case MtRequestSocketBind: 
        rc = xe_net_bind_socket( (mt_request_socket_bind_t *) request,
                                 (mt_response_socket_bind_t *) &response,
                                 worker );
        break;
    case MtRequestSocketListen:
        rc = xe_net_listen_socket( (mt_request_socket_listen_t *) request,
                                   (mt_response_socket_listen_t *) &response,
                                   worker );
        break;
    case MtRequestSocketAccept:
        rc = xe_net_accept_socket( (mt_request_socket_accept_t *) request,
                                   (mt_response_socket_accept_t *) &response,
                                   worker );
        break;
    case MtRequestSocketRecv:
        rc = xe_net_recv_socket( (mt_request_socket_recv_t*) request,
                                 (mt_response_socket_recv_t*) &response,
                                  worker );
        break;
    case MtRequestSocketRecvFrom:
        rc = xe_net_recvfrom_socket( ( mt_request_socket_recv_t*) request,
                                     ( mt_response_socket_recvfrom_t* ) &response,
                                     worker );
        break;
    case MtRequestInvalid:
    default:
        MYASSERT( !"Invalid request type" );
        // Send back an internal error
        set_response_to_internal_error( request, &response );
        response.base.type = MtResponseInvalid;
        break;
    }

    // No matter the results of the network operation, we sent the
    // response back over the device. Write the entire response.
    // XXXXXXXXXX Optimize data written - we don't need to write all of it!
    
    MYASSERT( 0 == rc );
    MYASSERT( MT_IS_RESPONSE( &response ) );
    
    //clock_gettime(CLOCK_REALTIME, &t1);

    // In accept's success case, assign the new socket to an available thread
    if ( MtRequestSocketAccept == request->base.type
         && response.base.status >= 0 )
    {
        thread_item_t * accept_thread = NULL;
        rc = get_worker_thread_for_socket( MT_INVALID_SOCKET_FD, &accept_thread );
        if ( rc )
        {
            MYASSERT( !"Failed to find thread to service accepted socket" );
            // Destroy the new socket and fail the request
            close( response.base.status );
            response.base.status = -1;
        }
        else
        {
            // A thread was available - record the assignment now
            accept_thread->sock_fd = response.base.status;
            accept_thread->native_sock_fd =
                MW_SOCKET_GET_FD( response.base.status );
        }
    }

    size_t written = write( g_state.output_fd,
                            &response,
                            sizeof(response) );

    //clock_gettime(CLOCK_REALTIME, &t2);
    //t3 = diff(t1,t2);
    //DEBUG_PRINT( "Time of Execution for write(). sec: %ld  nsec: %ld\n",
             //t3.tv_sec, t3.tv_nsec);

    if ( written != response.base.size )
    {
        rc = errno;
        MYASSERT( !"write" );
    }

    // We're done with the buffer item
    release_buffer_item( BufferItem );

    // If we closed the socket, we can release the thread.
    if ( MtRequestSocketClose == reqtype )
    {
        release_worker_thread( worker );
    }

    return rc;
}

/**
 * assign_work_to_thread
 *
 * Identify the thread that should process the given buffer item. In
 * the case of a request to create a socket, the processing is done
 * here.
 *
 * Exit condition: One of these is true -
 * (1) the buffer item has been processed, or
 * (2) the buffer item has been successfully assigned to
 *     a worker thread, or
 * (3) an error reponse has been written.
 */
static int
assign_work_to_thread( IN buffer_item_t   * BufferItem,
                       OUT thread_item_t ** AssignedThread,
                       OUT bool           * ProcessFurther )
{
    int rc = 0;
    mt_request_generic_t * request = (mt_request_generic_t *) BufferItem->region;
    mt_request_type_t request_type = MT_RESPONSE_GET_TYPE( request );
    
    // Release the buffer item, unless it is successfully assigned to the worker thread.
    bool release_item = true;

    // Typically the caller needs to do more work on this buffer
    *ProcessFurther = true;
    
    DEBUG_BREAK();
    DEBUG_PRINT( "Looking for thread for request in buffer item %d\n",
                 BufferItem->idx );

    // Any failure here is an "internal" failure. In such a case, we
    // must make sure that we issue a response to the request, since
    // it won't be processed further.
    if ( MtRequestSocketCreate == request_type ) 
    
    {
        // Request is for a new socket, so we assign the task to an
        // unassigned thread. The thread and socket will be bound
        // together during the socket's lifetime. However, we must
        // also complete this request now so that future work for this
        // socket goes to the right thread. So in this special case,
        // the buffer item is not processed by the thread that's
        // assigned to the socket.

        *ProcessFurther = false;
        
        rc = get_worker_thread_for_socket( MT_INVALID_SOCKET_FD, AssignedThread );
        if ( rc )
        {
            // No worker thread is available, but we must associate a
            // new socket and an available thread now. We could yield
            // and try again. For now, give up.
            goto ErrorExit;
        }

        BufferItem->assigned_thread = *AssignedThread;
        rc = process_buffer_item( BufferItem );
    }
    /*
    else if ( MtRequestSocketConnect == request_type ||
              MtRequestSocketWrite   == request_type || 
              MtRequestSocketClose   == request_type )
    {
        *ProcessFurther = false;

        // Assign thread 0 and comment out next two statements
        rc = get_worker_thread_for_socket( request->base.sockfd, AssignedThread );

        BufferItem->assigned_thread = *AssignedThread;

        rc = process_buffer_item( BufferItem );
    }
    */
    else
    {
        // This request is for an existing connection. Find the thread
        // that services the connection and assign it.
        //DEBUG_BREAK();
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

        // The buffer item was successfully assigned to the worker thread
        release_item = false;
    }

    DEBUG_PRINT( "Work item %d assigned to thread %d\n",
                 BufferItem->idx, (*AssignedThread)->idx );

ErrorExit:
    // Something here failed. Report the error. The worker thread will
    // not release the buffer item, so do it here.
    if ( rc )
    {
        *ProcessFurther = false;
        DEBUG_PRINT( "An internal failure occured. Sending error reponse.\n" );
        (void) send_dispatch_error_response( request );
    }

    if ( release_item )
    {
        release_buffer_item( BufferItem );
    }

    return rc;
}

//
// This is the function that the worker thread executes. Here's the
// basic algorithm:
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
        DEBUG_PRINT( "**** Thread %d is waiting for work\n", myitem->idx );

        sem_wait( &myitem->awaiting_work_sem );
        
        DEBUG_PRINT( "**** Thread %d is working\n", myitem->idx );
        DEBUG_BREAK();

        work_queue_buffer_idx_t buf_idx = workqueue_dequeue( myitem->work_queue );
        empty = (WORK_QUEUE_UNASSIGNED_IDX == buf_idx);
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

    pthread_exit(Arg);
}


static int
init_state( void )
{
    int rc = 0;
    
    bzero( &g_state, sizeof(g_state) );

    //
    // Init the buffer items
    //
    for ( int i = 0; i < BUFFER_ITEM_COUNT; i++ )
    {
        buffer_item_t * curr = &g_state.buffer_items[i];

        curr->idx    = i;
        curr->offset = ONE_REQUEST_REGION_SIZE * i;
        curr->region = &g_state.in_request_buf[ curr->offset ];
    }

    //
    // Init the threads' state, so that posting to the semaphores
    // works as soon as the threads start up.
    //
    
    for ( int i = 0; i < MAX_THREAD_COUNT; i++ )
    {
        thread_item_t * curr = &g_state.worker_threads[i];
        curr->idx = i;

        // Alloc the work queue.
        curr->work_queue = workqueue_alloc( BUFFER_ITEM_COUNT );
        if ( NULL == curr->work_queue )
        {
            rc = ENOMEM;
            MYASSERT( !"work_queue: allocation failure" );
            goto ErrorExit;
        }

        sem_init( &curr->awaiting_work_sem, BUFFER_ITEM_COUNT, 0 );
        //sem_init( &curr->awaiting_work_sem, 0, 1 );
        //sem_init( &curr->awaiting_work_sem, 0, 0 );
    }

    //
    // Open the device
    //

#ifdef NODEVICE
    rc = DEBUG_open_device();
    if ( rc )
    {
        DEBUG_PRINT( "Can't open device in test mode. Ignoring.\n" );
        rc = 0;
    }
#else
    rc = open_device();
#endif
    
    if ( rc )
    {
        goto ErrorExit;
    }

    /*
    rc = pthread_attr_init(&g_state.attr);

    if ( rc )
    {
        MYASSERT( !"pthread_attr_init" );
        goto ErrorExit;
    }

    rc = pthread_attr_setdetachstate(&g_state.attr, PTHREAD_CREATE_DETACHED);

    if ( rc )
    {
        MYASSERT( !"pthread_attr_setdetachstate" );
        goto ErrorExit;
    }

    g_state.schedparam.sched_priority = 20;

    //pthread_attr_setinheritsched(&g_state.attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setinheritsched(&g_state.attr, PTHREAD_INHERIT_SCHED);
    //pthread_attr_setschedpolicy(&g_state.attr, SCHED_RR);
    pthread_attr_setschedpolicy(&g_state.attr, SCHED_FIFO);
    pthread_attr_setschedparam(&g_state.attr, &g_state.schedparam);
    */
    //
    // Start up the threads
    //
    for ( int i = 0; i < MAX_THREAD_COUNT; i++ )
    {
        rc = pthread_create( &g_state.worker_threads[ i ].self,
                             NULL,//&g_state.attr,
                             worker_thread_func,
                             &g_state.worker_threads[ i ] );
        if ( rc )
        {
            MYASSERT( !"pthread_create" );
            goto ErrorExit;
        }
    }

    DEBUG_PRINT( "All %d threads have been created\n", MAX_THREAD_COUNT );
    //xe_yield();
    sched_yield();
    
ErrorExit:
    return rc;
}


static int
fini_state( void )
{
    //
    // Force all threads to exit: set shutdown pending and signal all their
    // semaphores so they exit, then join them and clean up their resources.
    //

    DEBUG_PRINT( "Shutting down all threads\n" );
    
    g_state.shutdown_pending = true;
    
    for ( int i = 0; i < MAX_THREAD_COUNT; i++ )
    {
        thread_item_t * curr = &g_state.worker_threads[i];
        
        sem_post( &curr->awaiting_work_sem );
        
        pthread_join( curr->self, NULL );
        //pthread_attr_destroy(&g_state.attr);
        workqueue_free( curr->work_queue );
        sem_destroy( &curr->awaiting_work_sem );
    }

    if ( g_state.input_fd > 0 )
    {
        close( g_state.input_fd );
    }

#ifdef NODEVICE
    if ( g_state.output_fd > 0 )
    {
        close( g_state.output_fd );
    }
#endif

    return 0;
}

//
// Reads commands from the xenevent device and dispatches them to
// their respective threads.
//
static int
message_dispatcher( void )
{
    int rc = 0;
    ssize_t size = 0;
    bool more_processing = false;

    //struct timespec t1,t2,t3;

    // Forever, read commands from device and dispatch them, allowing
    // the other threads to execute.
    while( true )
    {
        thread_item_t * assigned_thread = NULL;
        buffer_item_t * myitem = NULL;
        mt_request_type_t request_type;

        //clock_gettime(CLOCK_REALTIME, &t1);

        // Always allow other threads to run in case there's work.
        //xe_yield();

        DEBUG_PRINT( "Dispatcher looking for available buffer\n" );
        // Identify the next available buffer item
        rc = reserve_available_buffer_item( &myitem );
        if ( rc )
        {
            // Failed to find an available buffer item. Yield and try again.
            DEBUG_PRINT( "Warning: No buffer items are available. Yielding for now." );
            continue;
        }

        //
        // We have a buffer item. Read the next command into its
        // buffer. Block until a command arrives.
        //

        DEBUG_PRINT( "Attempting to read %ld bytes from input FD\n",
                     ONE_REQUEST_REGION_SIZE );

        size = read( g_state.input_fd, myitem->region, ONE_REQUEST_REGION_SIZE );
        if ( size < (ssize_t) sizeof(mt_request_base_t) ||
             myitem->request->base.size > ONE_REQUEST_REGION_SIZE )
        {
            // Handle underflow or overflow
            if ( 0 == size )
            {
                // end of input - normal exit
                goto ErrorExit;
            }
            
            // Otherwise we have invalid input
            rc = EINVAL;
            if ( 0 != size )
            {
                MYASSERT( !"Received request with invalid size" );
            }
            goto ErrorExit;
        }

        // Assign the buffer to a thread. If fails, reports to PVM but
        // keeps going.
        rc = assign_work_to_thread( myitem, &assigned_thread, &more_processing );
        if ( rc )
        {
            MYASSERT( !"No thread is available to work on this request." );
            continue;
        }

        if ( more_processing )
        {
            // Tell the thread to process the buffer
            DEBUG_PRINT( "Instructing thread %d to resume\n", assigned_thread->idx );

            sem_post( &assigned_thread->awaiting_work_sem );
        }

        // Remember: we'll yield next...
        request_type = MT_RESPONSE_GET_TYPE( myitem->request );
        if ( request_type == MtRequestSocketConnect || request_type == MtRequestSocketAccept)
        {
            xe_yield();
        } else 
        {
            sched_yield();
        }

        //clock_gettime(CLOCK_REALTIME, &t2);
        //t3 = diff(t1,t2);
        //DEBUG_PRINT( "Time of Execution message_dispatcher loop. sec: %ld  nsec: %ld\n",
                 //t3.tv_sec, t3.tv_nsec);


    } // while
    
ErrorExit:
    xe_yield();
    return rc;

} // message_dispatcher


#if 0 // test code

//
// Simulates messages to the threads while we're testing this system
//
static int
test_message_dispatcher( void )
{
    int rc = 0;
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
            xe_yield();
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
    int ct = 0;
    char buf[100];

    while( ct++ < 10 )
    {
        // Read just sends an event -
        ssize_t size = read( g_state.input_fd, buf, sizeof(buf) );

        DEBUG_PRINT( "Read %ld bytes from input_fd. Waiting a bit\n", size );
        sleep(5);
        
    } // while

    return rc;
} // test_message_dispatcher2

#endif // 0

int main(void)
{
    int rc = 0;
  
    rc = init_state();
    if ( rc )
    {
        goto ErrorExit;
    }

    // main thread dispatches commands to the other threads
    //rc = test_message_dispatcher2();
    rc = message_dispatcher();

ErrorExit:
    fini_state();
    return rc;
}

