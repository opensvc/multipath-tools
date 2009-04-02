/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 * Copyright (c) 2005 Kiyoshi Ueda, NEC
 * Copyright (c) 2005 Benjamin Marzinski, Redhat
 * Copyright (c) 2005 Edward Goggin, EMC
 */
#include <unistd.h>
#include <libdevmapper.h>
#include <sys/mman.h>
#include <pthread.h>
#include <signal.h>

#include "vector.h"
#include "memory.h"
#include "checkers.h"
#include "structs.h"
#include "structs_vec.h"
#include "devmapper.h"
#include "debug.h"
#include "lock.h"
#include "waiter.h"

struct event_thread *alloc_waiter (void)
{

	struct event_thread *wp;

	wp = (struct event_thread *)MALLOC(sizeof(struct event_thread));

	return wp;
}

void free_waiter (void *data)
{
	sigset_t old;
	struct event_thread *wp = (struct event_thread *)data;

	/*
	 * indicate in mpp that the wp is already freed storage
	 */
	block_signal(SIGHUP, &old);
	lock(wp->vecs->lock);

	if (wp->mpp)
		/*
		 * be careful, mpp may already be freed -- null if so
		 */
		wp->mpp->waiter = NULL;
	else
		/*
		* This is OK condition during shutdown.
		*/
		condlog(3, "free_waiter, mpp freed before wp=%p (%s).", wp, wp->mapname);

	unlock(wp->vecs->lock);
	pthread_sigmask(SIG_SETMASK, &old, NULL);

	if (wp->dmt)
		dm_task_destroy(wp->dmt);

	FREE(wp);
}

void stop_waiter_thread (struct multipath *mpp, struct vectors *vecs)
{
	struct event_thread *wp = (struct event_thread *)mpp->waiter;
	pthread_t thread;

	if (!wp) {
		condlog(3, "%s: no waiter thread", mpp->alias);
		return;
	}
	thread = wp->thread;
	condlog(2, "%s: stop event checker thread (%lu)", wp->mapname, thread);

	pthread_kill(thread, SIGUSR1);
}

static sigset_t unblock_signals(void)
{
	sigset_t set, old;

	sigemptyset(&set);
	sigaddset(&set, SIGHUP);
	sigaddset(&set, SIGUSR1);
	pthread_sigmask(SIG_UNBLOCK, &set, &old);
	return old;
}

/*
 * returns the reschedule delay
 * negative means *stop*
 */
int waiteventloop (struct event_thread *waiter)
{
	sigset_t set;
	int event_nr;
	int r;

	if (!waiter->event_nr)
		waiter->event_nr = dm_geteventnr(waiter->mapname);

	if (!(waiter->dmt = dm_task_create(DM_DEVICE_WAITEVENT))) {
		condlog(0, "%s: devmap event #%i dm_task_create error",
				waiter->mapname, waiter->event_nr);
		return 1;
	}

	if (!dm_task_set_name(waiter->dmt, waiter->mapname)) {
		condlog(0, "%s: devmap event #%i dm_task_set_name error",
				waiter->mapname, waiter->event_nr);
		dm_task_destroy(waiter->dmt);
		return 1;
	}

	if (waiter->event_nr && !dm_task_set_event_nr(waiter->dmt,
						      waiter->event_nr)) {
		condlog(0, "%s: devmap event #%i dm_task_set_event_nr error",
				waiter->mapname, waiter->event_nr);
		dm_task_destroy(waiter->dmt);
		return 1;
	}

	dm_task_no_open_count(waiter->dmt);

	/* accept wait interruption */
	set = unblock_signals();

	/* wait */
	r = dm_task_run(waiter->dmt);

	/* wait is over : event or interrupt */
	pthread_sigmask(SIG_SETMASK, &set, NULL);

	if (!r) /* wait interrupted by signal */
		return -1;

	dm_task_destroy(waiter->dmt);
	waiter->dmt = NULL;
	waiter->event_nr++;

	/*
	 * upon event ...
	 */
	while (1) {
		condlog(3, "%s: devmap event #%i",
				waiter->mapname, waiter->event_nr);

		/*
		 * event might be :
		 *
		 * 1) a table reload, which means our mpp structure is
		 *    obsolete : refresh it through update_multipath()
		 * 2) a path failed by DM : mark as such through
		 *    update_multipath()
		 * 3) map has gone away : stop the thread.
		 * 4) a path reinstate : nothing to do
		 * 5) a switch group : nothing to do
		 */
		pthread_cleanup_push(cleanup_lock, &waiter->vecs->lock);
		lock(waiter->vecs->lock);
		r = update_multipath(waiter->vecs, waiter->mapname);
		lock_cleanup_pop(waiter->vecs->lock);

		if (r) {
			condlog(2, "%s: event checker exit",
				waiter->mapname);
			return -1; /* stop the thread */
		}

		event_nr = dm_geteventnr(waiter->mapname);

		if (waiter->event_nr == event_nr)
			return 1; /* upon problem reschedule 1s later */

		waiter->event_nr = event_nr;
	}
	return -1; /* never reach there */
}

void *waitevent (void *et)
{
	int r;
	struct event_thread *waiter;

	mlockall(MCL_CURRENT | MCL_FUTURE);

	waiter = (struct event_thread *)et;
	pthread_cleanup_push(free_waiter, et);

	block_signal(SIGUSR1, NULL);
	block_signal(SIGHUP, NULL);
	while (1) {
		r = waiteventloop(waiter);

		if (r < 0)
			break;

		sleep(r);
	}

	pthread_cleanup_pop(1);
	return NULL;
}

int start_waiter_thread (struct multipath *mpp, struct vectors *vecs)
{
	pthread_attr_t attr;
	struct event_thread *wp;

	if (!mpp)
		return 0;

	if (pthread_attr_init(&attr))
		goto out;

	pthread_attr_setstacksize(&attr, 32 * 1024);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	wp = alloc_waiter();

	if (!wp)
		goto out;

	mpp->waiter = (void *)wp;
	strncpy(wp->mapname, mpp->alias, WWID_SIZE);
	wp->vecs = vecs;
	wp->mpp = mpp;

	if (pthread_create(&wp->thread, &attr, waitevent, wp)) {
		condlog(0, "%s: cannot create event checker", wp->mapname);
		goto out1;
	}
	condlog(2, "%s: event checker started", wp->mapname);

	return 0;
out1:
	free_waiter(wp);
	mpp->waiter = NULL;
out:
	condlog(0, "failed to start waiter thread");
	return 1;
}

