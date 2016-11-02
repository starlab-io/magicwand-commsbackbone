#include <mini-os/types.h>
#include <xen/xen.h>
#include <xen/io/xs_wire.h>

#if defined(__i386__)
#include <mini-os/x86/x86_32/hypercall-x86_32.h>
#elif defined(__x86_64__)
#include <mini-os/x86/x86_64/hypercall-x86_64.h>
#else
#error "Unsupported architecture"
#endif

unsigned int find_friend_domu_id(void);

