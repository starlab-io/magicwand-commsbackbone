/*************************************************************************
  * STAR LAB PROPRIETARY & CONFIDENTIAL
  * Copyright (C) 2016, Star Lab — All Rights Reserved
  * Unauthorized copying of this file, via any medium is strictly prohibited.
  ***************************************************************************/

/**
 * Provides support for netflow/metadata stream to Invincea analysis
 * engine(s).
 */

#ifndef mwcomms_netflow_h
#define mwcomms_netflow_h

#include <linux/in.h>

int
mw_netflow_init( IN struct sockaddr * LocalIp );


void
mw_netflow_fini( void );


bool
mw_netflow_consumer_exists( void );


int
mw_netflow_write_msg( const char * Fmt, ... );


int
mw_netflow_write_all( void * Message, size_t Len );


#endif //mwcomms_netflow_h
