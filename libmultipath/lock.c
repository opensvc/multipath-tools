#include <pthread.h>
#include <signal.h>
#include "lock.h"
#include <stdio.h>

void block_signal (int signum, sigset_t *old)
{
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, signum);
	pthread_sigmask(SIG_BLOCK, &set, old);
}

void cleanup_lock (void * data)
{
	unlock ((*(struct mutex_lock *)data));
}

