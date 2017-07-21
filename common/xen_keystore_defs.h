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

// These are defined for initializing the keystore from within the driver
#define XENEVENT_XENSTORE_PVM_NODE      "pvm"
#define XENEVENT_XENSTORE_INS_NODE      "ins"
#define XENEVENT_NO_NODE                ""

// These definitions are used for navigating the keystore

#define XENEVENT_XENSTORE_ROOT      "/mw"

#define XENEVENT_XENSTORE_PVM       XENEVENT_XENSTORE_ROOT "/" XENEVENT_XENSTORE_PVM_NODE

#define PRIVATE_ID_PATH             "domid"

#define SERVER_ID_KEY               "id"
#define SERVER_ID_PATH              XENEVENT_XENSTORE_PVM "/" SERVER_ID_KEY

#define SERVER_BACKCHANNEL_PORT_KEY  "backchannel_port"
#define SERVER_BACKCHANNEL_PORT_PATH XENEVENT_XENSTORE_PVM "/" SERVER_BACKCHANNEL_PORT_KEY

// INS constants
#define XENEVENT_PATH_STR_LEN       32

#define CLIENT_ID_KEY               "ins_dom_id" 

#define GNT_REF_KEY                 "gnt_ref" 

#define VM_EVT_CHN_PORT_KEY         "vm_evt_chn_prt"

#define VM_EVT_CHN_BOUND_KEY        "vm_evt_chn_is_bound"

#define HEARTBEAT_KEY               "heartbeat"

//This defines the index of the domid string once
//a xenstore path has been returned and split with
//token  ex /mw/5/vm_evt_chn_is_bound -> 5 is the
//domid at index 2
#define XEN_DOMID_INDEX             2

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
