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

// Hard max: 62
//#define MAX_THREAD_COUNT 62   // for production
#define MAX_THREAD_COUNT 24  // for testing

//
// Number of buffer items. This is the number of requests (from the
// protected VM) that we can queue.
//
#define BUFFER_ITEM_COUNT 8

#define WORK_QUEUE_ITEM_COUNT BUFFER_ITEM_COUNT


//
// Xen event device
//

#define XENEVENT_DEVICE  "/dev/xe"

#endif // config_h
