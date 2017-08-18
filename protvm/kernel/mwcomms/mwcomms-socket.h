/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

#ifndef mwcomms_socket_h
#define mwcomms_socket_h

#include "mwcomms-common.h"

#include <linux/init.h>           
#include <linux/module.h>         
#include <linux/device.h>         
#include <linux/kernel.h>         
#include <linux/err.h>
#include <linux/fs.h>             
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/kthread.h>
#include <linux/moduleparam.h>
#include <asm/uaccess.h>          
#include <linux/time.h>

#include <linux/list.h>

#include <linux/netdevice.h>

#include <xen/grant_table.h>
#include <xen/page.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/interface/callback.h>
#include <xen/interface/io/ring.h>

#include <mwcomms-ioctls.h>
#include <message_types.h>
#include "mwcomms-common.h"
#include "mwcomms-xen-iface.h"


int
mwsocket_init( mw_region_t * SharedMem,
               IN struct sockaddr * LocalIp );


void
mwsocket_notify_ring_ready( void );


void
mwsocket_fini( void );


int
mwsocket_create( OUT mwsocket_t * SockFd,
                 IN  int          Domain,
                 IN  int          Type,
                 IN  int          Protocol );


mw_xen_event_handler_cb_t mwsocket_event_cb;


/**
 * @brief Returns whether the given file descriptor is backed by an MW socket.
 */
bool
mwsocket_verify( const struct file * File );


/**
 * @brief Closes socket by remote file descriptor.
 *
 * Does not close local descriptor. The using process will incur an
 * error and/or SIGPIPE upon the next usage. The local resources won't
 * be released until the local FD is closed locally, either by the
 * process or the kernel.
 */
int
mwsocket_close_by_remote_fd( IN mw_socket_fd_t RemoteFd,
                             IN bool           Wait );


/**
 * @brief Sends given signal to owner of given remote file descriptor.
 *
 * Finds owner of RemoteFd and signals it with SignalNum.
 */
int
mwsocket_signal_owner_by_remote_fd( IN mw_socket_fd_t RemoteFd,
                                    IN int            SignalNum );

/**
 * @brief Sends request to INS and puts response in given buffer.
 *
 * @param Response must hold the max size of the region it points to
 *        in its base field.
 */
int
mwsocket_send_bare_request( IN    mt_request_generic_t  * Request,
                            INOUT mt_response_generic_t * Response );


#endif // mwcomms_socket_h
