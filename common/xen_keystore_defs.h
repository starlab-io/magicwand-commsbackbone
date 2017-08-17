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

#define SERVER_NETFLOW_PORT_KEY     "netflow"
#define SERVER_NETFLOW_PORT_PATH    XENEVENT_XENSTORE_PVM "/" SERVER_NETFLOW_PORT_KEY

// INS constants
#define XENEVENT_PATH_STR_LEN       32

#define GNT_REF_KEY                 "gnt_ref" 
#define VM_EVT_CHN_PORT_KEY         "vm_evt_chn_prt"
#define VM_EVT_CHN_BOUND_KEY        "vm_evt_chn_is_bound"

#define INS_ID_KEY                   "ins_dom_id"
#define INS_IP_ADDR_KEY              "ip_addrs"
#define INS_HEARTBEAT_KEY            "heartbeat"

#define INS_SOCKET_PARAMS_KEY        "sockopts"

// Format of value: "socket_ct:bytes_recv:bytes_sent". All numbers are
// base 16. See also ins-ioctls.h.
#define INS_NETWORK_STATS_KEY        "network_stats"

#define INS_LISTENER_KEY             "listening_ports"

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
