#ifndef mw_timer_h
#define mw_timer_h

#include <sys/time.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>


#define MW_TIMER_PRINT_FUNCTION fprintf
#define MW_TIMER_FLUSH_FUNCTION fflush
#define MW_TIMER_FILE_STREAM    stdout

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

#define MW_START_TIMER()                                \
    gettimeofday( &mw_start_time, NULL );               \
    MW_TIMER_FLUSH_FUNCTION( MW_TIMER_FILE_STREAM );

#define MW_END_TIMER( x )                                               \
    gettimeofday( &mw_end_time, NULL );                                 \
    timersub( &mw_end_time, &mw_start_time, &mw_res );                  \
    MW_TIMER_PRINT_FUNCTION( MW_TIMER_FILE_STREAM,                      \
                             "%s elapsed: %f sec\n",                    \
                             x, (double) ( mw_res.tv_sec + (mw_res.tv_usec/1000000.0) ) );


#endif //mw_timer_h
