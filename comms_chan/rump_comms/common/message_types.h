#ifndef message_types_h
#define message_types_h

/***************************************************************************
 * Defines the message types that are passed between the Protected VM
 * and the unikernel via Xen shared memory.
 *
 * The user of this include file must have defined primitive types
 * like uint8_t, uint32_t, etc prior to including this file.
 ***************************************************************************/


// XXXXXXXXXXXXXXXXXXXXXX put real definitions here

typedef struct _command_message
{
    uint8_t buf[1024];
} command_message_t;


typedef struct _response_message
{
    uint8_t buf[24];
} response_message_t;



#endif // message_types_h
