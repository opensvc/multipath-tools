#ifndef _LOCK_H
#define _LOCK_H

#include <pthread.h>

/*
 * Wrapper for the mutex. Includes a ref-count to keep
 * track of how many there are out-standing threads blocking
 * on a mutex. */
struct mutex_lock {
	pthread_mutex_t *mutex;
	int depth;
};

#define lock(a) a.depth++; pthread_mutex_lock(a.mutex)
#define unlock(a) a.depth--; pthread_mutex_unlock(a.mutex)
#define lock_cleanup_pop(a) pthread_cleanup_pop(1)

void cleanup_lock (void * data);

#endif /* _LOCK_H */
