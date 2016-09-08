#ifndef xenevent_common_h
#define xenevent_common_h

#define DEVICE_NAME "xenevent"
#define DEVICE_PATH "/dev/xenevent"
//#define XENEVENT_DEVICE 0x125 // ??

typedef uint32_t status_t;

#ifdef MYDEBUG
#  define DEBUG_BREAK() asm("int $3\n\t")
#else
#  define DEBUG_BREAK() ((void)0)
#endif

#ifdef MYDEBUG
#  include <sys/cdefs.h> // printf ?
#  define DEBUG_PRINT(...) printf ( "%s:%d ");printf(__VA_ARGS__)
#else
#  define DEBUG_PRINT(...) ((void)0)
#endif // MYDEBUG


#ifdef MYDEBUG
#define MYASSERT(x) \
    if(!(x)) { \
       DEBUG_PRINT("Assertion failure: %s\n", #x); \
       DEBUG_BREAK();                              \
    }
#else
#  define MYASSERT(...) ((void)0)
#endif // MYDEBUG


#endif // xenevent_common_h
