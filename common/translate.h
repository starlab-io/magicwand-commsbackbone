#ifndef translate_h
#define translate_h

#include <sys/types.h>
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdio.h>

#include "message_types.h"
#include "translate.h"


static inline
mt_protocol_family_t
xe_net_get_mt_protocol_family( sa_family_t Fam )
{
    mt_protocol_family_t mt_fam = -1;    

    switch( Fam )
    {
    case AF_INET:
        mt_fam = MT_PF_INET;
        break;

    case AF_INET6:
        mt_fam = MT_PF_INET6;
        break;

    default:
        perror("Invalid protocol family");
    }

    return mt_fam;
}


static inline
sa_family_t
xe_net_get_native_protocol_family( mt_protocol_family_t Fam )
{
    sa_family_t pfam = -1; 

    switch( Fam )
    {   
    case MT_PF_INET:
        pfam = AF_INET;
        break;
    case MT_PF_INET6:
        pfam = AF_INET6;
        break;
    case MT_PF_UNSET:
    default:
        perror("Invalid protocol family");
    }   

    return pfam;
}

static inline
int 
xe_net_get_native_sock_type( mt_sock_type_t Type )
{
    int stype = -1;

    switch( Type )
    {   
    case MT_ST_DGRAM:
        stype = SOCK_DGRAM;
        break;
    case MT_ST_STREAM:
        stype = SOCK_STREAM;
        break;
    case MT_ST_UNSET:
    default:
        perror("Invalid socket type requested");
    }

    return stype;
}


static inline
void
populate_sockaddr_in( struct sockaddr_in * sockaddr,  
                      mt_sockaddr_in_t *mt_sockaddr)
{
    sockaddr->sin_family        = xe_net_get_native_protocol_family( mt_sockaddr->sin_family );
    sockaddr->sin_port          = mt_sockaddr->sin_port;
    sockaddr->sin_addr.s_addr   = mt_sockaddr->sin_addr.s_addr;
    memcpy( sockaddr->sin_zero, mt_sockaddr->sin_zero, sizeof(sockaddr->sin_zero) );
}


static inline
void
populate_mt_sockaddr_in( mt_sockaddr_in_t * mt_sockaddr,
                         struct sockaddr_in * SockAddr)
{	
    mt_sockaddr->sin_family         = xe_net_get_mt_protocol_family( SockAddr->sin_family );
    mt_sockaddr->sin_port           = SockAddr->sin_port;
    mt_sockaddr->sin_addr.s_addr    = SockAddr->sin_addr.s_addr;
    memcpy( mt_sockaddr->sin_zero, SockAddr->sin_zero, sizeof(mt_sockaddr->sin_zero) );
}

#endif
