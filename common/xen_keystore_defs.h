#ifndef xenevent_config_h
#define xenevent_config_h

//
// XenStore keys and specs
//

// DomId max digit width 
#define MAX_DOMID_WIDTH           5
// Grant ref max digit width 
#define MAX_GNT_REF_WIDTH         15

// Maximum length permitted for key that we read or write 
#define MAX_KEY_VAL_WIDTH         32


//
// Default key reset value
//
#define KEY_RESET_VAL             "0"
#define KEY_RESET_INT             0


//
// Define the out-of-band keys here.
//
// These should be in a header file that is common to both sides of
// the system.
//

#define XENEVENT_XENSTORE_ROOT "/unikernel/random"

#define PRIVATE_ID_PATH           "domid"


#define SERVER_ID_PATH            XENEVENT_XENSTORE_ROOT "/server_id" 
#define CLIENT_ID_PATH            XENEVENT_XENSTORE_ROOT "/client_id" 

#define GRANT_REF_PATH            XENEVENT_XENSTORE_ROOT "/gnt_ref"
#define MSG_LENGTH_PATH           XENEVENT_XENSTORE_ROOT "/msg_len"

#define VM_EVT_CHN_PRT_PATH       XENEVENT_XENSTORE_ROOT "/vm_evt_chn_prt"
#define VM_EVT_CHN_IS_BOUND       XENEVENT_XENSTORE_ROOT "/vm_evt_chn_is_bound"

//
//  Grant Mapping Variables 
//

//
// Number of grant refs (1/page). This determines how much space we
// have for our ring buffer. For instance, if the order is 6, we share
// 2^6 = 64 (0x40) pages, or 0x40000 bytes.
//

//#define XENEVENT_GRANT_REF_ORDER  6 // (2^order == page count)
#define XENEVENT_GRANT_REF_ORDER  1 // (2^order == page count)
#define XENEVENT_GRANT_REF_COUNT  (1 << XENEVENT_GRANT_REF_ORDER)

// Split the grant refs apart by this in XenStore
#define XENEVENT_GRANT_REF_DELIM " "


#endif // xenevent_config_h
