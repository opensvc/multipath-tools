#ifndef DEBUG_H_INCLUDED
#define DEBUG_H_INCLUDED
void dlog (int prio, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));


#include <pthread.h>
#include <stdarg.h>

#include "log_pthread.h"

extern int logsink;
extern int libmp_verbosity;

#ifndef MAX_VERBOSITY
#define MAX_VERBOSITY 4
#endif

enum {
	LOGSINK_STDERR_WITH_TIME = 0,
	LOGSINK_STDERR_WITHOUT_TIME = -1,
	LOGSINK_SYSLOG = 1,
};

#define condlog(prio, fmt, args...)					\
	do {								\
		int __p = (prio);					\
									\
		if (__p <= MAX_VERBOSITY && __p <= libmp_verbosity)	\
			dlog(__p, fmt "\n", ##args);			\
	} while (0)
#endif /* DEBUG_H_INCLUDED */
