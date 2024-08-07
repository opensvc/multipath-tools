#ifndef LOG_PTHREAD_H_INCLUDED
#define LOG_PTHREAD_H_INCLUDED

#include <pthread.h>

void log_safe(int prio, const char * fmt, va_list ap)
	__attribute__((format(printf, 2, 0)));
void log_thread_start(pthread_attr_t *attr);
void log_thread_reset (void);
void log_thread_stop(void);

#endif /* LOG_PTHREAD_H_INCLUDED */
