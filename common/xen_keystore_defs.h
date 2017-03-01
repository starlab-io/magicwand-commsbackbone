#ifndef xenevent_config_h
#define xenevent_config_h


#define _common_config_defined
#   include "common_config.h"
#undef  _common_config_defined


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

#define PRIVATE_ID_PATH       "domid"

#define SERVER_ID_KEY             "server_id" 
#define SERVER_ID_PATH            XENEVENT_XENSTORE_ROOT "/" SERVER_ID_KEY

#define CLIENT_ID_KEY             "client_id" 
#define CLIENT_ID_PATH            XENEVENT_XENSTORE_ROOT "/" CLIENT_ID_KEY

#define GNT_REF_KEY               "gnt_ref" 
#define GRANT_REF_PATH            XENEVENT_XENSTORE_ROOT "/" GNT_REF_KEY

#define MSG_LEN_KEY               "msg_len"
#define MSG_LEN_PATH              XENEVENT_XENSTORE_ROOT "/" MSG_LEN_KEY

#define VM_EVT_CHN_PORT_KEY       "vm_evt_chn_prt"
#define VM_EVT_CHN_PORT_PATH      XENEVENT_XENSTORE_ROOT "/" VM_EVT_CHN_PORT_KEY

#define VM_EVT_CHN_BOUND_KEY      "vm_evt_chn_is_bound"
#define VM_EVT_CHN_BOUND_PATH     XENEVENT_XENSTORE_ROOT "/" VM_EVT_CHN_BOUND_KEY

//
//  Grant Mapping Variables 
//

// XXXX: in moving to a multi-rump model, we can have smaller but more
// shared regions. now we just have one.

// XENEVENT_GRANT_REF_ORDER is in common_config.h
#define XENEVENT_GRANT_REF_COUNT  (1 << XENEVENT_GRANT_REF_ORDER)

// Split the grant refs apart by this in XenStore
#define XENEVENT_GRANT_REF_DELIM " "


#endif // xenevent_config_h
