#ifndef _MINIOS_SEMAPHORE_H_
#define _MINIOS_SEMAPHORE_H_

#include <mini-os/wait.h>
#include <mini-os/spinlock.h>

/*
 * Implementation of semaphore in Mini-os is simple, because 
 * there are no preemptive threads, the atomicity is guaranteed.
 */

struct semaphore
{
	int count;
	struct wait_queue_head wait;
};

/*
 * the semaphore definition
 */
struct rw_semaphore {
	signed long		count;
	spinlock_t		wait_lock;
	int			debug;
};

#define __SEMAPHORE_INITIALIZER(name, n)                            \
{                                                                   \
    .count    = n,                                                  \
    .wait           = __WAIT_QUEUE_HEAD_INITIALIZER((name).wait)    \
}

#define __MUTEX_INITIALIZER(name) \
    __SEMAPHORE_INITIALIZER(name,1)
                           
#define __DECLARE_SEMAPHORE_GENERIC(name,count) \
    struct semaphore name = __SEMAPHORE_INITIALIZER(name,count)
    
#define DECLARE_MUTEX(name) __DECLARE_SEMAPHORE_GENERIC(name,1)

#define DECLARE_MUTEX_LOCKED(name) __DECLARE_SEMAPHORE_GENERIC(name,0)

static inline void init_SEMAPHORE(struct semaphore *sem, int count)
{
  sem->count = count;
  minios_init_waitqueue_head(&sem->wait);
}

#define init_MUTEX(sem) init_SEMAPHORE(sem, 1)

static inline int trydown(struct semaphore *sem)
{
    unsigned long lflags;
    int ret = 0;
    local_irq_save(lflags);
    if (sem->count > 0) {
        ret = 1;
        sem->count--;
    }
    local_irq_restore(lflags);
    return ret;
}

static inline void down(struct semaphore *sem)
{
    unsigned long lflags;
    while (1) {
        minios_wait_event(sem->wait, sem->count > 0);
        local_irq_save(lflags);
        if (sem->count > 0)
            break;
        local_irq_restore(lflags);
    }
    sem->count--;
    local_irq_restore(lflags);
}

static inline void up(struct semaphore *sem)
{
    unsigned long lflags;
    local_irq_save(lflags);
    sem->count++;
    minios_wake_up(&sem->wait);
    local_irq_restore(lflags);
}

/* FIXME! Thre read/write semaphores are unimplemented! */
static inline void init_rwsem(struct rw_semaphore *sem)
{
  sem->count = 1;
}

static inline void down_read(struct rw_semaphore *sem)
{
}


static inline void up_read(struct rw_semaphore *sem)
{
}

static inline void up_write(struct rw_semaphore *sem)
{
}

static inline void down_write(struct rw_semaphore *sem)
{
}

#endif /* _MINIOS_SEMAPHORE_H */
