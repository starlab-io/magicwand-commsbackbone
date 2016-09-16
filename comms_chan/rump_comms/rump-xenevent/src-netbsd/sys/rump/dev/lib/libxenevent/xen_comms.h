#ifndef xen_iface_h
#define xen_iface_h

#include "xenevent_common.h"

/************************************
*
* XenStore Keys and Specs 
*
*************************************/
/* DomId max digit width */ 
#define MAX_DOMID_WIDTH           5
/* Grant ref max digit width */ 
#define MAX_GNT_REF_WIDTH         15

// Maximum length permitted for key that we read or write 
#define MAX_KEY_VAL_WIDTH         32


/* Default key reset value */
#define KEY_RESET_VAL             "0"
#define KEY_RESET_INT             0
/* Define out-of-band keys */
#define SERVER_ID_PATH            "/unikernel/random/server_id" 
#define CLIENT_ID_PATH            "/unikernel/random/client_id" 
#define PRIVATE_ID_PATH           "domid"
#define GRANT_REF_PATH            "/unikernel/random/gnt_ref"
#define MSG_LENGTH_PATH           "/unikernel/random/msg_len"
#define EVT_CHN_PRT_PATH          "/unikernel/random/evt_chn_port"
#define LOCAL_PRT_PATH            "/unikernel/random/client_local_port"

/************************************
*
*  Grant Mapping Variables 
*
*************************************/
/* Default Nmbr of Grant Refs */
#define DEFAULT_NMBR_GNT_REF      1
/* Default Stride */
#define DEFAULT_STRIDE            1
/* Write access to shared mem */
#define WRITE_ACCESS_ON           1
/* First Domain Slot */
#define FIRST_DOM_SLOT            0 
/* First Grant Ref */
#define FIRST_GNT_REF             0 

/************************************
*
* Test Message Variables and Specs 
*
*************************************/
/* Test Message Size */
#define TEST_MSG_SZ               64
/* Max Message Width  */
#define MAX_MSG_WIDTH             5
/* Test Message */
#define TEST_MSG                  "The abyssal plain is flat.\n"

//
// TODO: How do we set this? Is it per session? Per message?
//
typedef uint64_t event_id_t;

#define EVENT_ID_INVALID (event_id_t)0

int
xe_comms_init( void );

int xe_comms_read_data( event_id_t Id,
                        void * Memory,
                        size_t Size );

int
xe_comms_fini( void );

#endif //xen_iface_h
