//
// Defines IOCTLs for the INS to communicate with the
// Magicwand - system
//
#ifndef ins_ioctl_h
#define ins_ioctl_h

// Network statistics are passed with the heartbeat IOCTL. The format
// of the stats is: "socket_ct:bytes_recv:bytes_sent", where all
// numbers are base 16. See xen_keystore_defs.h also
#define INS_NETWORK_STATS_MAX_LEN 64
#define INS_LISTENING_PORTS_MAX_LEN INS_NETWORK_STATS_MAX_LEN 

#define INS_SOCK_PARAMS_MAX_LEN 128

#define INS_GET_SOCK_PARAMS_IOCTL                               \
    _IOWR( 'i', 71, const char[INS_SOCK_PARAMS_MAX_LEN] )

#define INS_HEARTBEAT_IOCTL                                     \
    _IOW( 'i', 72, const char[INS_NETWORK_STATS_MAX_LEN] )

#define INS_PUBLISH_LISTENERS_IOCTL                             \
    _IOW( 'i', 73, const char[INS_NETWORK_STATS_MAX_LEN] )

// User must define domid_t
#define INS_DOMID_IOCTL                         \
    _IOWR( 'i', 74, domid_t )

#endif
