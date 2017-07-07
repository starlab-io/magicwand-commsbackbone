#ifndef xenevent_comms_h
#define xenevent_comms_h

#include "xenevent_common.h"
#include "xenevent_minios.h"

//
// Test Message Variables and Specs 
//

// Test Message Size
#define TEST_MSG_SZ               64

// Max Message Width
#define MAX_MSG_WIDTH             5

// Test Message 
#define TEST_MSG                  "The abyssal plain is flat.\n"

//
// TODO: How do we set this? Is it per session? Per message?
//
typedef uint64_t event_id_t;

#define EVENT_ID_INVALID (event_id_t)0

int
xe_comms_init( void );


int
xe_comms_write_str_to_key( const char * Path,
                           const char * Value );

int
xe_comms_publish_ip_addr( const char * Ip );

int
xe_comms_fini( void );

int
xe_comms_read_item( void * Memory,
                    size_t Size,
                    size_t * BytesRead );

int
xe_comms_write_item( void * Memory,
                     size_t Size,
                     size_t * BytesRead );

int
xe_comms_get_domid( void );

int
xe_comms_heartbeat( void );

#endif //xenevent_comms_h
