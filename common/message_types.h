#ifndef message_types_h
#define message_types_h

/***************************************************************************
 * Defines the message types that are passed between the Protected VM
 * and the unikernel via Xen shared memory.
 *
 * The user of this include file must have defined primitive types
 * like uint8_t, uint32_t, etc prior to including this file.
 ***************************************************************************/

#include "mwsocket.h"

// XXXX do this smartly
// Set it small for testing - then we can deal with more messages
#define MESSAGE_TYPE_MAX_PAYLOAD_LEN 64

// Maximum length for a host name
#define MESSAGE_TYPE_MAX_HOSTNAME_BYTE_LEN MESSAGE_TYPE_MAX_PAYLOAD_LEN

// Structures are shared across VMs in code built by different gccs.
// Make sure they agree on the layout.
#define MT_STRUCT_ATTRIBS __attribute__ ((__packed__))

typedef uint16_t mt_size_t;


//
// Request and response types come in pairs. The response types have
// the bits in MT_RESPONSE_MASK set, e.g
//   MT_SOCKET_CREATE_REQUEST_TYPE  0x0001
//   MT_SOCKET_CREATE_RESPONSE_TYPE 0x7001
//

#define MT_RESPONSE_MASK 0x7000

#define MT_REQUEST(x)     (x)
#define MT_RESPONSE(x)    (MT_RESPONSE_MASK | (x))

#define MT_REQUEST_NAME( __name ) __name##_request
#define MT_RESPONSE_NAME( __name ) __name##_response

//
// Each message has a message ID. It is assigned and used by the
// protected VM for matching requests and responses. The unikernel
// copies a request's message ID into the corresponding response.
//
typedef uint64_t mt_id_t;

#define MT_ID_UNSET_VALUE (mt_id_t)-3



//
// Possible message types - Request and Response values must run in parallel
//

typedef enum
{
    MtRequestInvalid        = MT_REQUEST( 0 ),
    MtRequestSocketCreate   = MT_REQUEST( 1 ),
    MtRequestSocketConnect  = MT_REQUEST( 2 ),
    MtRequestSocketClose    = MT_REQUEST( 3 ),
    MtRequestSocketRead     = MT_REQUEST( 4 ),
    MtRequestSocketSend     = MT_REQUEST( 5 ),
    MtRequestSocketBind     = MT_REQUEST( 6 ),
    MtRequestSocketListen   = MT_REQUEST( 7 ),
    MtRequestSocketAccept   = MT_REQUEST( 8 ),
    MtRequestSocketRecv     = MT_REQUEST( 9 ),
    MtRequestSocketRecvFrom = MT_REQUEST( 10 ),
} mt_request_type_t;


typedef enum
{
    MtResponseInvalid           = MT_RESPONSE( MtRequestInvalid        ),
    MtResponseSocketCreate      = MT_RESPONSE( MtRequestSocketCreate   ),
    MtResponseSocketConnect     = MT_RESPONSE( MtRequestSocketConnect  ),
    MtResponseSocketClose       = MT_RESPONSE( MtRequestSocketClose    ),
    MtResponseSocketRead        = MT_RESPONSE( MtRequestSocketRead     ),
    MtResponseSocketSend        = MT_RESPONSE( MtRequestSocketSend     ),
    MtResponseSocketBind        = MT_RESPONSE( MtRequestSocketBind     ),
    MtResponseSocketListen      = MT_RESPONSE( MtRequestSocketListen   ),
    MtResponseSocketAccept      = MT_RESPONSE( MtRequestSocketAccept   ),
    MtResponseSocketRecv        = MT_RESPONSE( MtRequestSocketRecv     ),
    MtResponseSocketRecvFrom    = MT_RESPONSE( MtRequestSocketRecvFrom ),
} mt_response_id_t;

typedef uint32_t mt_addrlen_t;

#define MT_INVALID_SOCKET_FD (mw_socket_fd_t)-1

typedef uint16_t mt_port_t;

// maps to -errno; non-negative value typically means success; must be signed
typedef int64_t mt_status_t;

#define CRITICAL_ERROR(x) (0xc0000000 | (x))


typedef uint16_t mt_sig_t;
#define MT_SIGNATURE_REQUEST  0xff11
#define MT_SIGNATURE_RESPONSE 0xff33


#define MT_STATUS_INTERNAL_ERROR CRITICAL_ERROR(1)

//
// Linux and Rump do not agree on constants used by the socket() and
// connect() calls. We use these between the two.
//

// "domain" or "protocol family"
typedef enum
{
    MT_PF_UNSET = 0,
    MT_PF_INET  = 1,
    MT_PF_INET6 = 2,
} mt_protocol_family_t;

// stream type
typedef enum
{
    MT_ST_UNSET = 0,
    MT_ST_DGRAM = 1,
    MT_ST_STREAM = 2,
} mt_sock_type_t;

//
//Struct for generic socket type
//
#define MT_SA_DATA_LEN 14

typedef struct _mt_sockaddr
{
    uint16_t sa_family;
    uint8_t sa_data[MT_SA_DATA_LEN];
} mt_sockaddr_t;


//
// IPv4 AF_INET sockets:
//
typedef struct _mt_in_addr
{
    uint64_t    s_addr;

} mt_in_addr_t;


typedef struct _mt_sockaddr_in
{
    mt_protocol_family_t  sin_family;
    uint16_t              sin_port;
    mt_in_addr_t          sin_addr;
    uint8_t               sin_zero[8];

} mt_sockaddr_in_t;

//Inet address type
#define MT_INADDR_ANY ((unsigned long int) 0x00000000)


//
// The preamble for every request.
//
typedef struct MT_STRUCT_ATTRIBS _mt_request_base 
{
    // Must be MT_SIGNATURE_REQUEST
    mt_sig_t          sig;
    
    // MtRequest*
    mt_request_type_t   type;
    
    // All-inclusive size of the populated bytes in the payload,
    // including this structure.
    mt_size_t   size;

    // Server-generated request ID
    mt_id_t     id;

    // The socket. Used in most requests.
    mw_socket_fd_t sockfd;

} mt_request_base_t;

//
// The preamble for every response
//
typedef struct MT_STRUCT_ATTRIBS _mt_response_base 
{
    // Must be MT_SIGNATURE_RESPONSE
    mt_sig_t          sig;

    // MtResponse*
    mt_response_id_t   type;

    // Size of the payload only - after this header
    mt_size_t   size;

    // Matches id in corresponding request
    mt_id_t     id;

    // The socket. Used in most requests.
    mw_socket_fd_t sockfd;

    // Status returned from the call. 0 or an errno. The number of
    // bytes read/written is tracked elsewhere.
    mt_status_t status;

} mt_response_base_t;


//
// Socket creation
//

typedef struct MT_STRUCT_ATTRIBS _mt_request_socket_create
{
    mt_request_base_t    base;
    mt_protocol_family_t sock_fam; // or socket domain
    mt_sock_type_t       sock_type;
    uint32_t             sock_protocol;
} mt_request_socket_create_t;

typedef struct  MT_STRUCT_ATTRIBS _mt_response_socket_create
{
    mt_response_base_t base;
} mt_response_socket_create_t;

#define MT_REQUEST_SOCKET_CREATE_SIZE  sizeof(mt_request_socket_create_t)
#define MT_RESPONSE_SOCKET_CREATE_SIZE sizeof(mt_response_socket_create_t)


//
//Bind
//
typedef struct MT_STRUCT_ATTRIBS _mt_request_socket_bind
{
    mt_request_base_t base;
    mt_sockaddr_in_t sockaddr;  //Hard coded for now, but should be a union of all sockaddr types.

} mt_request_socket_bind_t;

typedef struct MT_STRUCT_ATTRIBS _mt_response_socket_bind
{
    mt_response_base_t base;

} mt_response_socket_bind_t;

#define MT_REQUEST_SOCKET_BIND_SIZE sizeof(mt_request_socket_bind_t)
#define MT_RESPONSE_SOCKET_BIND_SIZE sizeof(mt_response_socket_bind_t)


//
//Listen
//
typedef struct MT_STRUCT_ATTRIBS _mt_request_socket_listen
{
    mt_request_base_t base;
    int backlog;

} mt_request_socket_listen_t;

typedef struct MT_STRUCT_ATTRIBS _mt_response_socket_listen
{
    mt_response_base_t base;

} mt_response_socket_listen_t;

#define MT_REQUEST_SOCKET_LISTEN_SIZE sizeof(mt_request_socket_listen_t)
#define MT_RESPONSE_SOCKET_LISTEN_SIZE sizeof(mt_response_socket_listen_t)


//
// Accept
//

typedef struct MT_STRUCT_ATTRIBS _mt_request_socket_accept
{
    mt_request_base_t base;

} mt_request_socket_accept_t;

typedef struct MT_STRUCT_ATTRIBS _mt_response_socket_accept
{
    mt_response_base_t base;
    mt_sockaddr_in_t sockaddr;

} mt_response_socket_accept_t;

#define MT_REQUEST_SOCKET_ACCEPT_SIZE sizeof(mt_request_socket_accept_t)
#define MT_RESPONSE_SOCKET_ACCEPT_SIZE sizeof(mt_response_socket_accept_t)



//
// Recv
//

typedef struct MT_STRUCT_ATTRIBS _mt_request_socket_recv
{
    mt_request_base_t base;
    uint64_t          flags;
    mt_size_t         requested;
} mt_request_socket_recv_t;

#define MT_REQUEST_SOCKET_RECV_SIZE ( sizeof(mt_request_socket_recv_t ) )

typedef struct MT_STRUCT_ATTRIBS _mt_response_socket_recv
{
    mt_response_base_t base;
    uint8_t            bytes[MESSAGE_TYPE_MAX_PAYLOAD_LEN];
} mt_response_socket_recv_t;
#define MT_RESPONSE_SOCKET_RECV_SIZE ( sizeof( mt_response_base_t ) )

typedef struct MT_STRUCT_ATTRIBS _mt_response_socket_recvfrom
{
    mt_response_base_t      base;
    mt_sockaddr_in_t        src_addr;
    uint32_t                addrlen;
    uint8_t                 bytes[MESSAGE_TYPE_MAX_PAYLOAD_LEN];
} mt_response_socket_recvfrom_t;

#define MT_RESPONSE_SOCKET_RECVFROM_SIZE                        \
    ( sizeof( mt_response_base_t )                              \
      + sizeof( mt_sockaddr_in_t ) + sizeof( uint32_t ) )


//
// Connect
//

typedef struct MT_STRUCT_ATTRIBS _mt_request_socket_connect
{
    mt_request_base_t base;
    mt_sockaddr_in_t sockaddr;

} mt_request_socket_connect_t;

typedef struct MT_STRUCT_ATTRIBS _mt_response_socket_connect
{
    mt_response_base_t base;

    // nothing else
} mt_response_socket_connect_t;

#define MT_REQUEST_SOCKET_CONNECT_SIZE sizeof( mt_request_socket_connect_t )
#define MT_RESPONSE_SOCKET_CONNECT_SIZE sizeof( mt_response_socket_connect_t )


//
// Close
//

typedef struct MT_STRUCT_ATTRIBS _mt_request_socket_close
{
    mt_request_base_t base;
} mt_request_socket_close_t;

typedef struct MT_STRUCT_ATTRIBS _mt_response_socket_close
{
    mt_response_base_t base;

    // nothing else
} mt_response_socket_close_t;

#define MT_REQUEST_SOCKET_CLOSE_SIZE  sizeof(mt_request_socket_close_t)
#define MT_RESPONSE_SOCKET_CLOSE_SIZE sizeof(mt_response_socket_close_t)


//
// Write
//
typedef struct MT_STRUCT_ATTRIBS _mt_request_socket_send
{
    mt_request_base_t  base;
    uint64_t           flags;
    uint8_t            bytes[ MESSAGE_TYPE_MAX_PAYLOAD_LEN ];
} mt_request_socket_send_t;

typedef struct MT_STRUCT_ATTRIBS _mt_response_socket_send
{
    mt_response_base_t base;
    mt_size_t          sent;
} mt_response_socket_send_t;

// User must add count of filled bytes to size
#define MT_REQUEST_SOCKET_SEND_SIZE  sizeof(mt_request_base_t) + sizeof(uint64_t)
#define MT_RESPONSE_SOCKET_SEND_SIZE sizeof(mt_response_socket_send_t)


//
// All inclusive types to be used before the message type is known
//

typedef union _mt_request_generic
{
    mt_request_base_t           base;
    mt_request_socket_create_t  socket_create;
    mt_request_socket_connect_t socket_connect;
    mt_request_socket_close_t   socket_close;
    mt_request_socket_send_t    socket_send;
    mt_request_socket_bind_t    socket_bind;
    mt_request_socket_listen_t  socket_listen;
    mt_request_socket_accept_t  socket_accept;
    mt_request_socket_recv_t    socket_recv;
} mt_request_generic_t;

#define MT_REQUEST_BASE_GET_TYPE(rqb) ((rqb)->type)
#define MT_REQUEST_GET_TYPE(rq) ((rq)->base.type)
#define MT_IS_REQUEST(x)                                                \
    ((MT_SIGNATURE_REQUEST == (x)->base.sig) &&                         \
     (0 == (MT_RESPONSE_MASK & (x)->base.type)))


typedef union _mt_response_generic
{
    mt_response_base_t           base;
    mt_response_socket_create_t     socket_create;
    mt_response_socket_connect_t    socket_connect;
    mt_response_socket_close_t      socket_close;
//    mt_response_socket_read_t       socket_read;
    mt_response_socket_send_t       socket_send;
    mt_response_socket_bind_t       socket_bind;
    mt_response_socket_listen_t     socket_listen;
    mt_response_socket_accept_t     socket_accept;
    mt_response_socket_recv_t       socket_recv;
    mt_response_socket_recvfrom_t   socket_recvfrom;
} mt_response_generic_t;

#define MT_RESPONSE_BASE_GET_TYPE(rqb) ((rqb)->type)
#define MT_RESPONSE_GET_TYPE(rq) ((rq)->base.type)

#define MT_IS_RESPONSE(x)                                               \
    ((MT_SIGNATURE_RESPONSE == (x)->base.sig) &&                        \
     (MT_RESPONSE_MASK == (MT_RESPONSE_MASK & (x)->base.type)))


#endif // message_types_h
