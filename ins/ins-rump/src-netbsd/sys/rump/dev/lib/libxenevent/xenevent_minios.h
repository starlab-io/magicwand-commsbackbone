#ifndef xenevent_minios_h
#define xenevent_minios_h

//
// Functions for general usage that depend on mini-os. We keep them
// seperate to avoid header file conflicts.
//


typedef void * xenevent_semaphore_t;

int
xenevent_semaphore_init( xenevent_semaphore_t * Semaphore );

void
xenevent_semaphore_destroy( xenevent_semaphore_t * Semaphore );

void
xenevent_semaphore_up( xenevent_semaphore_t Semaphore );

void
xenevent_semaphore_down( xenevent_semaphore_t Semaphore );

int
xenevent_semaphore_trydown( xenevent_semaphore_t Semaphore );

#endif // xenevent_minios_h
