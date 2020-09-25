void dlog (int sink, int prio, const char * fmt, ...)
	__attribute__((format(printf, 3, 4)));


#include <pthread.h>
#include <stdarg.h>

#include "log_pthread.h"

extern int logsink;
extern int libmp_verbosity;

#define condlog(prio, fmt, args...) \
	dlog(logsink, prio, fmt "\n", ##args)
