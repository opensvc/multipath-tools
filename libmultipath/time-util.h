#ifndef _TIME_UTIL_H_
#define _TIME_UTIL_H_

#include <pthread.h>

struct timespec;

void pthread_cond_init_mono(pthread_cond_t *cond);
void normalize_timespec(struct timespec *ts);
void timespecsub(const struct timespec *a, const struct timespec *b,
		 struct timespec *res);

#endif /* _TIME_UTIL_H_ */
