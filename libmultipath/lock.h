#ifndef _LOCK_H
#define _LOCK_H

#include <pthread.h>
#include <urcu/uatomic.h>
#include <stdbool.h>

typedef void (wakeup_fn)(void);

struct mutex_lock {
	pthread_mutex_t mutex;
	wakeup_fn *wakeup;
	int waiters; /* uatomic access only */
};

static inline void init_lock(struct mutex_lock *a)
{
	pthread_mutex_init(&a->mutex, NULL);
	uatomic_set(&a->waiters, 0);
}

#if defined(__GNUC__) && __GNUC__ == 12 && URCU_VERSION < 0xe00
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

static inline int uatomic_xchg_int(int *ptr, int val)
{
	return uatomic_xchg(ptr, val);
}

static inline void lock(struct mutex_lock *a)
{
	uatomic_inc(&a->waiters);
	pthread_mutex_lock(&a->mutex);
	uatomic_dec(&a->waiters);
}

#if defined(__GNUC__) && __GNUC__ == 12 && URCU_VERSION < 0xe00
#pragma GCC diagnostic pop
#endif

static inline int trylock(struct mutex_lock *a)
{
	return pthread_mutex_trylock(&a->mutex);
}

static inline int timedlock(struct mutex_lock *a, struct timespec *tmo)
{
	return pthread_mutex_timedlock(&a->mutex, tmo);
}

static inline void __unlock(struct mutex_lock *a)
{
	pthread_mutex_unlock(&a->mutex);
}

static inline bool lock_has_waiters(struct mutex_lock *a)
{
	return (uatomic_read(&a->waiters) > 0);
}

#define lock_cleanup_pop(a) pthread_cleanup_pop(1)

void cleanup_lock (void * data);
void set_wakeup_fn(struct mutex_lock *lock, wakeup_fn *fn);

#endif /* _LOCK_H */
