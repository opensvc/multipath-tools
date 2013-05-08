#ifndef _LOCK_H
#define _LOCK_H

#include <signal.h>

/*
 * Wrapper for the mutex. Includes a ref-count to keep
 * track of how many there are out-standing threads blocking
 * on a mutex. */
struct mutex_lock {
	pthread_mutex_t *mutex;
	int depth;
};

#ifdef LCKDBG
#define lock(a) \
		fprintf(stderr, "%s:%s(%i) lock %p depth: %d (%ld)\n", __FILE__, __FUNCTION__, __LINE__, a.mutex, a.depth, pthread_self()); \
		a.depth++; pthread_mutex_lock(a.mutex)
#define unlock(a) \
		fprintf(stderr, "%s:%s(%i) unlock %p depth: %d (%ld)\n", __FILE__, __FUNCTION__, __LINE__, a.mutex, a.depth, pthread_self()); \
	a.depth--; pthread_mutex_unlock(a.mutex)
#define lock_cleanup_pop(a) \
		fprintf(stderr, "%s:%s(%i) unlock %p depth: %d (%ld)\n", __FILE__, __FUNCTION__, __LINE__, a.mutex, a.depth, pthread_self()); \
	pthread_cleanup_pop(1);
#else
#define lock(a) a.depth++; pthread_mutex_lock(a.mutex)
#define unlock(a) a.depth--; pthread_mutex_unlock(a.mutex)
#define lock_cleanup_pop(a) pthread_cleanup_pop(1);
#endif

void cleanup_lock (void * data);

#endif /* _LOCK_H */
