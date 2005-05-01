#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "config.h"

void condlog (int prio, char * fmt, ...)
{
	va_list ap;
	int thres;

	if (!conf)
		thres = 0;
	else
		thres = conf->verbosity;

	va_start(ap, fmt);

	if (prio <= thres) {
		vfprintf(stdout, fmt, ap);
		fprintf(stdout, "\n");
	}
	va_end(ap);
}
