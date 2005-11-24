/*
 * Copyright (c) 2005 Christophe Varoqui
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#if DAEMON
#include "log_pthread.h"
#include <sys/types.h>
#include <time.h>
#endif

#include "config.h"

void dlog (int sink, int prio, char * fmt, ...)
{
	va_list ap;
	int thres;

	va_start(ap, fmt);
	thres = (conf) ? conf->verbosity : 0;

	if (prio <= thres) {
#if DAEMON
		if (!sink) {
			time_t t = time(NULL);
			struct tm *tb = localtime(&t);
			char buff[16];
			
			strftime(buff, 16, "%b %d %H:%M:%S", tb); 

			fprintf(stdout, "%s | ", buff);
			vfprintf(stdout, fmt, ap);
			fprintf(stdout, "\n");
		}
		else
			log_safe(prio + 3, fmt, ap);
#else
		vfprintf(stdout, fmt, ap);
		fprintf(stdout, "\n");
#endif
	}
	va_end(ap);
}
