#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include <sys/mman.h>

#include <memory.h>

#include "log_pthread.h"
#include "log.h"

void log_safe (int prio, char * fmt, va_list ap)
{
	pthread_mutex_lock(logq_lock);
	//va_start(ap, fmt);
	log_enqueue(prio, fmt, ap);
	va_end(ap);
	pthread_mutex_unlock(logq_lock);

	pthread_mutex_lock(logev_lock);
	pthread_cond_signal(logev_cond);
	pthread_mutex_unlock(logev_lock);
}

static void flush_logqueue (void)
{
	int empty;

	do {
		pthread_mutex_lock(logq_lock);
		empty = log_dequeue(la->buff);
		pthread_mutex_unlock(logq_lock);
		log_syslog(la->buff);
	} while (empty == 0);
}

static void * log_thread (void * et)
{
	mlockall(MCL_CURRENT | MCL_FUTURE);
	logdbg(stderr,"enter log_thread\n");

	while (1) {
		pthread_mutex_lock(logev_lock);
		pthread_cond_wait(logev_cond, logev_lock);
		pthread_mutex_unlock(logev_lock);

		flush_logqueue();
	}
}

void log_thread_start (void)
{
	pthread_attr_t attr;
	
	logdbg(stderr,"enter log_thread_start\n");

	logq_lock = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
	logev_lock = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
	logev_cond = (pthread_cond_t *) malloc(sizeof(pthread_cond_t));
	
	pthread_mutex_init(logq_lock, NULL);
	pthread_mutex_init(logev_lock, NULL);
	pthread_cond_init(logev_cond, NULL);
	
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 64 * 1024);

	if (log_init("multipathd", 0)) {
		fprintf(stderr,"can't initialize log buffer\n");
		exit(1);
	}
	pthread_create(&log_thr, &attr, log_thread, NULL);

	return;
}

void log_thread_stop (void)
{
	logdbg(stderr,"enter log_thread_stop\n");

	pthread_mutex_lock(logq_lock);
	pthread_cancel(log_thr);
	pthread_mutex_unlock(logq_lock);

	flush_logqueue();

	pthread_mutex_destroy(logq_lock);
	pthread_mutex_destroy(logev_lock);
	pthread_cond_destroy(logev_cond);

	free_logarea();
}	
