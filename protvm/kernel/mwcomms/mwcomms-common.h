/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

#ifndef mwcomms_common_h
#define mwcomms_common_h

#define DRIVER_NAME "mwcomms"
#define pr_fmt(fmt)                                     \
    DRIVER_NAME "P%d (%s) " fmt, current->pid, __func__

#include <linux/kernel.h>         

typedef struct _mw_region
{
    void * ptr;
    size_t pagect;
} mw_region_t;


#define    IN
#define   OUT
#define INOUT

// Disables optimization on a per-function basis.
#define MWSOCKET_DEBUG_ATTRIB  __attribute__((optimize("O0")))


//
// General helper macros
//
#ifndef NUMBER_OF
#  define NUMBER_OF(x) (sizeof(x)/sizeof(x[0]))
#endif

#ifndef bzero
#  define bzero(p, sz) memset( (p), 0, (sz) )
#endif // bzero

#ifndef CHECK_FREE
#  define CHECK_FREE(_p) { if(_p) { kfree(_p); } }
#endif

#define _DEBUG_EMIT_BREAKPOINT()                \
    asm("int $3")

#ifdef MYTRAP
#  define DEBUG_EMIT_BREAKPOINT() _DEBUG_EMIT_BREAKPOINT()
#else
#  define DEBUG_EMIT_BREAKPOINT() ((void)0)
#endif

#define DEBUG_BREAK() DEBUG_EMIT_BREAKPOINT()


// dump_stack() might be of use here
#define MYASSERT(x)                                                     \
    do { if (x) break;                                                  \
        pr_emerg( "### ASSERTION FAILED %s: %s: %d: %s\n",              \
                  __FILE__, __func__, __LINE__, #x);                    \
        dump_stack();                                                   \
        DEBUG_BREAK();                                                  \
    } while (0)


// Define pr_verbose()
#ifdef MYVERBOSE
#  define pr_verbose(...)   pr_debug(__VA_ARGS__)
#else
#  define pr_verbose(...)   ((void)0)
#endif

#endif // mwcomms_common_h
