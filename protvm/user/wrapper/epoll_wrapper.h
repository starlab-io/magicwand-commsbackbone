/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2017, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

#ifndef epoll_wrapper_h
#define epoll_wrapper_h

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <message_types.h>
#include <sys/epoll.h>

#include <mwsocket.h>
#include "user_list.h"

//
// Wrapper for tracking state of epoll object. NOT THREAD SAFE.
//

//
// HACK HACK HACK: These are the NetBSD values for poll()
//
// From src-netbsd/sys/sys/poll.h
//

#define	MW_POLLIN       0x0001
#define	MW_POLLPRI      0x0002
#define	MW_POLLOUT      0x0004
#define	MW_POLLRDNORM   0x0040
#define	MW_POLLWRNORM   MW_POLLOUT
#define	MW_POLLRDBAND   0x0080
#define	MW_POLLWRBAND   0x0100

/*
 * Non-testable events (may not be specified in events field).
 */
#define	MW_POLLERR    0x0008
#define	MW_POLLHUP    0x0010
//#define	MW_POLLNVAL   0x0020 // no epoll() equivalent

#define MW_INVALID_FD (int)-1


// Record data about epoll object as it is created/modified
typedef struct _epoll_request {
    int pseudofd;

    // Max fd slots used by this object
    int fdct;
    bool is_mw;
    int createflags;

    // The FDs registered via epoll_ctl(). Unset items are MT_INVALID_FD.
    int fds[ MAX_POLL_FD_COUNT ];

    // Has events and data fields; we preseve the user-supplied events here
    struct epoll_event events[ MAX_POLL_FD_COUNT ];

    // Other requests
    struct list_head list;
} epoll_request_t;



epoll_request_t *
mw_epoll_create( void );

void 
mw_epoll_destroy( epoll_request_t * MwEp );


epoll_request_t *
mw_epoll_find( int EpFd );


int
mw_epoll_ctl(int EpFd,
             int Op,
             int Fd,
             struct epoll_event * Event );

int
mw_epoll_init( void );


/*
int
mw_epoll_wait( int EpFd,
               struct epoll_event * Events,
               int MaxEvents,
               int Timeout );
*/

#endif // epoll_wrapper_h
