/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2018, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

#ifndef config_h
#define config_h

//
// Max number of threads. This is the same as the max number of
// concurrent connections we can handle. Under normal circumstances,
// most of them will be in a wait state most of the time - so a large
// number should be OK. However, Rump seems to have an internal limit
// of 100, although we have problems when we exceed 62 worker threads
// (plus some extra that do other things). Exceeding the max causes
// open to fail with EMFILE errno.
//
// Must be >= 2 to function.  Values < 25 result in significant
// performance degradation.

#define MAX_THREAD_COUNT 500

//
// Number of buffer items. This is the number of requests (from the
// protected VM) that we can queue. Accounts for each thread blocking
// plus one buffer item to handle poll requests
//
#define BUFFER_ITEM_COUNT ( MAX_THREAD_COUNT + 50 )

#define WORK_QUEUE_ITEM_COUNT BUFFER_ITEM_COUNT

//This constant is for defer accept functionality, this will determine
//how many seconds an idle connection will wait for data before disconnecting
#define DEFER_ACCEPT_MAX_IDLE 10.0

//
// Xen event device
//

#define XENEVENT_DEVICE  "/dev/xe"

#endif // config_h
