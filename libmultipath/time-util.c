#include <assert.h>
#include <pthread.h>
#include <time.h>
#include "time-util.h"

void get_monotonic_time(struct timespec *res)
{
	struct timespec ts;
	int rv = clock_gettime(CLOCK_MONOTONIC, &ts);

	assert(rv == 0);
	*res = ts;
}

/* Initialize @cond as a condition variable that uses the monotonic clock */
void pthread_cond_init_mono(pthread_cond_t *cond)
{
	pthread_condattr_t attr;
	int res;

	res = pthread_condattr_init(&attr);
	assert(res == 0);
	res = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
	assert(res == 0);
	res = pthread_cond_init(cond, &attr);
	assert(res == 0);
	res = pthread_condattr_destroy(&attr);
	assert(res == 0);
}

/* Ensure that 0 <= ts->tv_nsec && ts->tv_nsec < 1000 * 1000 * 1000. */
void normalize_timespec(struct timespec *ts)
{
	while (ts->tv_nsec < 0) {
		ts->tv_nsec += 1000UL * 1000 * 1000;
		ts->tv_sec--;
	}
	while (ts->tv_nsec >= 1000UL * 1000 * 1000) {
		ts->tv_nsec -= 1000UL * 1000 * 1000;
		ts->tv_sec++;
	}
}

/* Compute *res = *a - *b */
void timespecsub(const struct timespec *a, const struct timespec *b,
		 struct timespec *res)
{
	res->tv_sec = a->tv_sec - b->tv_sec;
	res->tv_nsec = a->tv_nsec - b->tv_nsec;
	normalize_timespec(res);
}
