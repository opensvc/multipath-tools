/*
 * Copyright (c) 2005 Christophe Varoqui
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "log_pthread.h"
#include <sys/types.h>
#include <time.h>
#include "../third-party/valgrind/drd.h"
#include "vector.h"
#include "config.h"
#include "defaults.h"
#include "debug.h"
#include "time-util.h"
#include "util.h"

int logsink;
int libmp_verbosity = DEFAULT_VERBOSITY;

void dlog(int prio, const char * fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (logsink != LOGSINK_SYSLOG) {
		if (logsink == LOGSINK_STDERR_WITH_TIME) {
			struct timespec ts;
			char buff[32];

			get_monotonic_time(&ts);
			safe_sprintf(buff, "%ld.%06ld",
				     (long)ts.tv_sec,
				     ts.tv_nsec/1000);
			fprintf(stderr, "%s | ", buff);
		}
		vfprintf(stderr, fmt, ap);
	}
	else
		log_safe(prio + 3, fmt, ap);
	va_end(ap);
}
