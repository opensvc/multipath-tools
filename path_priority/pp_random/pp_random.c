#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

int main(void)
{
	struct timeval tv;
	
	gettimeofday(&tv, NULL);
	srand((unsigned int)tv.tv_usec);
	printf("%i\n", 1+(int) (10.0*rand()/(RAND_MAX+1.0)));
	return 0;
}
