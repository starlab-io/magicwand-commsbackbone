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
// This type holds one buffer index. Max index is 0x7fff.
//

typedef int16_t work_queue_buffer_idx_t;

//
// Indicates a slot in the work queue what is unassigned.
//
#define WORK_QUEUE_UNASSIGNED_IDX ((work_queue_buffer_idx_t)-1)

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

int
workqueue_get_contents( workqueue_t * wq,
                        work_queue_buffer_idx_t * items,
                        size_t size );

#endif // work_queue_h
