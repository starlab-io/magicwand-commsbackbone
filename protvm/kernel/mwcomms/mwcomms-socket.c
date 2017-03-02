/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

//
// Implementation of MW sockets. An MW socket is backed by a
// registered file object and file descriptor with it's own file
// operations.
//

#include "mwcomms-common.h"

#include "mwcomms-socket.h"

#include <linux/slab.h>

#include <message_types.h>
#include <xen_keystore_defs.h>

// Defines:
// mwevent_sring_t
// mwevent_front_ring_t
// mwevent_back_ring_t
DEFINE_RING_TYPES( mwevent, mt_request_generic_t, mt_response_generic_t );



/******************************************************************************
 * Interface to MW socket files.
 ******************************************************************************/

static int
mwsocket_release( struct inode *Inode,
                  struct file * File );

static ssize_t
mwsocket_read( struct file * File,
               uint8_t     * Bytes,
               size_t        Len,
               loff_t      * Offset );

static ssize_t
mwsocket_write( struct file * File,
                uint8_t     * Bytes,
                size_t        Len,
                loff_t      * Offset );

static ssize_t
mwsocket_poll( struct file * File,
               struct poll_table_struct * PollTbl );


static struct file_operations
mwsocket_fops =
{
    .release = mwsocket_release,
    .read    = mwsocket_read,
    .write   = mwsocket_write,
    .poll    = mwsocket_poll
};


/******************************************************************************
 * Types and globals
 ******************************************************************************/
struct mwsocket_thread_response;

typedef struct _mwsocket_outstanding_request
{
    mt_id_t message_id;
    struct mwsocket_thread_response * thread_response;
    bool   blocked_on_response;

    struct list_head list_all;
    struct list_head list_by_thread;

} mwsocket_outstanding_request_t;


typedef struct _mwsocket_thread_response
{
    // 
} mwsocket_thread_response_t;



typedef struct _mwcomms_base_globals {
    // Used to signal socket subsystem threads
    struct completion ring_ready;
    bool             is_ring_ready;

    // Fast allocators (SLAB)
//    struct kmem_cache * open_socket_slab;
//    struct kmem_cache * threadresp_slab;

    struct kmem_cache * outstanding_request_cache;
    struct kmem_cache * thread_response_cache;
    
} mwcomms_socket_globals_t;


mwcomms_socket_globals_t g_mwsocket_state;


// Callback for arrival of event on Xen event channel
void
mw_socket_event_cb ( void )
{
    return;
}


int
mw_socket_init( mw_region_t * SharedMem,
                struct completion * RingShared )
{
    // Do everything we can, then wait for the ring to be ready
    int rc = 0;

    bzero( &g_mwsocket_state, sizeof(g_mwsocket_state) );



    
    wait_for_completion( RingShared );
    g_mwsocket_state.ring_ready = true;

ErrorExit:
    return rc;
}

void
mw_socket_fini( void )
{

}


int
mw_socket_create( OUT mwsocket_t * SockFd,
                  IN  int          Domain,
                  IN  int          Type,
                  IN  int          Protocol )
{
    int rc = 0;
    int fd = -1;

    

}


// @brief Returns whether the given file descriptor is backed by an MW socket.
bool
mw_socket_verify( const struct file * File )
{
    return (File->f_op == &mwsocket_fops);
}



static int
mwsocket_release( struct inode *Inode,
                  struct file * File )
{

}

static ssize_t
mwsocket_read( struct file * File,
               uint8_t     * Bytes,
               size_t        Len,
               loff_t      * Offset )
{


}


static ssize_t
mwsocket_write( struct file * File,
                uint8_t     * Bytes,
                size_t        Len,
                loff_t      * Offset )
{


}

static ssize_t
mwsocket_poll( struct file * File,
               struct poll_table_struct * PollTbl )
{


}

