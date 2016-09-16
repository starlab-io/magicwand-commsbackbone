#ifndef app_common_h
#define app_common_h

#define DEBUG_PRINT_FUNCTION printf

//
// Decorators for function parameters
//
#define IN
#define OUT
#define INOUT

//
// Debugging helpers
//
#ifdef MYDEBUG
#  define DEBUG_BREAK() \
    DEBUG_PRINT_FUNCTION ( "[%s:%d] %s\tAt breakpoint\n", __FILE__, __LINE__, __FUNCTION__); \
    asm("int $3\n\t")
#else
#  define DEBUG_BREAK() ((void)0)
#endif

#ifdef MYDEBUG
#  define DEBUG_PRINT(...)                                     \
    DEBUG_PRINT_FUNCTION ( "[%s:%d] %s\t", __FILE__, __LINE__, __FUNCTION__); \
    DEBUG_PRINT_FUNCTION(__VA_ARGS__)
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
