#include "lock.h"

void cleanup_lock (void * data)
{
	unlock ((*(struct mutex_lock *)data));
}
