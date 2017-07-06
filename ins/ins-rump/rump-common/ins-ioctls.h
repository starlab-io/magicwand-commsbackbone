//
// Defines IOCTLs for the INS to communicate with the
// Magicwand - system
//
#ifndef ins_ioctl_h
#define ins_ioctl_h


#define domid_t uint16_t

#define INSHEARTBEATIOCTL   _IO(   'i', 62 )
#define DOMIDIOCTL          _IOWR( 'i', 63, domid_t )

#define INS_HEARTBEAT_INTERVAL 1

#endif
