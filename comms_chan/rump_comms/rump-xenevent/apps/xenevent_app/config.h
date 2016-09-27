#ifndef config_h
#define config_h

//
// Max number of threads. This is the same as the max number of
// concurrent connections we can handle. Under normal circumstances,
// most of them will be in a wait state most of the time - so a large
// number should be OK. However, Rump seems to have an internal limit
// of 100.
//
//#define MAX_THREAD_COUNT 100

#define MAX_THREAD_COUNT 4

//
// Number of buffer items. This is the number of requests (from the
// protected VM) that we can queue.
//
#define BUFFER_ITEM_COUNT 4

#define WORK_QUEUE_ITEM_COUNT BUFFER_ITEM_COUNT

//
// Network config - hardcoded for now
//

#define XEN_HOST_ADDR "10.190.2.100"
#define XEN_HOST_PORT 5555
#define TEST_STRING "Hello from Rump unikernel!\n"

//
// Xen event device
//

#define XENEVENT_DEVICE  "/dev/xe"

#endif // config_h
