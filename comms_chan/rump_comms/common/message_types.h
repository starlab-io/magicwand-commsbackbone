#ifndef message_types_h
#define message_types_h

/***************************************************************************
 * Defines the message types that are passed between the Protected VM
 * and the unikernel via Xen shared memory.
 *
 * The user of this include file must have defined primitive types
 * like uint8_t, uint32_t, etc prior to including this file.
 ***************************************************************************/

/*

// XXXX do this smartly
#define MESSAGE_TYPE_MAX_PAYLOAD_LEN 1500

typedef uint16_t mt_type_t;
typedef uint16_t mt_length_t;
typedef uint32_t mt_id_t;

typedef uint32_t mt_socket_fd_t;
typedef uint32+t mt_status_t;

typedef struct _message_base
{
    mt_type_t   type;
    mt_length_t len;
    mt_id_t     id;
} message_base_t;


typedef struct

typedef struct _reponse_base
{
    
} reponse_base_t;

//
// Request and response types come in pairs. The response types have
// the bits in MT_REPONSE_MASK set, e.g
//   MT_SOCKET_CREATE_REQUEST_TYPE  0x0001
//   MT_SOCKET_CREATE_RESPONSE_TYPE 0x7001
//

#define MT_REPONSE_MASK 0x7000

#define MT_REQUEST(x) (x)
#define MT_RESPONSE(x) ( MT_REPONSE_MASK | (x) )
#define MT_IS_REQUEST(x) (~MT_RESPONSE_MASK & (x) == (x))
#define MT_IS_RESPONSE(x) (MT_RESPONSE_MASK & (x) == MT_RESPONSE_MASK)

#define MT_DEFINE_REQUEST_REPONSE( __name, val )            \
    #define __name##_REQUEST_TYPE    MT_REQUEST(val)        \
    #define __name##_RESPONSE_TYPE   MT_RESPONSE(val)      


#define 


//
// Socket creation
//

MT_DEFINE_REQUEST_RESPONSE( MT_SOCKET_CREATE, 1)

typedef struct _socket_

//



#define MT_REQUEST(x) (x)
#DEFINE MT_RESPONSE_type(x) 



//
//
//





*/




typedef struct _command_message
{
    uint32_t size;
    uint8_t buf[1024];
} command_message_t;


typedef struct _response_message
{
    uint32_t size;
    uint8_t buf[24];
} response_message_t;



#endif // message_types_h
