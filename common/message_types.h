#ifndef message_types_h
#define message_types_h

/***************************************************************************
 * Defines the message types that are passed between the Protected VM
 * and the unikernel via Xen shared memory.
 *
 * The user of this include file must have defined primitive types
 * like uint8_t, uint32_t, etc prior to including this file.
 *
 * WARNING                                                 WARNING
 *
 * If you change this file, update (if needed) common_config.h and
 * rebuild everything.
 *
 * WARNING                                                 WARNING
 ***************************************************************************/

#include "mwsocket.h"

#define _common_config_defined
#   include "common_config.h"
#undef  _common_config_defined

//
// Maximum number of file descriptors we will call poll() on
//
#define MAX_POLL_FD_COUNT 16


// Maximum length for a host name
#define MESSAGE_TYPE_MAX_HOSTNAME_BYTE_LEN MESSAGE_TYPE_MAX_PAYLOAD_LEN


// Structures are shared across VMs in code built by different gccs.
// Make sure they agree on the layout.
#define MT_STRUCT_ATTRIBS __attribute__ ((__packed__))

typedef uint16_t mt_size_t;
typedef uint8_t  mt_bool_t;

//
// Request and response types come in pairs. The response types have
// the bits in MT_RESPONSE_MASK set, e.g
//   MT_SOCKET_CREATE_REQUEST_TYPE  0x0001
//   MT_SOCKET_CREATE_RESPONSE_TYPE 0x7001
//

//
// And here are their signatures
//
typedef uint16_t mt_sig_t;
#define MT_SIGNATURE_REQUEST  (mt_sig_t)0xff11
#define MT_SIGNATURE_RESPONSE (mt_sig_t)0xff33


#define MT_RESPONSE_MASK 0x8000
#define MT_TYPE_MASK     0x7fff

#define MT_REQUEST(x)     (x)
#define MT_RESPONSE(x)    (MT_RESPONSE_MASK | (x))

#define MT_GET_REQUEST_TYPE(t) ((t) & MT_TYPE_MASK)

//
// Each message has a message ID. It is assigned and used by the
// protected VM for matching requests and responses. The unikernel
// copies a request's message ID into the corresponding response.
//
typedef uint64_t mt_id_t;

#define MT_ID_UNSET_VALUE (mt_id_t)0

//
// Possible message types - request and response values run in
// parallel. Here's the format of the message type:
//
//  position 76543210 76543210
//  value    sfffffff tttttttt
//
// s = side (0 = request, 1 = response), f = flags, t = type
//

#define _MT_TYPE_MASK_ALLOC_FD    0x0100
#define _MT_TYPE_MASK_DEALLOC_FD  0x0200

// PVM-initiated close() will not execute while this operation is in-process
#define _MT_TYPE_MASK_CLOSE_WAITS 0x0400

//#define _MT_TYPE_MASK_INS_MAIN    0x1000 // INS should process in main thread

#define MT_ALLOCATES_FD(x)   ( (x) & _MT_TYPE_MASK_ALLOC_FD )
#define MT_DEALLOCATES_FD(x) ( (x) & _MT_TYPE_MASK_DEALLOC_FD )

#define MT_CLOSE_WAITS(r)    ( (r)->base.type & _MT_TYPE_MASK_CLOSE_WAITS )

// The INS should process this request in its main thread - there is no thread assignment

typedef enum
{
    MtRequestInvalid        = MT_REQUEST( 0x00 ),

    MtRequestSocketCreate   = MT_REQUEST( 0x01 | _MT_TYPE_MASK_ALLOC_FD ),
    MtRequestSocketShutdown = MT_REQUEST( 0x02 ),
    MtRequestSocketClose    = MT_REQUEST( 0x03 | _MT_TYPE_MASK_DEALLOC_FD ),

    MtRequestSocketConnect  = MT_REQUEST( 0x10 ),
    MtRequestSocketBind     = MT_REQUEST( 0x11 ),
    MtRequestSocketListen   = MT_REQUEST( 0x12 ),
    MtRequestSocketAccept   = MT_REQUEST( 0x13 | _MT_TYPE_MASK_ALLOC_FD ), 

    MtRequestSocketSend     = MT_REQUEST( 0x20 | _MT_TYPE_MASK_CLOSE_WAITS ),
    MtRequestSocketRecv     = MT_REQUEST( 0x21 ),
    MtRequestSocketRecvFrom = MT_REQUEST( 0x22 ),

    MtRequestSocketGetName  = MT_REQUEST( 0x30 | _MT_TYPE_MASK_CLOSE_WAITS ),
    MtRequestSocketGetPeer  = MT_REQUEST( 0x31 | _MT_TYPE_MASK_CLOSE_WAITS ),
    MtRequestSocketAttrib   = MT_REQUEST( 0x32 ),
    MtRequestPollsetQuery   = MT_REQUEST( 0x33 ),
} mt_request_type_t;


typedef enum
{
    MtResponseInvalid           = MT_RESPONSE( MtRequestInvalid        ),
    MtResponseSocketCreate      = MT_RESPONSE( MtRequestSocketCreate   ),
    MtResponseSocketShutdown    = MT_RESPONSE( MtRequestSocketShutdown ),
    MtResponseSocketClose       = MT_RESPONSE( MtRequestSocketClose    ),

    MtResponseSocketConnect     = MT_RESPONSE( MtRequestSocketConnect  ),
    MtResponseSocketBind        = MT_RESPONSE( MtRequestSocketBind     ),
    MtResponseSocketListen      = MT_RESPONSE( MtRequestSocketListen   ),
    MtResponseSocketAccept      = MT_RESPONSE( MtRequestSocketAccept   ),

    MtResponseSocketSend        = MT_RESPONSE( MtRequestSocketSend     ),
    MtResponseSocketRecv        = MT_RESPONSE( MtRequestSocketRecv     ),
    MtResponseSocketRecvFrom    = MT_RESPONSE( MtRequestSocketRecvFrom ),

    MtResponseSocketGetName     = MT_RESPONSE( MtRequestSocketGetName  ),
    MtResponseSocketGetPeer     = MT_RESPONSE( MtRequestSocketGetPeer  ),
    MtResponseSocketAttrib      = MT_RESPONSE( MtRequestSocketAttrib   ),
    MtResponsePollsetQuery      = MT_RESPONSE( MtRequestPollsetQuery   ),
} mt_response_id_t;

typedef uint32_t mt_addrlen_t;

#define MT_INVALID_SOCKET_FD (mw_socket_fd_t)-1
#define MT_INVALID_FD        (mw_socket_fd_t)-1

typedef uint16_t mt_port_t;

// maps to -errno; non-negative value typically means success; must be signed
typedef int32_t mt_status_t;


// A critical error is a 32 bit value starting with the byte 0xc0
#define _CRITICAL_ERROR_PREFIX 0xc0
#define _CRITICAL_ERROR_PREFIX_SHIFT 24
#define _CRITICAL_ERROR_MASK (_CRITICAL_ERROR_PREFIX << _CRITICAL_ERROR_PREFIX_SHIFT)

#define CRITICAL_ERROR(x)                       \
    (_CRITICAL_ERROR_MASK | (x))

#define IS_CRITICAL_ERROR(x)                                            \
    ( ((x) >> _CRITICAL_ERROR_PREFIX_SHIFT) == _CRITICAL_ERROR_PREFIX )




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

// Inet address type
#define MT_INADDR_ANY ((unsigned long int) 0x00000000)


typedef uint32_t mt_flags_t;

//
// Indicates the PVM wrapper will read the response
//
#define _MT_FLAGS_PVM_CALLER_AWAITS_RESPONSE 0x00000001


//
// Did the remote side of the TCP/IP connection close? This can appear
// in response.
//
#define _MT_FLAGS_REMOTE_CLOSED 0x80000000

//
// For supporting multiple send() calls, commonly used to break up
// large messages into smaller chunks
//

#define _MT_FLAGS_BATCH_SEND_INIT 0x40000000
#define _MT_FLAGS_BATCH_SEND      0x20000000
#define _MT_FLAGS_BATCH_SEND_FINI 0x10000000


#define MT_REQUEST_CALLER_WAITS(_req)                                   \
    ( (_req)->base.flags & _MT_FLAGS_PVM_CALLER_AWAITS_RESPONSE )

#define MT_REQUEST_SET_CALLER_WAITS(_req)                               \
    ( (_req)->base.flags |= _MT_FLAGS_PVM_CALLER_AWAITS_RESPONSE )

//
// The preamble for every request.
//
typedef struct MT_STRUCT_ATTRIBS _mt_request_base 
{
    // Must be MT_SIGNATURE_REQUEST
    mt_sig_t            sig;
    
    // MtRequest*
    mt_request_type_t   type;
    
    // All-inclusive size of the populated bytes in the payload,
    // including this structure.
    mt_size_t           size;

    // Server-generated request ID
    mt_id_t             id;

    // The socket. Used in most requests.
    mw_socket_fd_t      sockfd;

    // _MT_FLAGS_* go here. These can change the behavior of the PVM
    // driver rather than the INS and are uneccessarily sent to the
    // INS.

    // Will the user thread wait for a response? This does not need to
    // go over shared memory, but it's more convenient here. An
    // alternate design would be for non-blocking IO to go through
    // aio_read()/aio_write().
    mt_flags_t          flags;
} mt_request_base_t;


//
// The preamble for every response
//
typedef struct MT_STRUCT_ATTRIBS _mt_response_base 
{
    // Must be MT_SIGNATURE_RESPONSE
    mt_sig_t            sig;

    // MtResponse*
    mt_response_id_t    type;

    // Size of the payload only - after this header
    mt_size_t           size;

    // Matches id in corresponding request
    mt_id_t             id;

    // The socket. Used in most requests.
    mw_socket_fd_t      sockfd;

    // Status returned from the call. 0 or an errno. The number of
    // bytes read/written is tracked elsewhere.
    mt_status_t         status;

    // Flags pertaining to response or remote socket state.
    mt_flags_t flags;
} mt_response_base_t;

#define MT_REQUEST_BASE_SIZE  sizeof(mt_request_base_t)
#define MT_RESPONSE_BASE_SIZE sizeof(mt_response_base_t)

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
// Bind
//
typedef struct MT_STRUCT_ATTRIBS _mt_request_socket_bind
{
    mt_request_base_t base;
    mt_sockaddr_in_t sockaddr;  // Hard coded for now, but should be a union of all sockaddr types.
} mt_request_socket_bind_t;

typedef struct MT_STRUCT_ATTRIBS _mt_response_socket_bind
{
    mt_response_base_t base;
} mt_response_socket_bind_t;

#define MT_REQUEST_SOCKET_BIND_SIZE sizeof(mt_request_socket_bind_t)
#define MT_RESPONSE_SOCKET_BIND_SIZE sizeof(mt_response_socket_bind_t)



//
// Listen
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

//
// Flags for accept4(). Only these (Linux) values will be in shared
// memory. See /usr/include/x86_64-linux-gnu/bits/socket_type.h
//

#define MW_SOCK_NONBLOCK  00004000 // yes, base 8


typedef struct MT_STRUCT_ATTRIBS _mt_request_socket_accept
{
    mt_request_base_t base;
    mt_flags_t        flags; // flags, from accept4()
} mt_request_socket_accept_t;

typedef struct MT_STRUCT_ATTRIBS _mt_response_socket_accept
{
    mt_response_base_t base;
    mt_flags_t        flags; // flags from request
    mt_sockaddr_in_t   sockaddr;
} mt_response_socket_accept_t;

#define MT_REQUEST_SOCKET_ACCEPT_SIZE sizeof(mt_request_socket_accept_t)
#define MT_RESPONSE_SOCKET_ACCEPT_SIZE sizeof(mt_response_socket_accept_t)


//
// Flags for send(), recv() and friends. Only these (Linux) values
// will be in shared memory. See /usr/include/x86_64-linux-gnu/bits/socket.h
//

#define MW_MSG_OOB                 0x01
#define MW_MSG_PEEK                0x02
#define MW_MSG_DONTROUTE           0x04	
#define MW_MSG_CTRUNC              0x08	
#define MW_MSG_PROXY               0x10	
#define MW_MSG_TRUNC               0x20
#define MW_MSG_DONTWAIT            0x40 
#define MW_MSG_EOR                 0x80 
#define MW_MSG_WAITALL            0x100 
#define MW_MSG_FIN                0x200
#define MW_MSG_SYN                0x400
#define MW_MSG_CONFIRM            0x800 
#define MW_MSG_RST               0x1000
#define MW_MSG_ERRQUEUE          0x2000
#define MW_MSG_NOSIGNAL          0x4000
#define MW_MSG_MORE              0x8000  
#define MW_MSG_WAITFORONE       0x10000 
#define MW_MSG_FASTOPEN      0x20000000 
#define MW_MSG_CMSG_CLOEXEC  0x40000000	


//
// Recv: both recv and recvfrom use mt_request_socket_recv_t but the
// response types they expect are different.
//

typedef struct MT_STRUCT_ATTRIBS _mt_request_socket_recv
{
    mt_request_base_t base;
    mt_flags_t        flags;
    mt_size_t         requested;
} mt_request_socket_recv_t;

#define MT_REQUEST_SOCKET_RECV_SIZE ( sizeof(mt_request_socket_recv_t ) )

typedef struct MT_STRUCT_ATTRIBS _mt_response_socket_recv
{
    mt_response_base_t base;
    mt_size_t          count;
    uint8_t            bytes[MESSAGE_TYPE_MAX_PAYLOAD_LEN];
} mt_response_socket_recv_t;
#define MT_RESPONSE_SOCKET_RECV_SIZE \
    ( sizeof( mt_response_base_t ) + sizeof( mt_size_t) )

typedef struct MT_STRUCT_ATTRIBS _mt_response_socket_recvfrom
{
    mt_response_base_t base;
    mt_sockaddr_in_t   src_addr;
    uint32_t           addrlen;
    mt_size_t          count;
    uint8_t            bytes[MESSAGE_TYPE_MAX_PAYLOAD_LEN];
} mt_response_socket_recvfrom_t;

#define MT_RESPONSE_SOCKET_RECVFROM_SIZE                                \
    ( sizeof( mt_response_base_t )                                      \
      + sizeof(mt_sockaddr_in_t) + sizeof(mt_size_t) + sizeof(uint32_t) )


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
} mt_response_socket_close_t;

#define MT_REQUEST_SOCKET_CLOSE_SIZE  sizeof(mt_request_socket_close_t)
#define MT_RESPONSE_SOCKET_CLOSE_SIZE sizeof(mt_response_socket_close_t)

//
// Shutdown
//

typedef struct MT_STRUCT_ATTRIBS _mt_request_socket_shutdown
{
    mt_request_base_t base;
    uint32_t          how; // standardized values; translation not needed
} mt_request_socket_shutdown_t;

typedef struct MT_STRUCT_ATTRIBS _mt_response_socket_shutdown
{
    mt_response_base_t base;
} mt_response_socket_shutdown_t;

#define MT_REQUEST_SOCKET_SHUTDOWN_SIZE  sizeof(mt_request_socket_shutdown_t)
#define MT_RESPONSE_SOCKET_SHUTDOWN_SIZE sizeof(mt_response_socket_shutdown_t)


//
// Write
//
typedef struct MT_STRUCT_ATTRIBS _mt_request_socket_send
{
    mt_request_base_t  base;
    mt_flags_t         flags;
    uint8_t            bytes[ MESSAGE_TYPE_MAX_PAYLOAD_LEN ];
} mt_request_socket_send_t;

typedef struct MT_STRUCT_ATTRIBS _mt_response_socket_send
{
    mt_response_base_t base;
    // orig flags from the request, needed by PVM for post-processing
    mt_flags_t         flags; 
    mt_size_t          count; // bytes sent
} mt_response_socket_send_t;

// User must add count of filled bytes to size
#define MT_REQUEST_SOCKET_SEND_SIZE  \
    ( sizeof(mt_request_base_t) + sizeof(mt_flags_t) )

#define MT_RESPONSE_SOCKET_SEND_SIZE sizeof(mt_response_socket_send_t)


//
// GetName, GetPeer
//
typedef struct MT_STRUCT_ATTRIBS _mt_request_socket_getname
{
    mt_request_base_t base;
    mt_size_t         maxlen;
} mt_request_socket_getname_t;

typedef struct MT_STRUCT_ATTRIBS _mt_response_socket_getname
{
    mt_response_base_t base;
    mt_size_t          reslen;
    mt_sockaddr_in_t sockaddr;  // Hard coded for now, but should be a union of all sockaddr types.
} mt_response_socket_getname_t;

#define mt_request_socket_getpeer_t  mt_request_socket_getname_t
#define mt_response_socket_getpeer_t mt_response_socket_getname_t

#define MT_REQUEST_SOCKET_GETNAME_SIZE sizeof(mt_request_socket_getname_t)
#define MT_RESPONSE_SOCKET_GETNAME_SIZE sizeof(mt_response_socket_getname_t)

#define MT_REQUEST_SOCKET_GETPEER_SIZE MT_REQUEST_SOCKET_GETNAME_SIZE
#define MT_RESPONSE_SOCKET_GETPEER_SIZE MT_RESPONSE_SOCKET_GETNAME_SIZE


//
// Socket behavioral attributes, normally set via setsockopt() and
// fcntl(). This subset is supported by Magic Wand.
//
typedef enum
{
    MtSockAttribNone,
    MtSockAttribNonblock,
    MtSockAttribReuseaddr,
    MtSockAttribKeepalive,
    MtSockAttribDeferAccept,
    MtSockAttribNodelay,
} mt_socket_attrib_t;


typedef struct MT_STRUCT_ATTRIBS _mt_request_socket_attrib
{
    mt_request_base_t   base;
    uint32_t            modify;  // bool: true => set, false => get
    mt_socket_attrib_t  attrib;  // value of the single attribute of interest
    uint32_t            value;   // value of specified attribute
} mt_request_socket_attrib_t;

#define MT_REQUEST_SOCKET_ATTRIB_SIZE sizeof(mt_request_socket_attrib_t)


typedef struct MT_STRUCT_ATTRIBS _mt_response_socket_attrib
{
    mt_response_base_t base;
    uint32_t           outval; // optional value of requested attrib
} mt_response_socket_attrib_t;

#define MT_RESPONSE_SOCKET_ATTRIB_SIZE sizeof(mt_response_socket_attrib_t)


//
// Poll sets: only these (Linux-based) values will be in shared memory
//
#define MW_POLLIN     0x001
#define MW_POLLPRI    0x002
#define MW_POLLOUT    0x004

#define MW_POLLERR    0x008
#define MW_POLLHUP    0x010
#define MW_POLLNVAL   0x020

#define MW_POLLRDNORM 0x040
#define MW_POLLRDBAND 0x080
#define MW_POLLWRNORM 0x100
#define MW_POLLWRBAND 0x200


typedef struct MT_STRUCT_ATTRIBS _mt_request_pollset_query
{
    mt_request_base_t base;
} mt_request_pollset_query_t;

#define MT_REQUEST_POLLSET_QUERY_SIZE MT_REQUEST_BASE_SIZE


typedef struct MT_STRUCT_ATTRIBS _mt_response_pollset_query_item
{
    mw_socket_fd_t sockfd;
    uint32_t       events; // MW_POLL*
} mt_response_pollset_query_item_t;

#define MT_RESPONSE_POLLSET_QUERY_ITEM_SIZE \
    sizeof( mt_response_pollset_query_item_t )

#define MT_POLLSET_QUERY_MAX_ITEMS \
    (MESSAGE_TYPE_MAX_PAYLOAD_LEN / MT_RESPONSE_POLLSET_QUERY_ITEM_SIZE )


typedef struct MT_STRUCT_ATTRIBS _mt_response_pollset_query
{
    mt_response_base_t base;
    uint32_t           count;
    mt_response_pollset_query_item_t items[1];
} mt_response_pollset_query_t;

// Base size: size of items needed too
#define MT_RESPONSE_POLLSET_QUERY_SIZE                  \
    ( sizeof( mt_response_base_t) + sizeof(uint32_t) )


//
// All inclusive types to be used before the message type is known
//

typedef union _mt_request_generic
{
    mt_request_base_t             base;
    mt_request_socket_create_t    socket_create;
    mt_request_socket_connect_t   socket_connect;
    mt_request_socket_close_t     socket_close;
    mt_request_socket_shutdown_t  socket_shutdown;
    mt_request_socket_send_t      socket_send;
    mt_request_socket_bind_t      socket_bind;
    mt_request_socket_listen_t    socket_listen;
    mt_request_socket_accept_t    socket_accept;
    mt_request_socket_recv_t      socket_recv;
    mt_request_socket_getname_t   socket_getname;
    mt_request_socket_getpeer_t   socket_getpeer;

    mt_request_socket_attrib_t    socket_attrib;
    mt_request_pollset_query_t    pollset_query;
} mt_request_generic_t;

#define MT_REQUEST_BASE_GET_TYPE(rqb) ((rqb)->type)
#define MT_REQUEST_GET_TYPE(rq) ((rq)->base.type)
#define MT_IS_REQUEST(x)                                                \
    ( (MT_SIGNATURE_REQUEST == (x)->base.sig) &&                        \
      (0 == (MT_RESPONSE_MASK & (x)->base.type)) &&                     \
      ((x)->base.size >= sizeof(mt_request_base_t)) )


typedef union _mt_response_generic
{
    mt_response_base_t              base;
    mt_response_socket_create_t     socket_create;
    mt_response_socket_connect_t    socket_connect;
    mt_response_socket_close_t      socket_close;
    mt_response_socket_shutdown_t   socket_shutdown;
    mt_response_socket_send_t       socket_send;
    mt_response_socket_bind_t       socket_bind;
    mt_response_socket_listen_t     socket_listen;
    mt_response_socket_accept_t     socket_accept;
    mt_response_socket_recv_t       socket_recv;
    mt_response_socket_recvfrom_t   socket_recvfrom;
    mt_response_socket_getname_t    socket_getname;
    mt_response_socket_getpeer_t    socket_getpeer;

    mt_response_socket_attrib_t     socket_attrib;
    mt_response_pollset_query_t     pollset_query;
} mt_response_generic_t;

#define MT_RESPONSE_BASE_GET_TYPE(rqb) ((rqb)->type)
#define MT_RESPONSE_GET_TYPE(rq) ((rq)->base.type)

#define MT_IS_RESPONSE(x)                                               \
    ((MT_SIGNATURE_RESPONSE == (x)->base.sig) &&                        \
     (MT_RESPONSE_MASK == (MT_RESPONSE_MASK & (x)->base.type)) &&       \
     ((x)->base.size >= sizeof(mt_response_base_t)) )

// Container that holds either a request or a response
typedef union _mt_request_response_union
{
    mt_request_generic_t  request;
    mt_response_generic_t response;
} mt_request_response_union_t;

#endif // message_types_h
