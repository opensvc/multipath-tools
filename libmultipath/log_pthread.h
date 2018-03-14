#ifndef _LOG_PTHREAD_H
#define _LOG_PTHREAD_H

#include <pthread.h>

void log_safe(int prio, const char * fmt, va_list ap);
void log_thread_start(pthread_attr_t *attr);
void log_thread_reset (void);
void log_thread_stop(void);

#endif /* _LOG_PTHREAD_H */
