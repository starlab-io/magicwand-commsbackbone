/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

#include <stdio.h> 
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <poll.h>
#include <fcntl.h>

#include "pollset.h"
#include "networking.h"

#include <errno.h>

extern xenevent_globals_t g_state;

/*
// Is this needed? Will poll() work with threading on Rump?
static void *
pollset_thread_func( void * Arg )
{
    int rc = 0;

    // Is there a pending query?
}
*/


/*
void
xe_pollset_handle_close( IN thread_item_t * ThreadItem )
{
    MYASSERT( ThreadItem );

    if ( !ThreadItem->blocking ) // if non-blocking...
    {
        // Set to blocking to purge from pollset subsystem
        ThreadItem->blocking = true;
    }
}
*/


/*
// @brief Add or remove a socket to/from the pollset.
//
// @return 0 on success, or errno. The caller must handle failure by
// sending error response.
int
xe_pollset_mod( mt_request_pollset_mod_t *  Request,
                mt_response_pollset_mod_t * Response )
{
    thread_item_t * thread_item = NULL;
    mw_socket_fd_t         mwfd = Request->base.sockfd;

    int    rc = 0;
    int flags = 0;

    if ( !MW_SOCKET_IS_FD( mwfd ) )
    {
        DEBUG_PRINT( "Invalid FD %x given\n", mwfd );
        rc = -EINVAL;
        goto ErrorExit;
    }
    
    // Find the local FD from the mwsocket given in the request
    rc = get_worker_thread_for_fd( mwfd, &thread_item );
    if ( rc ) goto ErrorExit;

    // Update local FD's flags
    flags = fcntl( thread_item->public_fd, F_GETFL );
    rc = Request->blocking ?
        fcntl( thread_item->public_fd, flags | O_NONBLOCK ) :
        fcntl( thread_item->public_fd, flags & ~O_NONBLOCK );

    if ( rc )
    {
        rc = XE_GET_NEG_ERRNO();
        MYASSERT( !"fcntl" );
        goto ErrorExit;
    }

    // Mark the thread info struct with new status and for pending update
    if ( thread_item->blocking != Request->blocking )
    {
        thread_item->blocking = Request->blocking;
        thread_item->pollset_data = (void *) true; // data pending
    }

    xe_net_set_base_response( (mt_request_generic_t *)  Request,
                              MT_RESPONSE_POLLSET_MOD_SIZE,
                              (mt_response_generic_t *) Response );
    Response->base.status = 0;

ErrorExit:
    return rc;
}
*/

// @brief Perform non-blocking poll against one socket
int
xe_pollset_query_one( IN  int   Fd,
                      OUT int * Events )
{
    int     rc = 0;
    struct pollfd fds; // just one

    fds.fd = Fd;
    fds.events = POLLIN | MW_POLLRDNORM | POLLOUT | MW_POLLWRNORM;
    fds.revents = 0;

    // Non-blocking poll against 1 FD
    rc = poll( &fds, 1, 0 );
    if ( rc < 0 )
    {
        rc = XE_GET_NEG_ERRNO();
        MYASSERT( !"poll" );
        goto ErrorExit;
    }

    // Success: now check whether any events were reported
    if ( 0 == rc )
    {
        // no events
        goto ErrorExit;
    }

    *Events = fds.revents;
    rc = 0;

ErrorExit:
    return rc;
}


// @brief Perform a non-blocking poll against all the open sockets.
int
xe_pollset_query( mt_request_pollset_query_t  * Request,
                  mt_response_pollset_query_t * Response )
{
    int               rc = 0;
    int           pollct = 0;

    // Some of this data can stay the same across calls. Only the main
    // thread will be calling this function.
    static bool     init = false;
    static struct pollfd fds[ MAX_THREAD_COUNT ];

    // Set the input flags, which don't change across calls
    if ( !init )
    {
        init = true;
        for ( int i = 0; i < MAX_THREAD_COUNT; i++ )
        {
            // see socket(7); N.B. POLLHUP is output flag only
            fds[ i ].events =
                POLLIN | MW_POLLRDNORM | POLLOUT | MW_POLLWRNORM;
        }
    }

    // Set the FDs, which do change
    for ( int i = 0; i < MAX_THREAD_COUNT; ++i )
    {
        thread_item_t * thisti = &g_state.worker_threads[i];
        if ( !thisti->in_use ) ///XXXXXXXX || thisti->blocking )
        {
            fds[ i ].fd = -1;
            continue;
        }
        VERBOSE_PRINT( "Including FD %lx/%d in poll()\n",
                     thisti->public_fd, thisti->local_fd );
        fds[ i ].fd =  thisti->local_fd;
    }

    // Make the call, do not block
    pollct = poll( fds, (nfds_t)MAX_THREAD_COUNT, 0 );
    if ( pollct < 0 )
    {
        rc = XE_GET_NEG_ERRNO();
        MYASSERT( !"poll" );
        goto ErrorExit;
    }

    for ( int i = 0, itemidx = 0;
          i < MAX_THREAD_COUNT && itemidx < pollct;
          ++i )
    {
        struct pollfd * thisfd = &fds[i];
        mt_response_pollset_query_item_t * thisqi = &Response->items[ itemidx ];
        thread_item_t * thisti = &g_state.worker_threads[i];

        thisti->poll_events = 0;

        if ( !fds[ i ].revents )
        {
            // nothing to report
            continue;
        }

        // Something to report. Populate the query item.
        thisqi->sockfd = thisti->public_fd;
        thisqi->events = 0;

        if ( thisfd->revents & POLLIN  )    thisqi->events |= MW_POLLIN;
        if ( thisfd->revents & POLLRDNORM ) thisqi->events |= MW_POLLRDNORM;
        if ( thisfd->revents & POLLHUP )    thisqi->events |= MW_POLLHUP;
        if ( thisfd->revents & POLLOUT )    thisqi->events |= MW_POLLOUT;
        if ( thisfd->revents & POLLWRNORM ) thisqi->events |= MW_POLLWRNORM;
        if ( thisfd->revents & POLLERR )    thisqi->events |= MW_POLLERR;
        if ( thisfd->revents & POLLPRI )    thisqi->events |= MW_POLLPRI;
        if ( thisfd->revents & POLLNVAL )   thisqi->events |= MW_POLLNVAL;

        thisti->poll_events = thisqi->events;

        DEBUG_PRINT( "Found IO events %x => %x on socket %lx/%d\n",
                     thisfd->revents, thisqi->events,
                     thisti->public_fd, thisti->local_fd );
        ++itemidx;
    }

    Response->count = pollct;

    xe_net_set_base_response( (mt_request_generic_t *) Request,
                              MT_RESPONSE_POLLSET_QUERY_SIZE +
                              MT_RESPONSE_POLLSET_QUERY_ITEM_SIZE * pollct,
                              (mt_response_generic_t *) Response );
ErrorExit:
    return rc;
}


int
xe_pollset_init( void )
{
    int rc = 0;
//ErrorExit:
    return rc;
}


int
xe_pollset_fini( void )
{
    int rc = 0;
//ErrorExit:
    return rc;
}
