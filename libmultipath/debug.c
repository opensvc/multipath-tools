/*
 * Copyright (c) 2005 Christophe Varoqui
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "log_pthread.h"
#include <sys/types.h>
#include <time.h>

#include "vector.h"
#include "config.h"

void dlog (int sink, int prio, const char * fmt, ...)
{
	va_list ap;
	int thres;

	va_start(ap, fmt);
	thres = (conf) ? conf->verbosity : 0;

	if (prio <= thres) {
		if (sink < 1) {
			if (sink == 0) {
				time_t t = time(NULL);
				struct tm *tb = localtime(&t);
				char buff[16];

				strftime(buff, sizeof(buff),
					 "%b %d %H:%M:%S", tb);
				buff[sizeof(buff)-1] = '\0';

				fprintf(stdout, "%s | ", buff);
			}
			vfprintf(stdout, fmt, ap);
		}
		else
			log_safe(prio + 3, fmt, ap);
	}
	va_end(ap);
}
