/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2018, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

#ifndef mw_timer_h
#define mw_timer_h

#include <sys/time.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>


#define MW_TIMER_PRINT_FUNCTION fprintf
#define MW_TIMER_FLUSH_FUNCTION fflush
#ifdef DEBUG_FILE_STREAM
 #define MW_TIMER_FILE_STREAM DEBUG_FILE_STREAM
#else
 #define MW_TIMER_FILE_STREAM    stdout
#endif

#define MW_TIMER_INIT()                                         \
    struct timeval mw_start_time, mw_end_time, mw_res = {0};

#include <string.h>
#define MW_TIMER_SHORT_FILE strrchr(__FILE__, '/') ? strrchr(__FILE__, '/' ) + 1 : __FILE__

#define MW_TIMER_EMIT_META()                        \
    MW_TIMER_PRINT_FUNCTION( MW_TIMER_FILE_STREAM,  \
                             "%d [%s:%d] ",         \
                             getpid(),              \
                             MW_TIMER_SHORT_FILE,   \
                             __LINE__ );

#define MW_TIMER_START()                                                \
    gettimeofday( &mw_start_time, NULL );                               \
    MW_TIMER_PRINT_FUNCTION( MW_TIMER_FILE_STREAM,                      \
                             "Start time: %f \n",                       \
                             (double) ( mw_start_time.tv_sec +          \
                                        (mw_start_time.tv_usec/1000000.0 ) ) ); \
    MW_TIMER_FLUSH_FUNCTION( MW_TIMER_FILE_STREAM );

#define MW_TIMER_END( ... )                                             \
    gettimeofday( &mw_end_time, NULL );                                 \
    timersub( &mw_end_time, &mw_start_time, &mw_res );                  \
    MW_TIMER_PRINT_FUNCTION( MW_TIMER_FILE_STREAM, __VA_ARGS__ );       \
    MW_TIMER_PRINT_FUNCTION( MW_TIMER_FILE_STREAM,                      \
                             " elapsed: %f sec\n",                    \
                             (double) ( mw_res.tv_sec + (mw_res.tv_usec/1000000.0) ) ); \
    MW_TIMER_FLUSH_FUNCTION( MW_TIMER_FILE_STREAM );


#endif //mw_timer_h
