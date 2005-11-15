/*
 * Copyright (c) 2005 Christophe Varoqui
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#if DAEMON
#include "log_pthread.h"
#endif

#include "config.h"

void dlog (int sink, int prio, char * fmt, ...)
{
	va_list ap;
	int thres;

	va_start(ap, fmt);
	thres = (conf) ? conf->verbosity : 0;

	if (prio <= thres) {
		if (!sink) {
			vfprintf(stdout, fmt, ap);
			fprintf(stdout, "\n");
		}
#if DAEMON
		else
			log_safe(prio + 3, fmt, ap);
#endif
	}
	va_end(ap);
}
