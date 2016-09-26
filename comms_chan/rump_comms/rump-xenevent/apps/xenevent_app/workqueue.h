#ifndef work_queue_h
#define work_queue_h

//
// A work queue is simply an array of indices into the buffer
// array. The memory for the underlying array is provided by
// the caller.
//


#include <stdint.h>
#include <stdbool.h>


//
// This type holds one buffer index
//

typedef uint16_t work_queue_buffer_idx_t;

// Invalid item -- invalid index.
#define INVALID_WORK_QUEUE_IDX ((work_queue_buffer_idx_t)-2)

struct _workqueue;
typedef struct _workqueue workqueue_t;

//workqueue_t *
//workqueue_alloc( void * buffer, size_t byte_len );

workqueue_t *
workqueue_alloc( size_t item_count );

void
workqueue_free( workqueue_t * wq );

int
workqueue_enqueue( workqueue_t * wq, work_queue_buffer_idx_t item );

work_queue_buffer_idx_t
workqueue_dequeue( workqueue_t * wq );

bool
workqueue_is_empty( workqueue_t * wq );

#endif // work_queue_h
