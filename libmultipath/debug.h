void dlog (int sink, int prio, char * fmt, ...);

#if DAEMON

#include <pthread.h>
#include <stdarg.h>
#include "../multipathd/log_pthread.h"

int logsink;

#define condlog(prio, fmt, args...) \
	dlog(logsink, prio, fmt, ##args)

#else /* DAEMON */

#define condlog(prio, fmt, args...) \
	dlog(0, prio, fmt, ##args)

#endif /* DAEMON */
