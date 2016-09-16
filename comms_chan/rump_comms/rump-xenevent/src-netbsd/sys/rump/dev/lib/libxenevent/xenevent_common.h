#ifndef xenevent_common_h
#define xenevent_common_h

#define DEVICE_NAME "xe"
#define DEVICE_PATH "/dev/xe"

#include <sys/stdbool.h> // bool, true, false

// Basic error codes:
#include <bmk-core/errno.h>
// #define BMK_ENOENT              2
// #define BMK_EIO                 5
// #define BMK_ENXIO               6
// #define BMK_E2BIG               7
// #define BMK_EBADF               9
// #define BMK_ENOMEM              12
// #define BMK_EBUSY               16
// #define BMK_EINVAL              22
// #define BMK_EROFS               30
// #define BMK_ETIMEDOUT           60
// #define BMK_ENOSYS              78


typedef uint32_t status_t;

//
// Define the out-of-band keys here.
//
// These should be in a header file that is common to both sides of
// the system.
//

#define SERVER_ID_PATH            "/unikernel/random/server_id" 
#define CLIENT_ID_PATH            "/unikernel/random/client_id" 
#define PRIVATE_ID_PATH           "domid"
#define GRANT_REF_PATH            "/unikernel/random/gnt_ref"
#define MSG_LENGTH_PATH           "/unikernel/random/msg_len"
#define EVT_CHN_PRT_PATH          "/unikernel/random/evt_chn_port"
#define LOCAL_PRT_PATH            "/unikernel/random/client_local_port"

#define MYDEBUG 1 // -----------------------

//
// By default, these macros will use printf. If the user cannot define
// printf due to header file conflicts, then the user must define
// DEBUG_PRINT_FUNCTION to the symbol that should be used.
//

#ifndef DEBUG_PRINT_FUNCTION
#   define DEBUG_PRINT_FUNCTION printf
#endif

//
// These macros make use of printf, which the user must define. One
// place it is defined is sys/systm.h. We don't do it here due to
// symbol collisions.
//
#ifdef MYDEBUG
#  define DEBUG_BREAK() \
    DEBUG_PRINT_FUNCTION ( "[%s:%d] %s\tAt breakpoint\n", __FILE__, __LINE__, __FUNCTION__ ); \
    asm("int $3\n\t")
#else
#  define DEBUG_BREAK() ((void)0)
#endif

#ifdef MYDEBUG
#  define DEBUG_PRINT(...) \
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


#include "xenevent_netbsd.h"

#endif // xenevent_common_h
