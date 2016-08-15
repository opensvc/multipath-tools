#include "lock.h"

void cleanup_lock (void * data)
{
	struct mutex_lock *lock = data;

	unlock(lock);
}
