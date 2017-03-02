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

#include <xen/grant_table.h>
#include <xen/page.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/interface/callback.h>
#include <xen/interface/io/ring.h>

#include <mwcomms-ioctls.h>
#include "mwcomms-xen-iface.h"

int
mw_socket_init( mw_region_t * SharedMem,
                struct completion * RingShared );

void
mw_socket_fini( void );


int
mw_socket_create( OUT mwsocket_t * SockFd,
                  IN  int          Domain,
                  IN  int          Type,
                  IN  int          Protocol );


mw_xen_event_handler_cb_t mw_socket_event_cb;


// @brief Returns whether the given file descriptor is backed by an MW socket.
bool
mw_socket_verify( const struct file * File );

#endif // mwcomms_socket_h
