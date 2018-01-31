/**************************************************************************
 * STAR LAB PROPRIETARY & CONFIDENTIAL
 * Copyright (C) 2018, Star Lab — All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited.
 **************************************************************************/

#ifndef xenevent_common_h
#define xenevent_common_h

//
// Decorators for function parameters
//
#define IN
#define OUT
#define INOUT

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

typedef uint16_t domid_t;
typedef uint32_t status_t;

// These need to be defined for the below macros to work
extern void bmk_printf(const char *, ...);
extern char *strrchr(const char *, int);

#define DEBUG_PRINT_FUNCTION bmk_printf

// Don't display the whole path - just show the last component (i.e. the filename)
#define SHORT_FILE strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__

//
// These macros make use of printf, which the user must define. One
// place it is defined is sys/systm.h. We don't do it here due to
// symbol collisions.
//
#ifdef MYDEBUG
#  define DEBUG_PREAMBLE() \
    bmk_printf ( "[%s:%d] %s\t", SHORT_FILE, __LINE__, __FUNCTION__);
#else
#  define DEBUG_PREAMBLE()  ((void)0)
#endif

#ifdef MYTRAP
#  define BARE_DEBUG_BREAK() asm("int $3")
#else
#  define BARE_DEBUG_BREAK() ((void)0)
#endif

#ifdef MYDEBUG
#  define DEBUG_BREAK()                                                 \
    DEBUG_PREAMBLE();                                                   \
    bmk_printf ( "At breakpoint\n" );                                   \
    asm("int $3\n\t") 
#else
#  define DEBUG_BREAK() ((void)0)
#endif

#ifdef MYDEBUG
#  define DEBUG_PRINT(...)                      \
    DEBUG_PREAMBLE();                           \
    bmk_printf(__VA_ARGS__)
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

#endif // xenevent_common_h
