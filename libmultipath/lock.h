#ifndef _LOCK_H
#define _LOCK_H

#include <pthread.h>

typedef void (wakeup_fn)(void);

struct mutex_lock {
	pthread_mutex_t mutex;
	wakeup_fn *wakeup;
};

static inline void lock(struct mutex_lock *a)
{
	pthread_mutex_lock(&a->mutex);
}

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

#define lock_cleanup_pop(a) pthread_cleanup_pop(1)

void cleanup_lock (void * data);
void set_wakeup_fn(struct mutex_lock *lock, wakeup_fn *fn);

#endif /* _LOCK_H */
