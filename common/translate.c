#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>

#include "message_types.h"
#include "translate.h"
void
populate_sockaddr_in( struct sockaddr_in * sockaddr,  
		              mt_sockaddr_in_t *mt_sockaddr)
{
	    sockaddr->sin_family        = mt_sockaddr->sin_family;
		sockaddr->sin_port          = mt_sockaddr->sin_port;
		sockaddr->sin_addr.s_addr   = mt_sockaddr->sin_addr.s_addr;
		memcpy( sockaddr->sin_zero, mt_sockaddr->sin_zero, sizeof(sockaddr->sin_zero) );
}


void
populate_mt_sockaddr_in( mt_sockaddr_in_t * mt_sockaddr,
						 struct sockaddr_in * SockAddr)
{	
	mt_sockaddr->sin_family         = SockAddr->sin_family;
	mt_sockaddr->sin_port           = SockAddr->sin_port;
	mt_sockaddr->sin_addr.s_addr    = SockAddr->sin_addr.s_addr;
	memcpy( mt_sockaddr->sin_zero, SockAddr->sin_zero, sizeof(mt_sockaddr->sin_zero) );
}
