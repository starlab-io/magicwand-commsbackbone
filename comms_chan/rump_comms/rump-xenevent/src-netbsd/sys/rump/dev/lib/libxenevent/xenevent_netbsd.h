#ifndef xenevent_netbsd_h
#define xenevent_netbsd_h


typedef void * xenevent_mutex_t;


//
// Symbols that every component can use. These can be in various
// modules, depending on where makes sense as dictated by header file
// conflicts.
//
void
hex_dump( const char *desc, void *addr, int len );

int
xenevent_mutex_init( xenevent_mutex_t * Mutex );

void
xenevent_mutex_wait( xenevent_mutex_t Mutex );

void
xenevent_mutex_release( xenevent_mutex_t Mutex );

void
xenevent_mutex_destroy( xenevent_mutex_t * Mutex );

uint32_t
xenevent_atomic_inc( uint32_t * old );

uint32_t
xenevent_atomic_dec( uint32_t * old );

#endif // xenevent_netbsd_h
