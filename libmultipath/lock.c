#include <pthread.h>
#include "lock.h"

void cleanup_lock (void * data)
{
	unlock((pthread_mutex_t *)data);
}

