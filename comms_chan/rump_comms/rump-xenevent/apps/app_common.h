#ifndef app_common_h
#define app_common_h

#define DEBUG_PRINT_FUNCTION printf

#include <pthread.h>

static pthread_mutex_t __common_mutex = PTHREAD_MUTEX_INITIALIZER;


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
#  define DEBUG_BREAK() \
    DEBUG_PRINT_FUNCTION ( "[%s:%d] %s\tAt breakpoint\n", SHORT_FILE, __LINE__, __FUNCTION__); \
    asm("int $3\n\t")
#else
#  define DEBUG_BREAK() ((void)0)
#endif

#ifdef MYDEBUG
#  define DEBUG_PRINT(...)                                              \
    pthread_mutex_lock( &__common_mutex );                              \
    DEBUG_PRINT_FUNCTION ( "[%s:%d] %s\t", SHORT_FILE, __LINE__, __FUNCTION__); \
    DEBUG_PRINT_FUNCTION(__VA_ARGS__);                                  \
    pthread_mutex_unlock( &__common_mutex )
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
