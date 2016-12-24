#ifndef translate_h
#define translate_h

#include <sys/types.h>
#include <netinet/in.h>
#include "message_types.h"

void
populate_sockaddr_in( struct sockaddr_in * sockaddr,
		              mt_sockaddr_in_t *mt_sockaddr); 
	
void
populate_mt_sockaddr_in( mt_sockaddr_in_t * mt_sockaddr,
						 struct sockaddr_in * SockAddr );
#endif
