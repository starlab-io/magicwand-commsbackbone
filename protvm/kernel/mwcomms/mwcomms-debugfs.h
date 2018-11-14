#ifndef mwcomms_debugfs_h
#define mwcomms_debugfs_h

#include <message_types.h>

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

extern mt_dbg_count_t g_mw_debugfs_req_count;
extern mt_dbg_count_t g_mw_debugfs_resp_count;

void mw_debugfs_init( void );

void mw_debugfs_request_count( mt_request_generic_t * );

void mw_debugfs_response_count( mt_response_generic_t * );

void mw_debugfs_fini( void );

#endif //MWCOMMS_DEBUGFS_H
