#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#if DAEMON
#include "../multipathd/log_pthread.h"
#endif

#include "config.h"

void dlog (int sink, int prio, char * fmt, ...)
{
	va_list ap;
	int thres;

	va_start(ap, fmt);

	if (!sink) {
		if (!conf)
			thres = 0;
		else
			thres = conf->verbosity;

		if (prio <= thres) {
			vfprintf(stdout, fmt, ap);
			fprintf(stdout, "\n");
		}
	}
#if DAEMON
	else {
		log_safe(prio + 3, fmt, ap);
	}
#endif
	va_end(ap);
}
