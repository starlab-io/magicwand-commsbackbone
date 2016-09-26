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
#define UK_EVT_CHN_PRT_PATH       XENEVENT_XENSTORE_ROOT "/uk_evt_chn_prt"

#define VM_EVT_CHN_IS_BOUND       XENEVENT_XENSTORE_ROOT "/vm_evt_chn_is_bound"


//
//  Grant Mapping Variables 
//

//
// Default Nmbr of Grant Refs
//

#define DEFAULT_NMBR_GNT_REF      1
// Default Stride
#define DEFAULT_STRIDE            1
// Write access to shared mem 
#define WRITE_ACCESS_ON           1
// First Domain Slot
#define FIRST_DOM_SLOT            0 
// First Grant Ref 
#define FIRST_GNT_REF             0 

#endif // xenevent_config_h
