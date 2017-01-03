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
int
xe_net_get_native_sock_type( mt_sock_type_t Type );

int 
xe_net_get_native_protocol_family( mt_protocol_family_t Fam );

mt_protocol_family_t
xe_net_get_mt_protocol_family( sa_family_t Fam );

#endif
