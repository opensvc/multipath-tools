#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "prio.h"

int getprio(__attribute__((unused)) struct path *pp,
	    __attribute__((unused)) char *args,
	    __attribute__((unused)) unsigned int timeout)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	srand((unsigned int)tv.tv_usec);
	return 1+(int) (10.0*rand()/(RAND_MAX+1.0));
}
