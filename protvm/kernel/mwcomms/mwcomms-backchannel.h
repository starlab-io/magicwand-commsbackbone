/*************************************************************************
  * STAR LAB PROPRIETARY & CONFIDENTIAL
  * Copyright (C) 2016, Star Lab — All Rights Reserved
  * Unauthorized copying of this file, via any medium is strictly prohibited.
  ***************************************************************************/

/**
 * Provides support for netflow/metadata stream to Invincea analysis
 * engine(s).
 */

#ifndef mwcomms_backchannel_h
#define mwcomms_backchannel_h

#include <linux/in.h>

int
mw_backchannel_init( IN struct sockaddr * LocalIp );


void
mw_backchannel_fini( void );


bool
mw_backchannel_consumer_exists( void );


int
mw_backchannel_write_msg( const char * Fmt, ... );


int
mw_backchannel_write( void * Message, size_t Len );


#endif //mwcomms_backchannel_h
