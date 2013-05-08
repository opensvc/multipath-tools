#include <pthread.h>
#include "lock.h"
#include <stdio.h>

void cleanup_lock (void * data)
{
	unlock ((*(struct mutex_lock *)data));
}

