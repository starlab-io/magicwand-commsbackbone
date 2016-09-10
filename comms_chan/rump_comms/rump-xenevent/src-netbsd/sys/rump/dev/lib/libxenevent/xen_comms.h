#ifndef xen_iface_h
#define xen_iface_h

#include "xenevent_common.h"


//typedef void (*SignalHandler)(int signum);
typedef int (*xen_event_callback_t)(void);

int
xen_comms_init( void );

int
xen_comms_register_callback( xen_event_callback_t Callback );

int
xen_comms_fini( void );

#endif //xen_iface_h
