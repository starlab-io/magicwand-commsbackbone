#ifndef app_common_h
#define app_common_h

#define DEBUG_PRINT_FUNCTION printf
#define DEBUG_FLUSH_FUNCTION fflush

#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>


#ifdef MYDEBUG
static pthread_mutex_t __debug_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

//
// Decorators for function parameters
//
#define IN
#define OUT
#define INOUT

//
// General helper macros
//
#ifndef NUMBER_OF
#  define NUMBER_OF(x) (sizeof(x)/sizeof(x[0]))
#endif

#include <string.h>
#define SHORT_FILE strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__

//
// Debugging helpers
//

#ifdef MYDEBUG

// Using gettid() would be better, but it isn't portable from Linux

#  define DEBUG_EMIT_META()                     \
    DEBUG_PRINT_FUNCTION( "P%d [%s:%d] ", getpid(), SHORT_FILE, __LINE__ )
#else
#  define DEBUG_EMIT_META() ((void)0)
#endif


#ifdef MYTRAP
#  define BARE_DEBUG_BREAK() asm("int $3")
#else
#  define BARE_DEBUG_BREAK() ((void)0)
#endif

#if (defined MYDEBUG) && (defined MYTRAP)
#  define DEBUG_BREAK()                                                 \
    DEBUG_EMIT_META();                                                  \
    DEBUG_PRINT_FUNCTION( "At breakpoint\n" );                          \
    DEBUG_FLUSH_FUNCTION(stdout);                                       \
    asm("int $3\n\t")
#else
#  define DEBUG_BREAK() ((void)0)
#endif

#ifdef MYDEBUG
#  define DEBUG_PRINT(...)                                              \
    pthread_mutex_lock( &__debug_mutex );                               \
    DEBUG_EMIT_META();                                                  \
    DEBUG_PRINT_FUNCTION(__VA_ARGS__);                                  \
    DEBUG_FLUSH_FUNCTION(stdout);                                       \
    pthread_mutex_unlock( &__debug_mutex )
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


#ifndef MIN
#  define MIN(x, y) ( (x) > (y) ? (y) : (x) )
#endif // MIN

#ifndef MAX
#  define MAX(x, y) ( (x) > (y) ? (x) : (y) )
#endif // MAX

#endif // app_common_h
