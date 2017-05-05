//
// Defines IOCTLs for the INS to communicate with the
// Magicwand - system
//


#ifndef _ins_ioctl_h
#define _ins_ioctl_h
#endif

#define INSHEARTBEATIOCTL   _IO(   'i', 62 )
#define DOMIDIOCTL          _IOWR( 'i', 63, void * )

