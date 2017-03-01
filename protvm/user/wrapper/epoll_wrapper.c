#include "epoll_wrapper.h"

#include <message_types.h>
#include <translate.h>
#include <user_common.h>
#include <stdlib.h>
#include <errno.h>

static struct list_head mw_epoll_head;

#if 0
static int
get_next_epoll_id( void )
{
    static int id = 0;
    return __sync_add_and_fetch( &id, 1 );
}
#endif

epoll_request_t *
mw_epoll_create( void )
{
    epoll_request_t * req = NULL;

    req = malloc( sizeof(*req) );
    if ( NULL == req )
    {
        MYASSERT( !"malloc" );
        goto ErrorExit;
    }

    bzero( req, sizeof(*req) );

    req->pseudofd = MT_INVALID_FD;

    for ( int i = 0; i < MAX_POLL_FD_COUNT; ++i )
    {
        req->fds[ i ] = MT_INVALID_SOCKET_FD;
    }
    
    list_add( &req->list, &mw_epoll_head );

ErrorExit:
    return req;
}

void 
mw_epoll_destroy( epoll_request_t * MwEp )
{
    DEBUG_PRINT( "Destroying epoll FD %x\n", MwEp->pseudofd );
    list_del( &MwEp->list );
    free( MwEp );
}


epoll_request_t *
mw_epoll_find( int EpFd )
{
    epoll_request_t * curr = NULL;
    bool found = false;

    list_for_each_entry( curr, &mw_epoll_head, list )
    {
        if ( EpFd == curr->pseudofd )
        {
            found = true;
            break;
        }
    }

    if ( !found )
    {
        curr = NULL;
    }

    return curr;
}


int
mw_epoll_ctl(int EpFd,
             int Op,
             int Fd,
             struct epoll_event * Event )
{
    int rc = 0;
    epoll_request_t * req = NULL;
    int idx = -1;

    req = mw_epoll_find( EpFd );
    if ( NULL == req )
    {
        rc = -1;
        errno = EINVAL;
        MYASSERT( !"Invalid EpFd given" );
        goto ErrorExit;
    }

    if ( req->fdct > 0 )
    {
        if ( req->is_mw != MW_SOCKET_IS_FD( Fd ) )
        {
            rc = -1;
            errno = EINVAL;
            MYASSERT( !"Can't have both MW and non-MW FDs in epoll set" );
            goto ErrorExit;
        }
    }
    else
    {
        req->is_mw = MW_SOCKET_IS_FD( Fd );
    }

    DEBUG_PRINT( "Modifying epoll FD %x: Op %d Fd %x\n",
                 req->pseudofd, Op, Fd );

    // Find target index
    idx = -1;
    for ( int i = 0; i < MAX_POLL_FD_COUNT; ++i )
    {
        switch( Op )
        {
        case EPOLL_CTL_ADD:
            if ( req->fds[i] == Fd )
            {
                MYASSERT( !"Can't add Fd a second time" );
                rc = -1;
                errno = EEXIST;
                goto ErrorExit;
            }
            if ( req->fds[i] == MT_INVALID_SOCKET_FD )
            {
                idx = i;
                break;
            }
        case EPOLL_CTL_DEL:
        case EPOLL_CTL_MOD:
            if ( req->fds[i] == Fd )
            {
                idx = i;
                break;
            }
        default:
            break;
        } //switch

        if ( idx >= 0 )
        {
            // Found a slot; get out now
            break;
        }
    }

    if ( idx < 0 )
    {
        MYASSERT( !"No suitable slot for Fd found" );
        rc = -1;
        errno = EEXIST;
        goto ErrorExit;
    }

    switch( Op )
    {
    case EPOLL_CTL_ADD:
        req->events[ idx ] = *Event;
        req->fds[ idx ]    = Fd;
        req->fdct          = MAX( idx, req->fdct + 1 );
        break;
    case EPOLL_CTL_MOD:
        req->events[ idx ] = *Event;
        break;
    case EPOLL_CTL_DEL:
        req->fds[ idx ] = MT_INVALID_SOCKET_FD;
        req->events[ idx ].data.ptr = 0;
        break;
    default:
        rc = -1;
        errno = EINVAL;
        break;
    }

ErrorExit:
    DEBUG_PRINT( "Done modifying epoll FD %x: Op %d Fd %x fdct %d\n",
                 req->pseudofd, Op, Fd, req->fdct );

    return rc;
}

int
mw_epoll_init( void )
{
    INIT_LIST_HEAD( &mw_epoll_head );
    return 0;
}
