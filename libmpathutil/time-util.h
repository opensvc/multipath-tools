#ifndef TIME_UTIL_H_INCLUDED
#define TIME_UTIL_H_INCLUDED

#include <pthread.h>

struct timespec;

void get_monotonic_time(struct timespec *res);
void pthread_cond_init_mono(pthread_cond_t *cond);
void normalize_timespec(struct timespec *ts);
void timespecsub(const struct timespec *a, const struct timespec *b,
		 struct timespec *res);
int timespeccmp(const struct timespec *a, const struct timespec *b);

#endif /* TIME_UTIL_H_INCLUDED */
