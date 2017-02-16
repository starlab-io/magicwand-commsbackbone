#ifndef app_common_h
#define app_common_h

#define DEBUG_PRINT_FUNCTION printf
#define DEBUG_FLUSH_FUNCTION fflush

#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>

//
// MYTRAP enables breakpoints and asserts (along with the assert message)
// MYDEBUG enables DEBUG_PRINT()
//

#if (defined MYDEBUG) || (defined MYTRAP)
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

// Using gettid() would be better, but it's only available on Linux
#define _DEBUG_EMIT_META() \
    DEBUG_PRINT_FUNCTION( "P%d [%s:%d] ", getpid(), SHORT_FILE, __LINE__ )

#define _DEBUG_EMIT_BREAKPOINT()                \
    asm("int $3")

#ifdef MYDEBUG
#  define DEBUG_EMIT_META() _DEBUG_EMIT_META()
#else
#  define DEBUG_EMIT_META() ((void)0)
#endif

#ifdef MYTRAP
#  define DEBUG_EMIT_BREAKPOINT() _DEBUG_EMIT_BREAKPOINT()
#else
#  define DEBUG_EMIT_BREAKPOINT() ((void)0)
#endif

// Unconditionally emits breakpoint
#define BARE_DEBUG_BREAK() _DEBUG_EMIT_BREAKPOINT()

#ifdef MYTRAP

#  define DEBUG_BREAK()                                                 \
    _DEBUG_EMIT_META();                                                 \
    DEBUG_PRINT_FUNCTION( "At breakpoint\n" );                          \
    DEBUG_FLUSH_FUNCTION(stdout);                                       \
    _DEBUG_EMIT_BREAKPOINT()

// MYASSERT emits breakpoint only if MYTRAP is defined.
// If MYDEBUG is defined it will print out the message
#  define MYASSERT(x)                                                   \
    if(!(x)) {                                                          \
        pthread_mutex_lock( &__debug_mutex );                           \
        _DEBUG_EMIT_META();                                             \
        DEBUG_PRINT_FUNCTION( "Assertion failure: %s\n", #x );          \
        DEBUG_EMIT_BREAKPOINT();                                        \
        pthread_mutex_unlock( &__debug_mutex );                         \
    }

#else

#  define DEBUG_BREAK()   ((void)0)
#  define MYASSERT(x)     ((void)0)
#endif


#ifdef MYDEBUG
#  define DEBUG_PRINT(...)                                              \
    pthread_mutex_lock( &__debug_mutex );                               \
    _DEBUG_EMIT_META();                                                 \
    DEBUG_PRINT_FUNCTION(__VA_ARGS__);                                  \
    DEBUG_FLUSH_FUNCTION(stdout);                                       \
    pthread_mutex_unlock( &__debug_mutex )


#else
#  define DEBUG_PRINT(...) ((void)0)
#endif // MYDEBUG


#ifndef MIN
#  define MIN(x, y) ( (x) > (y) ? (y) : (x) )
#endif // MIN

#ifndef MAX
#  define MAX(x, y) ( (x) > (y) ? (x) : (y) )
#endif // MAX

#endif // app_common_h
