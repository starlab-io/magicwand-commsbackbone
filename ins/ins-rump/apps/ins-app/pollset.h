/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

#ifndef pollset_h
#define pollset_h

#include "user_common.h"
#include "xenevent_app_common.h"
#include "mwerrno.h"

/*
int
xe_pollset_mod( mt_request_pollset_mod_t *  Request,
                mt_response_pollset_mod_t * Response );
*/

int
xe_net_defer_accept_wait( int SockFd );

int
xe_pollset_query_one( IN  int   Fd,
                      OUT int * Events );


int
xe_pollset_query( mt_request_pollset_query_t  * Request,
                  mt_response_pollset_query_t * Response );


void
xe_pollset_handle_close( IN thread_item_t * ThreadItem );


int
xe_pollset_init( void );


int
xe_pollset_fini( void );


#endif // pollset_h
