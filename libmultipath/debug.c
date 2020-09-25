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

int logsink;
int libmp_verbosity = DEFAULT_VERBOSITY;

void dlog(int prio, const char * fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (logsink != LOGSINK_SYSLOG) {
		if (logsink == LOGSINK_STDERR_WITH_TIME) {
			time_t t = time(NULL);
			struct tm *tb = localtime(&t);
			char buff[16];

			strftime(buff, sizeof(buff),
				 "%b %d %H:%M:%S", tb);
			buff[sizeof(buff)-1] = '\0';
			fprintf(stderr, "%s | ", buff);
		}
		vfprintf(stderr, fmt, ap);
	}
	else
		log_safe(prio + 3, fmt, ap);
	va_end(ap);
}
