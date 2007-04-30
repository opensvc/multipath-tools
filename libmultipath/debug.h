void dlog (int sink, int prio, const char * fmt, ...)
	__attribute__((format(printf, 3, 4)));

#if DAEMON

#include <pthread.h>
#include <stdarg.h>

#include "log_pthread.h"

int logsink;

#define condlog(prio, fmt, args...) \
	dlog(logsink, prio, fmt "\n", ##args)

#else /* DAEMON */

#define condlog(prio, fmt, args...) \
	dlog(0, prio, fmt "\n", ##args)

#endif /* DAEMON */
