
#ifndef __SOCK_INFO_LIST_H__
#define __SOCK_INFO_LIST_H__

#include "list.h"
#include "sock_info.h"

struct list * 
create_sock_info_list( void );

int 
add_sock_info( struct list    *list,
               sinfo_t        *sock_info );

void* 
find_sock_info( struct list *list,
                int          sockfd );

void 
get_next_sock_info( list_iterator *iterator,
	            sinfo_t       *sock_info );

void 
destroy_sock_info( struct list *list,
                   int          sockfd);

#endif

