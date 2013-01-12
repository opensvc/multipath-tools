#ifndef _LOG_PTHREAD_H
#define _LOG_PTHREAD_H

#include <pthread.h>

extern pthread_t log_thr;

extern pthread_mutex_t logq_lock;
extern pthread_mutex_t logev_lock;
extern pthread_cond_t logev_cond;

extern int logq_running;

void log_safe(int prio, const char * fmt, va_list ap);
void log_thread_start(pthread_attr_t *attr);
void log_thread_stop(void);
void log_thread_flush(void);

#endif /* _LOG_PTHREAD_H */
