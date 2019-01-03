#ifndef mwcomms_debugfs_h
#define mwcomms_debugfs_h

#include <message_types.h>

#define MW_DEBUGFS_TRACE_BUF_MAX 500

typedef struct _mt_count {
    atomic64_t invalid;
    atomic64_t create;
    atomic64_t shutdown; 
    atomic64_t close;
    atomic64_t connect;
    atomic64_t bind;
    atomic64_t listen;
    atomic64_t accept;
    atomic64_t send;
    atomic64_t recv;
    atomic64_t recvfrom;
    atomic64_t getname;
    atomic64_t getpeer;
    atomic64_t attrib;
    atomic64_t pollsetquery;
    atomic64_t unknown;
    atomic64_t total;
} mt_dbg_count_t;

typedef struct _mwsocket_trace_buffer {
    unsigned long index;    // buffer index
    pid_t pid;              // process ID for thread of execution
    int fops;               // file operation entry point (read(1), write(2), ioctl(3), poll(4), release(5) or none(0))
    int type;               // request type (Request->base.type)
    unsigned long t_mw1;    // nanosecond timestamp at fops entry (0 if internal cmd)
    unsigned long t_mw2;    // nanosecond timestamp at request creation
    unsigned long t_mw3;    // nanoseconds timestamp at attempt to get ring buffer slot
    unsigned long t_mw4;    // nanoseconds timestamp at request on ring buffer
    unsigned long t_mw5;    // nanoseconds timestamp at request off ring buffer
    unsigned long t_mw6;    // nanoseconds timestamp at request destruction
    unsigned long t_ins;    // nanoseconds elapsed time in ins
} mwsocket_trace_buffer_t;

extern mt_dbg_count_t g_mw_debugfs_req_count;
extern mt_dbg_count_t g_mw_debugfs_resp_count;

extern atomic64_t g_mw_trace_cur;
extern mwsocket_trace_buffer_t g_mw_trace_buf[];

void mw_debugfs_init( void );

void mw_debugfs_request_count( mt_request_generic_t * );

void mw_debugfs_response_count( mt_response_generic_t * );

void mw_debugfs_fini( void );

#endif //MWCOMMS_DEBUGFS_H
