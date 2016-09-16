#ifndef xenevent_app_networking_h
#define xenevent_app_networking_h

#include <sys/types.h>

#include "app_common.h"

typedef uint64_t connection_id_t;

//
// AF_INET, AF_INET6, or AF_UNSPEC
typedef uint32_t net_family_t;

typedef uint16_t port_t;

typedef int socket_t;


int
xe_net_establish_connection( IN connection_id_t Id,
                             IN net_family_t AfFamily,
                             IN const char * HostName,
                             IN port_t       PortNum,
                             OUT socket_t *  Socket );

int
xe_net_read_data( IN socket_t Socket,
                  IN uint8_t * Buffer,
                  IN size_t    BufferLen );

int
xe_net_write_data( IN socket_t Socket,
                   IN uint8_t * Buffer,
                   IN size_t    BufferLen );

void
xe_net_close_connection( IN socket_t Socket );


#endif // xenevent_app_networking_h
