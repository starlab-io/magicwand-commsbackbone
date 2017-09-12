/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

/**
 * @file    xenevent.c
 * @author  Matt Leinhos
 * @date    29 March 2017
 * @version 0.1
 * @brief   MagicWand INS request dispatcher/multiplexer.
 *
 * Application for Rump userspace that manages commands from the
 * protected virtual machine (PVM) over xen shared memory, as well as
 * the associated network connections. The application is designed to
 * minimize dynamic memory allocations after startup and to handle
 * multiple blocking network operations simultaneously.
 *
 *
 * This application processes incoming requests as follows:
 *
 * 1. The dispatcher function yields its own thread to allow worker
 *    threads to process and free up their buffers. We do this because
 *    Rump uses a non-preemptive scheduler.
 *
 * 2. The dispatcher function selects an available buffer from the
 *     buffer pool and reads an incoming request into it.
 *
 * 3. The dispatcher examines the request:
 *
 *    (a) If the request is for a new socket request, it selects an
 *        available thread for the socket.
 *
 *    (b) Otherwise, the request is for an existing connection. It
 *        finds the thread that is handling the connection and assigns
 *        the request to that thread.
 *
 *    A request is assigned to a thread by placing its associated
 *    buffer index into the thread's work queue and signalling to the
 *    thread that work is available via a semaphore.
 *
 * 4. Worker threads are initialized on startup and block on a
 *    semaphore until work becomes available. When there's work to do,
 *    the thread is given control and processes the oldest request in
 *    its queue against the socket file descriptor it was assigned. In
 *    case the request is for a new socket, it is processed
 *    immediately by the dispatcher thread so subsequent requests
 *    against that socket can find the thread that handled it.
 * 
 * The primary functions for a developer to understand are:
 *
 * worker_thread_func:
 * One runs per worker thread. It waits for requests processes them
 * against the socket that the worker thread is assigned.
 *
 * message_dispatcher:
 * Finds an available buffer and reads a request into it. Finds the
 * thread that will process the request, and, if applicable, adds the
 * request to its work queue and signals to it that work is there.
 */

#include <stdio.h> 
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "rumpdeps.h"

#include <sys/time.h>
#include <sys/ioctl.h>

#include <sys/fcntl.h>
#include <unistd.h>
#include <string.h>

#include <errno.h>

#include <sched.h>
#include <pthread.h>
#include <semaphore.h>

#include "networking.h"
#include "user_common.h"
#include "pollset.h"

#include "xenevent_app_common.h"

#include "mwerrno.h"

#include "ins-ioctls.h" // needs xenevent_app_common.h for domid_t

#ifdef NODEVICE // for debugging outside of Rump
#  define DEBUG_OUTPUT_FILE "outgoing_responses.bin"
#  define DEBUG_INPUT_FILE  "incoming_requests.bin"
#endif


// Global data
xenevent_globals_t g_state;

#define XE_PROCESS_IN_MAIN_THREAD( _mytype )      \
    ( MtRequestSocketCreate == _mytype ||         \
      MtRequestSocketAttrib == _mytype ||         \
      MtRequestPollsetQuery == _mytype )

#define XE_PROCESS_IN_MAIN_THREAD_NO_WORKER( _mytype )  \
    ( MtRequestPollsetQuery == _mytype )


// These are the request types we don't want stomping on each other.
#define REQUEST_REQUIRES_OPLOCK( _req )                     \
    ( MtRequestSocketSend     == (_req)->base.type ||       \
      MtRequestSocketShutdown == (_req)->base.type ||       \
      MtRequestSocketClose    == (_req)->base.type )


//
// Errno support
//

int xe_get_mwerrno( int NativeErrno )
{
    struct errmap
    {
        int native;
        int mwerr;
    };
    static struct errmap _emap[] =
        {
            {EPERM, MW_EPERM },
            {ENOENT, MW_ENOENT },
            {ESRCH, MW_ESRCH },
            {EINTR, MW_EINTR },
            {EIO, MW_EIO },
            {ENXIO, MW_ENXIO },
            {E2BIG, MW_E2BIG },
            {ENOEXEC, MW_ENOEXEC },
            {EBADF, MW_EBADF },
            {ECHILD, MW_ECHILD },
            {EAGAIN, MW_EAGAIN },
            {ENOMEM, MW_ENOMEM },
            {EACCES, MW_EACCES },
            {EFAULT, MW_EFAULT },
            {ENOTBLK, MW_ENOTBLK },
            {EBUSY, MW_EBUSY },
            {EEXIST, MW_EEXIST },
            {EXDEV, MW_EXDEV },
            {ENODEV, MW_ENODEV },
            {ENOTDIR, MW_ENOTDIR },
            {EISDIR, MW_EISDIR },
            {EINVAL, MW_EINVAL },
            {ENFILE, MW_ENFILE },
            {EMFILE, MW_EMFILE },
            {ENOTTY, MW_ENOTTY },
            {ETXTBSY, MW_ETXTBSY },
            {EFBIG, MW_EFBIG },
            {ENOSPC, MW_ENOSPC },
            {ESPIPE, MW_ESPIPE },
            {EROFS, MW_EROFS },
            {EMLINK, MW_EMLINK },
            {EPIPE, MW_EPIPE },
            {EDOM, MW_EDOM },
            {ERANGE, MW_ERANGE },
            {EDEADLK, MW_EDEADLK },
            {ENAMETOOLONG, MW_ENAMETOOLONG },
            {ENOLCK, MW_ENOLCK },
            {ENOSYS, MW_ENOSYS },
            {ENOTEMPTY, MW_ENOTEMPTY },
            {ELOOP, MW_ELOOP },
            {EWOULDBLOCK, MW_EWOULDBLOCK },
            {ENOMSG, MW_ENOMSG },
            {EIDRM, MW_EIDRM },
            //{ECHRNG, MW_ECHRNG },
            //{EL2NSYNC, MW_EL2NSYNC },
            //{EL3HLT, MW_EL3HLT },
            //{EL3RST, MW_EL3RST },
            //{ELNRNG, MW_ELNRNG },
            //{EUNATCH, MW_EUNATCH },
            //{ENOCSI, MW_ENOCSI },
            //{EL2HLT, MW_EL2HLT },
            //{EBADE, MW_EBADE },
            //{EBADR, MW_EBADR },
            //{EXFULL, MW_EXFULL },
            //{ENOANO, MW_ENOANO },
            //{EBADRQC, MW_EBADRQC },
            //{EBADSLT, MW_EBADSLT },
            //{EDEADLOCK, MW_EDEADLOCK },
            //{EBFONT, MW_EBFONT },
            {ENOSTR, MW_ENOSTR },
            {ENODATA, MW_ENODATA },
            {ETIME, MW_ETIME },
            {ENOSR, MW_ENOSR },
            //{ENONET, MW_ENONET },
            //{ENOPKG, MW_ENOPKG },
            {EREMOTE, MW_EREMOTE },
            {ENOLINK, MW_ENOLINK },
            //{EADV, MW_EADV },
            //{ESRMNT, MW_ESRMNT },
            //{ECOMM, MW_ECOMM },
            {EPROTO, MW_EPROTO },
            {EMULTIHOP, MW_EMULTIHOP },
            //{EDOTDOT, MW_EDOTDOT },
            {EBADMSG, MW_EBADMSG },
            {EOVERFLOW, MW_EOVERFLOW },
            //{ENOTUNIQ, MW_ENOTUNIQ },
            //{EBADFD, MW_EBADFD },
            //{EREMCHG, MW_EREMCHG },
            //{ELIBACC, MW_ELIBACC },
            //{ELIBBAD, MW_ELIBBAD },
            //{ELIBSCN, MW_ELIBSCN },
            //{ELIBMAX, MW_ELIBMAX },
            //{ELIBEXEC, MW_ELIBEXEC },
            //{EILSEQ, MW_EILSEQ },
            //{ERESTART, MW_ERESTART },
            //{ESTRPIPE, MW_ESTRPIPE },
            {EUSERS, MW_EUSERS },
            {ENOTSOCK, MW_ENOTSOCK },
            {EDESTADDRREQ, MW_EDESTADDRREQ },
            {EMSGSIZE, MW_EMSGSIZE },
            {EPROTOTYPE, MW_EPROTOTYPE },
            {ENOPROTOOPT, MW_ENOPROTOOPT },
            {EPROTONOSUPPORT, MW_EPROTONOSUPPORT },
            {ESOCKTNOSUPPORT, MW_ESOCKTNOSUPPORT },
            {EOPNOTSUPP, MW_EOPNOTSUPP },
            {EPFNOSUPPORT, MW_EPFNOSUPPORT },
            {EAFNOSUPPORT, MW_EAFNOSUPPORT },
            {EADDRINUSE, MW_EADDRINUSE },
            {EADDRNOTAVAIL, MW_EADDRNOTAVAIL },
            {ENETDOWN, MW_ENETDOWN },
            {ENETUNREACH, MW_ENETUNREACH },
            {ENETRESET, MW_ENETRESET },
            {ECONNABORTED, MW_ECONNABORTED },
            {ECONNRESET, MW_ECONNRESET },
            {ENOBUFS, MW_ENOBUFS },
            {EISCONN, MW_EISCONN },
            {ENOTCONN, MW_ENOTCONN },
            {ESHUTDOWN, MW_ESHUTDOWN },
            {ETOOMANYREFS, MW_ETOOMANYREFS },
            {ETIMEDOUT, MW_ETIMEDOUT },
            {ECONNREFUSED, MW_ECONNREFUSED },
            {EHOSTDOWN, MW_EHOSTDOWN },
            {EHOSTUNREACH, MW_EHOSTUNREACH },
            {EALREADY, MW_EALREADY },
            {EINPROGRESS, MW_EINPROGRESS },
            {ESTALE, MW_ESTALE },
            //{EUCLEAN, MW_EUCLEAN },
            //{ENOTNAM, MW_ENOTNAM },
            //{ENAVAIL, MW_ENppAVAIL },
            //{EISNAM, MW_EISNAM },
            //{EREMOTEIO, MW_EREMOTEIO },
            //{EDQUOT, MW_EDQUOT },
            //{ENOMEDIUM, MW_ENOMEDIUM },
            //{EMEDIUMTYPE, MW_EMEDIUMTYPE },
            //{ECANCELED, MW_ECANCELED },
            //{ENOKEY, MW_ENOKEY },
            //{EKEYEXPIRED, MW_EKEYEXPIRED },
            //{EKEYREVOKED, MW_EKEYREVOKED },
            //{EKEYREJECTED, MW_EKEYREJECTED },
            //{EOWNERDEAD, MW_EOWNERDEAD },
            //{ENOTRECOVERABLE, MW_ENOTRECOVERABLE },
            //{ERFKILL, MW_ERFKILL },
            //{EHWPOISON, MW_EHWPOISON },
        };
    int rc = 0;

    if ( 0 == NativeErrno )
    {
        goto ErrorExit;
    }

    for ( int i = 0; i < NUMBER_OF( _emap ); ++i )
    {
        if ( _emap[i].native == NativeErrno )
        {
            rc = _emap[i].mwerr;
            goto ErrorExit;
        }
    }
    // Generic error: EPERM
    rc = MW_EPERM;

ErrorExit:
    return rc;
}


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
        
        printf("  %d: used %d sock %x/%d, pending items %d\n",
               curr->idx, curr->in_use,
               curr->public_fd, curr->local_fd,
               pending );

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


/**
 * @brief Release the buffer item.
 *
 * Unassign its thread and mark it as available. It need not have been
 * assigned.
 */
static void
release_buffer_item( buffer_item_t * BufferItem )
{
    int prev = 0;

    prev = atomic_cas_32( &BufferItem->in_use, 1, 0 );

    if ( prev )
    {
        BufferItem->assigned_thread = NULL;
        DEBUG_PRINT( "Released buffer %d\n", BufferItem->idx );
    }
}


/**
 * @brief Find the thread for the given socket.
 *
 * If Socket is MT_INVALID_SOCKET_FD, acquire an unused thread and
 * assign it to the socket. Otherwise, get the the thread that has
 * already been assigned to work on Socket.
 */
int
get_worker_thread_for_fd( IN mw_socket_fd_t Fd,
                          OUT thread_item_t ** WorkerThread )
{
    int rc = EBUSY;
    thread_item_t * curr = NULL;
    bool found = false;
    
    *WorkerThread = NULL;

    DEBUG_PRINT( "Looking for worker thread for socket %x\n", Fd );

    if ( MT_INVALID_SOCKET_FD == Fd )
    {
        for ( int i = 0; i < MAX_THREAD_COUNT; i++ )
        {
            curr = &g_state.worker_threads[i];

            DEBUG_PRINT( "Worker thread %d: busy %d sock %x\n",
                         i, curr->in_use, curr->public_fd );

            // Look for any available thread and secure ownership of it.
            if ( 0 == atomic_cas_32( &(curr->in_use), 0, 1 ) )
            {
                found = true;
                break;
            }
        }
    }
    else
    {
        // This socket has already been assigned a thread. It's LSB is
        // the index into the thread array of its servicer. The code
        // below verifies this claim.
        int idx = MW_SOCKET_GET_ID( Fd );

        if ( idx >= MAX_THREAD_COUNT )
        {
            MYASSERT( !"Error: socket's ID is too high\n" );
            rc = EINVAL;
            goto ErrorExit;
        }

        curr = &g_state.worker_threads[ idx ];
        if ( 0 == curr->in_use )
        {
            MYASSERT( !"Error: socket's thread is not busy\n" );
            rc = EINVAL;
            goto ErrorExit;
        }

        if ( Fd != curr->public_fd )
        {
            MYASSERT( !"Internal error: socket's ID mismatches internal one\n" );
            rc = EINVAL;
            goto ErrorExit;
        }

        found = true;
    }

    if ( found )
    {
        rc = 0;
        *WorkerThread = curr;
    }

ErrorExit:
    return rc;
}


/**
 * @brief Release worker thread so it can be assigned to a new socket.
 *
 * Reset its state too.
 */
static void
release_worker_thread( thread_item_t * ThreadItem )
{
    // Verify that the thread's workqueue is empty
    int prev = 0;
    
    if ( !workqueue_is_empty( ThreadItem->work_queue ) )
    {
        debug_print_state();
        MYASSERT( !"Releasing thread with non-empty queue!" );
    }

    MYASSERT( !ThreadItem->oplock_acquired );
    
    // Clean up the struct for the next usage
    (void) xe_net_internal_close_socket( ThreadItem );

    ThreadItem->poll_events = 0;
    ThreadItem->state_flags = 0;
    ThreadItem->sock_domain = 0;
    ThreadItem->sock_type   = 0;
    ThreadItem->sock_protocol  = 0;
    ThreadItem->bound_port_num = 0;

    bzero( &ThreadItem->remote_host, sizeof(ThreadItem->remote_host) );
    
    // N.B. The thread might have never been reserved    
    prev = atomic_cas_32( &ThreadItem->in_use, 1, 0 );
    if ( prev )
    {
        DEBUG_PRINT( "Released worker thread %d\n", ThreadItem->idx );
    }
}



static void
set_response_to_internal_error( IN  mt_request_generic_t * Request,
                                OUT mt_response_generic_t * Response )
{
    MYASSERT( NULL != Request );
    MYASSERT( NULL != Response );

    Response->base.sig    = MT_SIGNATURE_RESPONSE;
    Response->base.type   = MT_RESPONSE( Request->base.type );
    Response->base.size   = MT_RESPONSE_BASE_SIZE;
    Response->base.id     = Request->base.id;
    Response->base.sockfd = Request->base.sockfd;

    Response->base.status = MT_STATUS_INTERNAL_ERROR;
}
                                

/**
 * @brief Write a response for an internal error encountered by the dispatch
 * thread.
 *
 * This runs in the context of the dispatch thread because one path to it is where a
 * worker thread couldn't be found.
 */
static int
send_dispatch_error_response( mt_request_generic_t * Request )
{
    mt_response_generic_t response = {0};
    int rc = 0;
    
    set_response_to_internal_error( Request, &response );
    
    ssize_t written = write( g_state.output_fd, &response, response.base.size );
    if ( written != response.base.size )
    {
        rc = errno;
        MYASSERT( !"Failed to send response" );
    }

    return rc;
}


/**
 * @brief Computes string describing ports that are in LISTEN
 * state. If there has been a change, then updates that string in
 * XenStore via an IOCTL.
 */
static int
update_listening_ports( void )
{
    int rc = 0;
    char listening_ports[ INS_LISTENING_PORTS_MAX_LEN ];

    if ( !g_state.pending_port_change ) { goto ErrorExit; }
    
    DEBUG_PRINT( "Scanning for change in set of listening ports\n" );
    g_state.pending_port_change = false;
    listening_ports[0] = '\0';

    for ( int i = 0; i < MAX_THREAD_COUNT; i++ )
    {
        thread_item_t * curr = &g_state.worker_threads[i];

        if ( !curr->in_use ) { continue; }

        // Only add the port if it is nonzero
        if ( 0 != curr->bound_port_num )
        {
            char port[5];
            snprintf( port, sizeof(port), "%x ", curr->bound_port_num );
            strncat( listening_ports, port, sizeof(listening_ports) );
        }
    }

    DEBUG_PRINT( "Listening ports: %s\n", listening_ports );
    rc = ioctl( g_state.input_fd,
                INS_PUBLISH_LISTENERS_IOCTL,
                (const char *)listening_ports );
    if ( rc )
    {
        MYASSERT( !"ioctl" );
    }

ErrorExit:
    return rc;
}



/**
 * @brief Perform internal steps required after a buffer item has been
 * processed.
 */
static int
post_process_response( mt_request_generic_t  * Request,
                       mt_response_generic_t * Response,
                       thread_item_t         * Worker )
{
    int rc = 0;

    // Propogate state, including remote closure status
    Response->base.flags |= Request->base.flags;

    if ( NULL != Worker )
    {
        Response->base.flags |= Worker->state_flags;
    }

    // In accept's success case, assign the new socket to an available thread
    if ( MtRequestSocketAccept == Request->base.type
         && Response->base.status >= 0 )
    {
        thread_item_t * accept_thread = NULL;
        DEBUG_PRINT( "accept() succeeded - allocating thread for the socket\n" );
        rc = get_worker_thread_for_fd( MT_INVALID_SOCKET_FD,  &accept_thread );
        if ( rc )
        {
            // Destroy the new socket and fail the request
            close( Response->base.status );
            Response->base.status = MT_STATUS_INTERNAL_ERROR;
        }
        else
        {
            // A thread was available - record the assignment now
            accept_thread->public_fd =
                MW_SOCKET_CREATE( g_state.client_id, accept_thread->idx );
            accept_thread->local_fd = Response->base.status;

            // Mask the native FD with the exported one
            Response->base.status = accept_thread->public_fd;
            Response->base.sockfd = accept_thread->public_fd;
        }
    }

    rc = update_listening_ports();

    return rc;
}


/**
 * @brief Process one request and issue one response.
 *
 * Exactly one response *MUST* be issued for each request. The
 * response needn't follow its own request immediately; however a
 * socket's response count must match its request count once its
 * close() has completed.
 */
static int
process_buffer_item( buffer_item_t * BufferItem )
{
    int rc = 0;
    mt_response_generic_t  response;
    mt_request_generic_t * request = (mt_request_generic_t *) BufferItem->region;
    thread_item_t * worker = BufferItem->assigned_thread;

    mt_request_type_t reqtype = MT_REQUEST_GET_TYPE( request );

    bzero( &response.base, sizeof(response.base) );
    
    DEBUG_PRINT( "Processing buffer item %d (request ID %lx)\n",
                 BufferItem->idx, (unsigned long)request->base.id );
    MYASSERT( MT_IS_REQUEST( request ) );

    switch( request->base.type )
    {
    case MtRequestSocketCreate:
        rc = xe_net_create_socket( &request->socket_create,
                                   &response.socket_create,
                                   worker );
        break;
    case MtRequestSocketShutdown:
        rc = xe_net_shutdown_socket( &request->socket_shutdown,
                                     &response.socket_shutdown,
                                     worker );
        break;
    case MtRequestSocketClose:
        rc = xe_net_close_socket( &request->socket_close,
                                  &response.socket_close,
                                  worker );
        break;
    case MtRequestSocketConnect:
        rc = xe_net_connect_socket( &request->socket_connect,
                                    &response.socket_connect,
                                    worker );
        break;
    case MtRequestSocketSend:
        rc = xe_net_send_socket( &request->socket_send,
                                 &response.socket_send,
                                 worker );
        break;
    case MtRequestSocketBind: 
        rc = xe_net_bind_socket( &request->socket_bind,
                                 &response.socket_bind,
                                 worker );
        break;
    case MtRequestSocketListen:
        rc = xe_net_listen_socket( &request->socket_listen,
                                   &response.socket_listen,
                                   worker );
        break;
    case MtRequestSocketAccept:
        rc = xe_net_accept_socket( &request->socket_accept,
                                   &response.socket_accept,
                                   worker );
        break;
    case MtRequestSocketRecv:
        rc = xe_net_recv_socket( &request->socket_recv,
                                 &response.socket_recv,
                                 worker );
        break;
    case MtRequestSocketRecvFrom:
        rc = xe_net_recvfrom_socket( &request->socket_recv,
                                     &response.socket_recvfrom,
                                     worker );
        break;
    case MtRequestSocketGetName:
    case MtRequestSocketGetPeer:
        rc = xe_net_get_name( &request->socket_getname,
                              &response.socket_getname,
                              worker );
        break;
    case MtRequestSocketAttrib:
        rc = xe_net_sock_attrib( &request->socket_attrib,
                                 &response.socket_attrib,
                                 worker );
        break;
    case MtRequestPollsetQuery:
        rc = xe_pollset_query( &request->pollset_query,
                               &response.pollset_query );
        break;

    case MtRequestInvalid:
    default:
        MYASSERT( !"Invalid request type" );
        // Send back an internal error
        set_response_to_internal_error( request, &response );
        response.base.type = MtResponseInvalid;
        break;
    }

    if ( worker && worker->oplock_acquired )
    {
        worker->oplock_acquired = false;
        sem_post( &worker->oplock );
    }

    // No matter the results of the network operation, send the
    // response back over the device.

    MYASSERT( 0 == rc );
    MYASSERT( MT_IS_RESPONSE( &response ) );

    // How to handle failure?
    (void) post_process_response( request, &response, worker );

    DEBUG_PRINT( "Writing response ID %lx len %hx to ring\n",
                 response.base.id, response.base.size );

    size_t written = write( g_state.output_fd,
                            &response,
                            response.base.size );

    if ( written != response.base.size )
    {
        rc = errno;
        MYASSERT( !"write" );
    }

    // We're done with the buffer item
    release_buffer_item( BufferItem );

    // Release the worker thread if: (1) we are releasing an FD, or
    // (2) we were allocating an FD and that failed (accept is handled
    // above).
    if ( MT_DEALLOCATES_FD( reqtype )
         || (MtRequestSocketCreate == reqtype && response.base.status < 0 ) )
    {
        DEBUG_PRINT( "Releasing thread due to request type or response: "
                     "ID %lx type %x status %d\n",
                     response.base.id, reqtype, response.base.status );
        release_worker_thread( worker );
    }

    DEBUG_PRINT( "Done with response %lx\n", response.base.id );
    //debug_print_state();

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
 * (3) an error response has been written.
 */
static int
assign_work_to_thread( IN buffer_item_t   * BufferItem,
                       OUT thread_item_t ** AssignedThread,
                       OUT bool           * ProcessFurther )
{
    int rc = 0;
    mt_request_generic_t * request = (mt_request_generic_t *) BufferItem->region;
    mt_request_type_t request_type = MT_REQUEST_GET_TYPE( request );
    bool process_now = false;
    
    // Typically the caller needs to do more work on this buffer. When
    // this is false we release the buffer item.
    *ProcessFurther = true;
    
    DEBUG_PRINT( "Looking for thread for request in buffer item %d\n",
                 BufferItem->idx );


    // Any failure here is an "internal" failure. In such a case, we
    // must make sure that we issue a response to the request, since
    // it won't be processed further.

    switch( request_type )
    {
        // Processed in current thread without a worker
    case MtRequestPollsetQuery:
        MYASSERT( MW_SOCKET_IS_FD( request->base.sockfd ) );
        *AssignedThread = NULL;
        process_now = true;
        break;

        // Processed in current thread with existing worker
    case MtRequestSocketShutdown:
    case MtRequestSocketClose:
    case MtRequestSocketBind:
    case MtRequestSocketListen:
    case MtRequestSocketAttrib:
        MYASSERT( MT_INVALID_SOCKET_FD != request->base.sockfd );
        process_now = true;
        rc = get_worker_thread_for_fd( request->base.sockfd, AssignedThread );
        if ( rc ) goto ErrorExit;
        break;

        // Processed in current thread, acquire a new worker
    case MtRequestSocketCreate:
        MYASSERT( MT_INVALID_SOCKET_FD == request->base.sockfd );
        process_now = true;
        rc = get_worker_thread_for_fd( MT_INVALID_SOCKET_FD, AssignedThread );
        if ( rc ) goto ErrorExit;
        break;

        // Processed in worker thread (already assigned)
    case MtRequestSocketConnect:
    case MtRequestSocketSend:
    case MtRequestSocketAccept:
    case MtRequestSocketRecv:
    case MtRequestSocketRecvFrom:
    case MtRequestSocketGetName:
    case MtRequestSocketGetPeer:
        MYASSERT( MT_INVALID_SOCKET_FD != request->base.sockfd );
        process_now = false;
        rc = get_worker_thread_for_fd( request->base.sockfd, AssignedThread );
        if ( rc ) goto ErrorExit;
        break;

    case MtRequestInvalid:
    default:
        MYASSERT( !"Invalid request type" );
        rc = EINVAL;
        // Send back an internal error
        goto ErrorExit;
        break;
    }

    BufferItem->assigned_thread = *AssignedThread;

    // Acquire oplock in main thread now. It is released by
    // process_buffer_item().
    if ( REQUEST_REQUIRES_OPLOCK( request ) )
    {
        MYASSERT( *AssignedThread );
        sem_wait( &(*AssignedThread)->oplock );
        (*AssignedThread)->oplock_acquired = true;
    }

    if ( process_now )
    {
        *ProcessFurther = false;
        rc = process_buffer_item( BufferItem );
    }
    else
    {
        *ProcessFurther = true;
        rc = workqueue_enqueue( (*AssignedThread)->work_queue, BufferItem->idx );
        if ( rc )
        {
            if ( (*AssignedThread)->oplock_acquired )
            {
                (*AssignedThread)->oplock_acquired = false;
                sem_post( &(*AssignedThread)->oplock );
            }
        }
    }
    
    if ( *ProcessFurther )
    {
        DEBUG_PRINT( "Work item %d assigned to thread %d\n",
                     BufferItem->idx, (*AssignedThread)->idx );
    }

ErrorExit:

    // Something here failed. Report the error. The worker thread will
    // not release the buffer item, so do it here.
    if ( rc )
    {
        *ProcessFurther = false;
        DEBUG_PRINT( "An internal failure occured. Sending error response.\n" );
        (void) send_dispatch_error_response( request );
    }

    if ( ! *ProcessFurther )
    {
        // may have already been released in process_buffer_item()
        release_buffer_item( BufferItem );
    }

    return rc;
}

/**
 * @brief The top-level function that each worker thread executes.
 *
 * Here's the basic algorithm:
 *
 * forever:
 * - wait for one work item to arrive
 * - get the work item from the workqueue
 * - process the work: if it's a shutdown command, then break out of loop
 */
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

void *
heartbeat_thread_func( void* Args )
{
    while ( g_state.continue_heartbeat )
    {
        char stats[ INS_NETWORK_STATS_MAX_LEN ] = {0};

        (void) snprintf( stats, sizeof(stats), "%lx:%llx:%llx",
                         (unsigned long) g_state.network_stats_socket_ct,
                         (unsigned long long) g_state.network_stats_bytes_recv,
                         (unsigned long long) g_state.network_stats_bytes_sent );

        ioctl( g_state.input_fd, INS_HEARTBEAT_IOCTL, (const char *)stats );
        sleep( HEARTBEAT_INTERVAL_SEC );
    }

    pthread_exit( Args );
}


static int
init_state( void )
{
    int rc = 0;
    
    bzero( &g_state, sizeof(g_state) );

    g_state.continue_heartbeat = true;

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
        sem_init( &curr->oplock, 0, 1 );
        curr->oplock_acquired = false;
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

    rc = xe_net_init();
    if ( 0 != rc )
    {
        goto ErrorExit;
    }

    rc = ioctl( g_state.input_fd, INS_DOMID_IOCTL, &g_state.client_id );
    if ( 0 != rc )
    {
       MYASSERT( !"Getting dom id from ioctl failed");
       goto ErrorExit;
    }

    DEBUG_PRINT( "INS got domid: %u from ioctl\n", g_state.client_id );
    
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

    //
    // Start up heartbeat thread
    //
    rc = pthread_create( &g_state.heartbeat_thread,
                         NULL,
                         heartbeat_thread_func,
                         NULL );
    if ( rc )
    {
        MYASSERT( !"Heartbeat thread" );
        goto ErrorExit;
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
        workqueue_free( curr->work_queue );
        sem_destroy( &curr->awaiting_work_sem );
    }

    g_state.continue_heartbeat = false;
    pthread_join( g_state.heartbeat_thread, NULL );

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


/**
 * @brief Reads requests from the Xen ring buffer and dispatches them
 * to their respective threads.
 *
 * Responses are read from the ring buffer via the xenevent
 * device. The threads each write their responses to the ring
 * buffer. Synchronization to the ring buffer is handled by the
 * driver.
 */
static int
message_dispatcher( void )
{
    int rc = 0;
    ssize_t size = 0;
    bool more_processing = false;

    // Forever, read commands from device and dispatch them, allowing
    // the other threads to execute.
    while( true )
    {
        thread_item_t * assigned_thread = NULL;
        buffer_item_t * myitem = NULL;
        mt_request_type_t request_type;

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
        if ( size < (ssize_t) sizeof(mt_request_base_t)
             || myitem->request->base.size > ONE_REQUEST_REGION_SIZE )
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

        DEBUG_PRINT( "Read request %lx type %x size %x off ring\n",
                     (unsigned long) myitem->request->base.id,
                     myitem->request->base.type,
                     myitem->request->base.size );

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
        request_type = MT_REQUEST_GET_TYPE( myitem->request );
        if ( request_type == MtRequestSocketConnect
             || request_type == MtRequestSocketAccept)
        {
            xe_yield();
        }
        else 
        {
            sched_yield();
        }
    } // while
    
ErrorExit:
    xe_yield();
    return rc;

} // message_dispatcher


int main(void)
{
    int rc = 0;

      
    rc = init_state();
    if ( rc )
    {
        goto ErrorExit;
    }

    // main thread dispatches commands to the other threads
    rc = message_dispatcher();

ErrorExit:
    fini_state();
    return rc;
}
