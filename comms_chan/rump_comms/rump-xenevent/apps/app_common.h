#ifndef app_common_h
#define app_common_h

#define MYDEBUG 1

#ifdef MYDEBUG
#  define DEBUG_BREAK() asm("int $3\n\t")
#else
#  define DEBUG_BREAK() ((void)0)
#endif

#ifdef MYDEBUG
#  include <sys/cdefs.h> // printf ?
#  define DEBUG_PRINT(...) \
    printf ( "%s:%d ", __FUNCTION__, __LINE__); \
    printf(__VA_ARGS__)
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

#endif // app_common_h
