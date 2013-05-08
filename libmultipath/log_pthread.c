/*
 * Copyright (c) 2005 Christophe Varoqui
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <syslog.h>
#include <pthread.h>
#include <sys/mman.h>

#include <memory.h>

#include "log_pthread.h"
#include "log.h"
#include "lock.h"

pthread_t log_thr;

pthread_mutex_t logq_lock;
pthread_mutex_t logev_lock;
pthread_cond_t logev_cond;

int logq_running;

void log_safe (int prio, const char * fmt, va_list ap)
{
	if (log_thr == (pthread_t)0) {
		syslog(prio, fmt, ap);
		return;
	}

	pthread_mutex_lock(&logq_lock);
	log_enqueue(prio, fmt, ap);
	pthread_mutex_unlock(&logq_lock);

	pthread_mutex_lock(&logev_lock);
	pthread_cond_signal(&logev_cond);
	pthread_mutex_unlock(&logev_lock);
}

void log_thread_flush (void)
{
	int empty;

	do {
		pthread_mutex_lock(&logq_lock);
		empty = log_dequeue(la->buff);
		pthread_mutex_unlock(&logq_lock);
		if (!empty)
			log_syslog(la->buff);
	} while (empty == 0);
}

static void flush_logqueue (void)
{
	int empty;

	do {
		pthread_mutex_lock(&logq_lock);
		empty = log_dequeue(la->buff);
		pthread_mutex_unlock(&logq_lock);
		if (!empty)
			log_syslog(la->buff);
	} while (empty == 0);
}

static void * log_thread (void * et)
{
	int running;

	pthread_mutex_lock(&logev_lock);
	logq_running = 1;
	pthread_mutex_unlock(&logev_lock);

	mlockall(MCL_CURRENT | MCL_FUTURE);
	logdbg(stderr,"enter log_thread\n");

	while (1) {
		pthread_mutex_lock(&logev_lock);
		pthread_cond_wait(&logev_cond, &logev_lock);
		running = logq_running;
		pthread_mutex_unlock(&logev_lock);
		if (!running)
			break;
		log_thread_flush();
	}
	return NULL;
}

void log_thread_start (pthread_attr_t *attr)
{
	logdbg(stderr,"enter log_thread_start\n");

	pthread_mutex_init(&logq_lock, NULL);
	pthread_mutex_init(&logev_lock, NULL);
	pthread_cond_init(&logev_cond, NULL);

	if (log_init("multipathd", 0)) {
		fprintf(stderr,"can't initialize log buffer\n");
		exit(1);
	}
	if (pthread_create(&log_thr, attr, log_thread, NULL)) {
		fprintf(stderr,"can't start log thread\n");
		exit(1);
	}

	return;
}

void log_thread_stop (void)
{
	logdbg(stderr,"enter log_thread_stop\n");

	pthread_mutex_lock(&logev_lock);
	logq_running = 0;
	pthread_cond_signal(&logev_cond);
	pthread_mutex_unlock(&logev_lock);

	pthread_mutex_lock(&logq_lock);
	pthread_cancel(log_thr);
	pthread_mutex_unlock(&logq_lock);
	pthread_join(log_thr, NULL);
	log_thr = (pthread_t)0;

	flush_logqueue();

	pthread_mutex_destroy(&logq_lock);
	pthread_mutex_destroy(&logev_lock);
	pthread_cond_destroy(&logev_cond);

	log_close();
}

