#include <pthread.h>
#include <unistd.h>
#include "init_unwinder.h"

static pthread_mutex_t dummy_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t dummy_cond = PTHREAD_COND_INITIALIZER;
static int dummy_started;

static void *dummy_thread(void *arg __attribute__((unused)))
{
	pthread_mutex_lock(&dummy_mtx);
	dummy_started = 1;
	pthread_cond_broadcast(&dummy_cond);
	pthread_mutex_unlock(&dummy_mtx);
	pause();
	return NULL;
}

int init_unwinder(void)
{
	pthread_t dummy;
	int rc;

	pthread_mutex_lock(&dummy_mtx);

	rc = pthread_create(&dummy, NULL, dummy_thread, NULL);
	if (rc != 0) {
		pthread_mutex_unlock(&dummy_mtx);
		return rc;
	}

	while (!dummy_started)
		pthread_cond_wait(&dummy_cond, &dummy_mtx);

	pthread_mutex_unlock(&dummy_mtx);

	rc = pthread_cancel(dummy);
	pthread_join(dummy, NULL);
	return rc;
}
