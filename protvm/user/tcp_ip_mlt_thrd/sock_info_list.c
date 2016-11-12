/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

/**
 * @file    sock_info_list.c
 * @author  Mark Mason 
 * @date    4 November 2016
 * @version 0.1
 * @brief   Concrete Implementation. Sock Info List.
 */

#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>

#include "sock_info_list.h"
#include "list.h"

static void* 
create_sock_info( void );

static void 
copy_sock_info( void *src_sock_info,
                void *dst_sock_info );

static bool 
compare_sock_infos( void *sock_info_1,
	            void *sock_info_2 );

static void 
remove_sock_info( void *sock_info ); 

static struct list_manager sock_info_manager = {

    .create  = create_sock_info,
    .copy    = copy_sock_info,
    .compare = compare_sock_infos, 
    .remove  = remove_sock_info 
};

static void*
create_sock_info( void )
{
    sinfo_t *sock_info;

    sock_info = calloc(1, sizeof(sinfo_t));

    if ( !sock_info )
        return NULL;

    return sock_info;
}

static void
copy_sock_info( void *src_sock_info,
	        void *dst_sock_info )
{
    sinfo_t *src;
    sinfo_t *dst;

    assert( src_sock_info != NULL );
    assert( dst_sock_info != NULL );

    if ( src_sock_info == dst_sock_info )
        return;

    src = (sinfo_t *)src_sock_info;
    dst = (sinfo_t *)dst_sock_info;

    *dst = *src;
}

static bool 
compare_sock_infos( void *sock_info_1,
	            void *sock_info_2 )
{
    sinfo_t *s_1;
    sinfo_t *s_2;

    assert(sock_info_1 != NULL);
    assert(sock_info_2 != NULL);

    if (sock_info_1 == sock_info_2)
        return true;

    s_1 = (sinfo_t *)sock_info_1;
    s_2 = (sinfo_t *)sock_info_2;

    return (s_1->sockfd == s_2->sockfd);
}

static void
remove_sock_info( void *sock_info )
{
    if ( sock_info == NULL)
        return;

    free(sock_info);
}

struct list *
create_sock_info_list( void )
{
    struct list *list;

    list = create_list();
    if (list == NULL)
	return NULL; 

    set_list_manager(list, &sock_info_manager);

    return list;
}

int
add_sock_info( struct list *list,
	       sinfo_t     *sock_info )
{
    int error;

    error = add_list_member(list, sock_info);
    if (error)
        return error;

    return 0;
}

void *
find_sock_info( struct list *list,
	        int          sockfd )
{
    sinfo_t   target_sock;
    void     *search_sock_found;
    sinfo_t  *sock_found;

    if ( list == NULL )
        return NULL;

    target_sock.sockfd   = sockfd;

    search_sock_found = find_list_member(list, &target_sock);
    if ( search_sock_found == NULL )
        return NULL;

    sock_found = (sinfo_t *)search_sock_found;

    return sock_found;
}

void
get_next_sock_info( list_iterator *iterator,
	            sinfo_t       *sock_info )
{
    void     *next_member;
    sinfo_t  *next_sock_info;

    assert(iterator != NULL);
    assert(sock_info != NULL);

    get_next_list_member(iterator, &next_member);

    next_sock_info = next_member;

   *sock_info = *next_sock_info;
}

void
destroy_sock_info( struct list *list,
	           int          sockfd )
{
    sinfo_t s;

    s.sockfd   = sockfd;

    remove_list_member(list, &s);
}

