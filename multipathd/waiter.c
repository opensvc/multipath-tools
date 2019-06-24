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
#include <urcu.h>

#include "util.h"
#include "vector.h"
#include "memory.h"
#include "checkers.h"
#include "config.h"
#include "structs.h"
#include "structs_vec.h"
#include "devmapper.h"
#include "debug.h"
#include "lock.h"
#include "waiter.h"
#include "main.h"

pthread_attr_t waiter_attr;
struct mutex_lock waiter_lock = { .mutex = PTHREAD_MUTEX_INITIALIZER };

static struct event_thread *alloc_waiter (void)
{

	struct event_thread *wp;

	wp = (struct event_thread *)MALLOC(sizeof(struct event_thread));
	memset(wp, 0, sizeof(struct event_thread));

	return wp;
}

static void free_waiter (void *data)
{
	struct event_thread *wp = (struct event_thread *)data;

	if (wp->dmt)
		dm_task_destroy(wp->dmt);

	rcu_unregister_thread();
	FREE(wp);
}

void stop_waiter_thread (struct multipath *mpp, struct vectors *vecs)
{
	pthread_t thread;

	if (mpp->waiter == (pthread_t)0) {
		condlog(3, "%s: event checker thread already stopped",
			mpp->alias);
		return;
	}
	/* Don't cancel yourself. __setup_multipath is called by
	   by the waiter thread, and may remove a multipath device */
	if (pthread_equal(mpp->waiter, pthread_self()))
		return;

	condlog(3, "%s: stop event checker thread (%lu)", mpp->alias,
		mpp->waiter);
	thread = mpp->waiter;
	mpp->waiter = (pthread_t)0;
	pthread_cleanup_push(cleanup_lock, &waiter_lock);
	lock(&waiter_lock);
	pthread_kill(thread, SIGUSR2);
	pthread_cancel(thread);
	lock_cleanup_pop(&waiter_lock);
}

/*
 * returns the reschedule delay
 * negative means *stop*
 */
static int waiteventloop (struct event_thread *waiter)
{
	sigset_t set, oldset;
	int event_nr;
	int r;

	if (!waiter->event_nr)
		waiter->event_nr = dm_geteventnr(waiter->mapname);

	if (!(waiter->dmt = libmp_dm_task_create(DM_DEVICE_WAITEVENT))) {
		condlog(0, "%s: devmap event #%i dm_task_create error",
				waiter->mapname, waiter->event_nr);
		return 1;
	}

	if (!dm_task_set_name(waiter->dmt, waiter->mapname)) {
		condlog(0, "%s: devmap event #%i dm_task_set_name error",
				waiter->mapname, waiter->event_nr);
		dm_task_destroy(waiter->dmt);
		waiter->dmt = NULL;
		return 1;
	}

	if (waiter->event_nr && !dm_task_set_event_nr(waiter->dmt,
						      waiter->event_nr)) {
		condlog(0, "%s: devmap event #%i dm_task_set_event_nr error",
				waiter->mapname, waiter->event_nr);
		dm_task_destroy(waiter->dmt);
		waiter->dmt = NULL;
		return 1;
	}

	dm_task_no_open_count(waiter->dmt);

	/* wait */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR2);
	pthread_sigmask(SIG_UNBLOCK, &set, &oldset);

	pthread_testcancel();
	r = dm_task_run(waiter->dmt);
	pthread_testcancel();

	pthread_sigmask(SIG_SETMASK, &oldset, NULL);
	dm_task_destroy(waiter->dmt);
	waiter->dmt = NULL;

	if (!r)	{ /* wait interrupted by signal. check for cancellation */
		pthread_cleanup_push(cleanup_lock, &waiter_lock);
		lock(&waiter_lock);
		pthread_testcancel();
		lock_cleanup_pop(&waiter_lock);
		return 1; /* If we weren't cancelled, just reschedule */
	}

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
		lock(&waiter->vecs->lock);
		pthread_testcancel();
		r = update_multipath(waiter->vecs, waiter->mapname, 1);
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

static void *waitevent (void *et)
{
	int r;
	struct event_thread *waiter;

	mlockall(MCL_CURRENT | MCL_FUTURE);

	waiter = (struct event_thread *)et;
	pthread_cleanup_push(free_waiter, et);

	rcu_register_thread();
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
	struct event_thread *wp;

	if (!mpp)
		return 0;

	wp = alloc_waiter();

	if (!wp)
		goto out;

	strlcpy(wp->mapname, mpp->alias, WWID_SIZE);
	wp->vecs = vecs;

	if (pthread_create(&wp->thread, &waiter_attr, waitevent, wp)) {
		condlog(0, "%s: cannot create event checker", wp->mapname);
		goto out1;
	}
	mpp->waiter = wp->thread;
	condlog(3, "%s: event checker started", wp->mapname);

	return 0;
out1:
	free_waiter(wp);
	mpp->waiter = (pthread_t)0;
out:
	condlog(0, "failed to start waiter thread");
	return 1;
}
