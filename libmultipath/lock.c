#include "lock.h"

void cleanup_lock (void * data)
{
	struct mutex_lock *lock = data;
	wakeup_fn *fn = lock->wakeup;

	unlock__(lock);
	if (fn)
		fn();
}

void set_wakeup_fn(struct mutex_lock *lck, wakeup_fn *fn)
{
	lock(lck);
	lck->wakeup = fn;
	unlock__(lck);
}
