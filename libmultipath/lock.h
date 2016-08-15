#ifndef _LOCK_H
#define _LOCK_H

#include <pthread.h>

struct mutex_lock {
	pthread_mutex_t mutex;
};

static inline void lock(struct mutex_lock *a)
{
	pthread_mutex_lock(&a->mutex);
}

static inline int timedlock(struct mutex_lock *a, struct timespec *tmo)
{
	return pthread_mutex_timedlock(&a->mutex, tmo);
}

static inline void unlock(struct mutex_lock *a)
{
	pthread_mutex_unlock(&a->mutex);
}

#define lock_cleanup_pop(a) pthread_cleanup_pop(1)

void cleanup_lock (void * data);

#endif /* _LOCK_H */
