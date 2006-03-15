#ifndef _LOCK_H
#define _LOCK_H

#ifdef LCKDBG
#define lock(a) \
	        fprintf(stderr, "%s:%s(%i) lock %p\n", __FILE__, __FUNCTION__, __LINE__, a); \
        pthread_mutex_lock(a)
#define unlock(a) \
	        fprintf(stderr, "%s:%s(%i) unlock %p\n", __FILE__, __FUNCTION__, __LINE__, a); \
        pthread_mutex_unlock(a)
#define lock_cleanup_pop(a) \
	        fprintf(stderr, "%s:%s(%i) unlock %p\n", __FILE__, __FUNCTION__, __LINE__, a); \
        pthread_cleanup_pop(1);
#else
#define lock(a) pthread_mutex_lock(a)
#define unlock(a) pthread_mutex_unlock(a)
#define lock_cleanup_pop(a) pthread_cleanup_pop(1);
#endif

void cleanup_lock (void * data);

#endif /* _LOCK_H */
