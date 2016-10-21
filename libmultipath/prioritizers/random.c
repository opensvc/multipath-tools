#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "prio.h"

int getprio(struct path * pp, char * args, unsigned int timeout)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	srand((unsigned int)tv.tv_usec);
	return 1+(int) (10.0*rand()/(RAND_MAX+1.0));
}
