/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 * Copyright (c) 2005 Kiyoshi Ueda, NEC
 * Copyright (c) 2005 Benjamin Marzinski, Redhat
 * Copyright (c) 2005 Edward Goggin, EMC
 */
#include "autoconfig.h"
#include <unistd.h>
#include <sys/stat.h>
#include <libdevmapper.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <linux/oom.h>
#include <libudev.h>
#include <urcu.h>
#include "fpin.h"
#ifdef USE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif
#include <semaphore.h>
#include <time.h>
#include <stdbool.h>

/*
 * libmultipath
 */
#include "time-util.h"

/*
 * libcheckers
 */
#include "checkers.h"

/*
 * libmultipath
 */
#include "version.h"
#include "parser.h"
#include "vector.h"
#include "config.h"
#include "util.h"
#include "hwtable.h"
#include "defaults.h"
#include "structs.h"
#include "blacklist.h"
#include "structs_vec.h"
#include "dmparser.h"
#include "devmapper.h"
#include "sysfs.h"
#include "dict.h"
#include "discovery.h"
#include "debug.h"
#include "propsel.h"
#include "uevent.h"
#include "switchgroup.h"
#include "print.h"
#include "configure.h"
#include "prio.h"
#include "wwids.h"
#include "pgpolicies.h"
#include "log.h"
#include "uxsock.h"
#include "alias.h"

#include "mpath_cmd.h"
#include "mpath_persist.h"
#include "mpath_persist_int.h"

#include "prioritizers/alua_rtpg.h"

#include "main.h"
#include "pidfile.h"
#include "uxlsnr.h"
#include "uxclnt.h"
#include "cli.h"
#include "cli_handlers.h"
#include "lock.h"
#include "waiter.h"
#include "dmevents.h"
#include "io_err_stat.h"
#include "foreign.h"
#include "../third-party/valgrind/drd.h"
#include "init_unwinder.h"

#define CMDSIZE 160
#define MSG_SIZE 32

int mpath_pr_event_handle(struct path *pp);
void * mpath_pr_event_handler_fn (void * );

#define LOG_MSG(lvl, pp)					\
do {								\
	if (pp->mpp && checker_selected(&pp->checker) &&	\
	    lvl <= libmp_verbosity) {					\
		if (pp->sysfs_state == PATH_DOWN)		\
			condlog(lvl, "%s: %s - path offline",	\
				pp->mpp->alias, pp->dev);	\
		else  {						\
			const char *__m =			\
				checker_message(&pp->checker);	\
								\
			if (strlen(__m))			      \
				condlog(lvl, "%s: %s - %s checker%s", \
					pp->mpp->alias,		      \
					pp->dev,		      \
					checker_name(&pp->checker),   \
					__m);			      \
		}						      \
	}							      \
} while(0)

struct mpath_event_param
{
	char * devname;
	struct multipath *mpp;
};

int uxsock_timeout;
static int verbosity;
static int bindings_read_only;
int ignore_new_devs;
#ifdef NO_DMEVENTS_POLL
static int poll_dmevents = 0;
#else
static int poll_dmevents = 1;
#endif
/* Don't access this variable without holding config_lock */
static enum daemon_status running_state = DAEMON_INIT;
/* Don't access this variable without holding config_lock */
static bool delayed_reconfig;
/* Don't access this variable without holding config_lock */
static enum force_reload_types reconfigure_pending = FORCE_RELOAD_NONE;
pid_t daemon_pid;
static pthread_mutex_t config_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t config_cond;
static pthread_t check_thr, uevent_thr, uxlsnr_thr, uevq_thr, dmevent_thr,
	fpin_thr, fpin_consumer_thr;
static bool check_thr_started, uevent_thr_started, uxlsnr_thr_started,
	uevq_thr_started, dmevent_thr_started, fpin_thr_started,
	fpin_consumer_thr_started;
static int pid_fd = -1;

static inline enum daemon_status get_running_state(bool *pending_reconfig)
{
	enum daemon_status st;

	pthread_mutex_lock(&config_lock);
	st = running_state;
	if (pending_reconfig != NULL)
		*pending_reconfig = (reconfigure_pending != FORCE_RELOAD_NONE);
	pthread_mutex_unlock(&config_lock);
	return st;
}

int should_exit(void)
{
	return get_running_state(NULL) == DAEMON_SHUTDOWN;
}

/*
 * global copy of vecs for use in sig handlers
 */
static struct vectors * gvecs;

struct config *multipath_conf;

/* Local variables */
static volatile sig_atomic_t exit_sig;
static volatile sig_atomic_t reconfig_sig;
static volatile sig_atomic_t log_reset_sig;

static const char *daemon_status_msg[DAEMON_STATUS_SIZE] = {
	[DAEMON_INIT] = "init",
	[DAEMON_START] = "startup",
	[DAEMON_CONFIGURE] = "configure",
	[DAEMON_IDLE] = "idle",
	[DAEMON_RUNNING] = "running",
	[DAEMON_SHUTDOWN] = "shutdown",
};

const char *
daemon_status(bool *pending_reconfig)
{
	int status = get_running_state(pending_reconfig);

	if (status < DAEMON_INIT || status >= DAEMON_STATUS_SIZE)
		return NULL;

	return daemon_status_msg[status];
}

/*
 * I love you too, systemd ...
 */
#ifdef USE_SYSTEMD
static void do_sd_notify(enum daemon_status old_state,
			 enum daemon_status new_state)
{
	char notify_msg[MSG_SIZE];
	const char *msg;
	static bool startup_done = false;

	/*
	 * Checkerloop switches back and forth between idle and running state.
	 * No need to tell systemd each time.
	 * These notifications cause a lot of overhead on dbus.
	 */
	if ((new_state == DAEMON_IDLE || new_state == DAEMON_RUNNING) &&
	    (old_state == DAEMON_IDLE || old_state == DAEMON_RUNNING))
		return;

	if (new_state == DAEMON_IDLE || new_state == DAEMON_RUNNING)
		msg = "up";
	else
		msg = daemon_status_msg[new_state];

	if (msg && !safe_sprintf(notify_msg, "STATUS=%s", msg))
		sd_notify(0, notify_msg);

	if (new_state == DAEMON_SHUTDOWN) {
		/* Tell systemd that we're not RELOADING any more */
		if (old_state == DAEMON_CONFIGURE && startup_done)
			sd_notify(0, "READY=1");
		sd_notify(0, "STOPPING=1");
	} else if (new_state == DAEMON_IDLE && old_state == DAEMON_CONFIGURE) {
		sd_notify(0, "READY=1");
		startup_done = true;
	} else if (new_state == DAEMON_CONFIGURE && startup_done)
		sd_notify(0, "RELOADING=1");
}
#else
static void do_sd_notify(__attribute__((unused)) enum daemon_status old_state,
			 __attribute__((unused)) enum daemon_status new_state)
{}
#endif

static void config_cleanup(__attribute__((unused)) void *arg)
{
	pthread_mutex_unlock(&config_lock);
}

#define wait_for_state_change__(condition, ms)				\
	({								\
		struct timespec tmo;					\
		int rc = 0;						\
									\
		if (condition) {					\
			get_monotonic_time(&tmo);			\
			tmo.tv_nsec += (ms) * 1000 * 1000;		\
			normalize_timespec(&tmo);			\
			do						\
				rc = pthread_cond_timedwait(		\
					&config_cond, &config_lock, &tmo); \
			while (rc == 0 && (condition));			\
		}							\
		rc;							\
	})

/*
 * If the current status is @oldstate, wait for at most @ms milliseconds
 * for the state to change, and return the new state, which may still be
 * @oldstate.
 */
enum daemon_status wait_for_state_change_if(enum daemon_status oldstate,
					    unsigned long ms)
{
	enum daemon_status st;

	if (oldstate == DAEMON_SHUTDOWN)
		return DAEMON_SHUTDOWN;

	pthread_mutex_lock(&config_lock);
	pthread_cleanup_push(config_cleanup, NULL);
	wait_for_state_change__(running_state == oldstate, ms);
	st = running_state;
	pthread_cleanup_pop(1);
	return st;
}

/* must be called with config_lock held */
static void post_config_state__(enum daemon_status state)
{
	if (state != running_state && running_state != DAEMON_SHUTDOWN) {
		enum daemon_status old_state = running_state;

		running_state = state;
		pthread_cond_broadcast(&config_cond);
		do_sd_notify(old_state, state);
		condlog(4, "daemon state %s -> %s",
			daemon_status_msg[old_state], daemon_status_msg[state]);
	}
}

void post_config_state(enum daemon_status state)
{
	pthread_mutex_lock(&config_lock);
	pthread_cleanup_push(config_cleanup, NULL);
	post_config_state__(state);
	pthread_cleanup_pop(1);
}

static bool unblock_reconfigure(void)
{
	bool was_delayed;

	pthread_mutex_lock(&config_lock);
	was_delayed = delayed_reconfig;
	if (was_delayed) {
		delayed_reconfig = false;
		/*
		 * In IDLE state, make sure child() is woken up
		 * Otherwise it will wake up when state switches to IDLE
		 */
		if (running_state == DAEMON_IDLE)
			post_config_state__(DAEMON_CONFIGURE);
	}
	pthread_mutex_unlock(&config_lock);
	if (was_delayed)
		condlog(3, "unblocked delayed reconfigure");
	return was_delayed;
}

/*
 * Make sure child() is woken up when a map is removed that multipathd
 * is currently waiting for.
 * Overrides libmultipath's weak symbol by the same name
 */
void remove_map_callback(struct multipath *mpp)
{
	if (mpp->wait_for_udev > 0)
		unblock_reconfigure();
}

void schedule_reconfigure(enum force_reload_types requested_type)
{
	pthread_mutex_lock(&config_lock);
	pthread_cleanup_push(config_cleanup, NULL);
	enum force_reload_types type;

	type = (reconfigure_pending == FORCE_RELOAD_YES ||
		requested_type == FORCE_RELOAD_YES) ?
	       FORCE_RELOAD_YES : FORCE_RELOAD_WEAK;
	switch (running_state)
	{
	case DAEMON_SHUTDOWN:
		break;
	case DAEMON_IDLE:
		reconfigure_pending = type;
		post_config_state__(DAEMON_CONFIGURE);
		break;
	case DAEMON_CONFIGURE:
	case DAEMON_RUNNING:
		reconfigure_pending = type;
		break;
	default:
		break;
	}
	pthread_cleanup_pop(1);
}

static enum daemon_status set_config_state(enum daemon_status state)
{
	int rc = 0;
	enum daemon_status st;

	pthread_cleanup_push(config_cleanup, NULL);
	pthread_mutex_lock(&config_lock);

	while (rc == 0 &&
	       running_state != state &&
	       running_state != DAEMON_SHUTDOWN &&
	       running_state != DAEMON_IDLE) {
		rc = pthread_cond_wait(&config_cond, &config_lock);
	}

	if (rc == 0 && running_state == DAEMON_IDLE && state != DAEMON_IDLE)
		post_config_state__(state);
	st = running_state;

	pthread_cleanup_pop(1);
	return st;
}

struct config *get_multipath_config(void)
{
	rcu_read_lock();
	return rcu_dereference(multipath_conf);
}

void put_multipath_config(__attribute__((unused)) void *arg)
{
	rcu_read_unlock();
}

/*
 * The path group orderings that this function finds acceptable are different
 * from now select_path_group determines the best pathgroup. The idea here is
 * to only trigger a kernel reload when it is obvious that the pathgroups would
 * be out of order, even if all the paths were usable. Thus pathgroups with
 * PRIO_UNDEF are skipped, and the number of enabled paths doesn't matter here.
 */
bool path_groups_in_order(struct multipath *mpp)
{
	int i;
	struct pathgroup *pgp;
	bool seen_marginal_pg = false;
	int last_prio = INT_MAX;

	if (VECTOR_SIZE(mpp->pg) < 2)
		return true;

	vector_foreach_slot(mpp->pg, pgp, i) {
		if (seen_marginal_pg && !pgp->marginal)
			return false;
		/* skip pgs with PRIO_UNDEF, since this is likely temporary */
		if (!pgp->paths || pgp->priority == PRIO_UNDEF)
			continue;
		if (pgp->marginal && !seen_marginal_pg) {
			seen_marginal_pg = true;
			last_prio = pgp->priority;
			continue;
		}
		if (pgp->priority > last_prio)
			return false;
		last_prio = pgp->priority;
	}
	return true;
}

static int
need_switch_pathgroup (struct multipath * mpp, bool *need_reload)
{
	int bestpg;

	*need_reload = false;
	if (!mpp)
		return 0;

	if (VECTOR_SIZE(mpp->pg) < 2)
		return 0;

	bestpg = select_path_group(mpp);
	if (mpp->pgfailback == -FAILBACK_MANUAL)
		return 0;

	mpp->bestpg = bestpg;
	*need_reload = !path_groups_in_order(mpp);

	return (*need_reload || mpp->bestpg != mpp->nextpg);
}

static void
switch_pathgroup (struct multipath * mpp)
{
	mpp->stat_switchgroup++;
	dm_switchgroup(mpp->alias, mpp->bestpg);
	condlog(2, "%s: switch to path group #%i",
		 mpp->alias, mpp->bestpg);
}

static int
wait_for_events(struct multipath *mpp, struct vectors *vecs)
{
	if (poll_dmevents)
		return watch_dmevents(mpp->alias);
	else
		return start_waiter_thread(mpp, vecs);
}

static void
remove_map_and_stop_waiter(struct multipath *mpp, struct vectors *vecs)
{
	/* devices are automatically removed by the dmevent polling code,
	 * so they don't need to be manually removed here */
	condlog(3, "%s: removing map from internal tables", mpp->alias);
	if (!poll_dmevents)
		stop_waiter_thread(mpp);
	remove_map(mpp, vecs->pathvec, vecs->mpvec);
}

static void
remove_maps_and_stop_waiters(struct vectors *vecs)
{
	int i;
	struct multipath * mpp;

	if (!vecs)
		return;

	if (!poll_dmevents) {
		vector_foreach_slot(vecs->mpvec, mpp, i)
			stop_waiter_thread(mpp);
	}
	else
		unwatch_all_dmevents();

	remove_maps(vecs);
}

int refresh_multipath(struct vectors *vecs, struct multipath *mpp)
{
	if (dm_get_info(mpp->alias, &mpp->dmi) != DMP_OK) {
		/* Error accessing table */
		condlog(2, "%s: cannot access table", mpp->alias);
		goto out;
	}

	if (update_multipath_strings(mpp, vecs->pathvec) != DMP_OK) {
		condlog(0, "%s: failed to setup multipath", mpp->alias);
		goto out;
	}
	return 0;
out:
	remove_map_and_stop_waiter(mpp, vecs);
	return 1;
}

int setup_multipath(struct vectors *vecs, struct multipath *mpp)
{
	if (refresh_multipath(vecs, mpp) != 0)
		return 1;

	set_no_path_retry(mpp);
	if (VECTOR_SIZE(mpp->paths) != 0)
		dm_cancel_deferred_remove(mpp);
	return 0;
}

int update_multipath (struct vectors *vecs, char *mapname)
{
	struct multipath *mpp;
	struct pathgroup  *pgp;
	struct path *pp;
	int i, j;

	mpp = find_mp_by_alias(vecs->mpvec, mapname);

	if (!mpp) {
		condlog(3, "%s: multipath map not found", mapname);
		return 2;
	}

	if (setup_multipath(vecs, mpp))
		return 1; /* mpp freed in setup_multipath */

	/*
	 * compare checkers states with DM states
	 */
	vector_foreach_slot (mpp->pg, pgp, i) {
		vector_foreach_slot (pgp->paths, pp, j) {
			if (pp->dmstate != PSTATE_FAILED)
				continue;

			if (pp->state != PATH_DOWN) {
				struct config *conf;
				int oldstate = pp->state;
				unsigned int checkint;

				conf = get_multipath_config();
				checkint = conf->checkint;
				put_multipath_config(conf);
				condlog(2, "%s: mark as failed", pp->dev);
				mpp->stat_path_failures++;
				pp->state = PATH_DOWN;
				if (oldstate == PATH_UP ||
				    oldstate == PATH_GHOST)
					update_queue_mode_del_path(mpp);

				/*
				 * if opportune,
				 * schedule the next check earlier
				 */
				if (pp->tick > checkint)
					pp->tick = checkint;
			}
		}
	}
	return 0;
}

static bool
flush_map_nopaths(struct multipath *mpp, struct vectors *vecs) {
	int r;
	bool is_queueing = true;

	if (mpp->features)
		is_queueing = strstr(mpp->features, "queue_if_no_path");

	/* It's not safe to do a remove of a map that has "queue_if_no_path"
	 * set, since there could be outstanding IO which will cause
	 * multipathd to hang while attempting the remove */
	if (mpp->flush_on_last_del == FLUSH_NEVER && is_queueing) {
		condlog(2, "%s: map is queueing, can't remove", mpp->alias);
		return false;
	}
	if (mpp->flush_on_last_del == FLUSH_UNUSED &&
	    mpath_in_use(mpp->alias) && is_queueing) {
		condlog(2, "%s: map in use and queueing, can't remove",
			mpp->alias);
		return false;
	}
	/*
	 * This will flush FLUSH_NEVER devices and FLUSH_UNUSED devices
	 * that are in use, but only if they are already marked as not
	 * queueing. That is just to make absolutely certain that they
	 * really are not queueing, like they claim.
	 */
	condlog(is_queueing ? 2 : 3, "%s Last path deleted, disabling queueing",
		mpp->alias);
	mpp->retry_tick = 0;
	mpp->no_path_retry = NO_PATH_RETRY_FAIL;
	mpp->disable_queueing = 1;
	mpp->stat_map_failures++;
	if (dm_queue_if_no_path(mpp, 0) != 0) {
		condlog(0, "%s: failed to disable queueing. Not removing",
			mpp->alias);
		return false;
	}

	r = dm_flush_map_nopaths(mpp->alias, mpp->deferred_remove);
	if (r != DM_FLUSH_OK) {
		if (r == DM_FLUSH_DEFERRED) {
			condlog(2, "%s: devmap deferred remove", mpp->alias);
			mpp->deferred_remove = DEFERRED_REMOVE_IN_PROGRESS;
		}
		else
			condlog(0, "%s: can't flush", mpp->alias);
		return false;
	}

	condlog(2, "%s: map flushed after removing all paths", mpp->alias);
	remove_map_and_stop_waiter(mpp, vecs);
	return true;
}

static void
pr_register_active_paths(struct multipath *mpp)
{
	unsigned int i, j;
	struct path *pp;
	struct pathgroup *pgp;

	vector_foreach_slot (mpp->pg, pgp, i) {
		vector_foreach_slot (pgp->paths, pp, j) {
			if ((pp->state == PATH_UP) || (pp->state == PATH_GHOST))
				mpath_pr_event_handle(pp);
		}
	}
}

static int
update_map (struct multipath *mpp, struct vectors *vecs, int new_map)
{
	int retries = 3;
	char *params __attribute__((cleanup(cleanup_charp))) = NULL;

retry:
	condlog(4, "%s: updating new map", mpp->alias);
	if (adopt_paths(vecs->pathvec, mpp, NULL)) {
		condlog(0, "%s: failed to adopt paths for new map update",
			mpp->alias);
		retries = -1;
		goto fail;
	}
	verify_paths(mpp);
	if (VECTOR_SIZE(mpp->paths) == 0 &&
	    flush_map_nopaths(mpp, vecs))
		return 1;

	mpp->action = ACT_RELOAD;

	if (setup_map(mpp, &params, vecs)) {
		condlog(0, "%s: failed to setup new map in update", mpp->alias);
		retries = -1;
		goto fail;
	}
	if (domap(mpp, params, 1) == DOMAP_FAIL && retries-- > 0) {
		condlog(0, "%s: map_udate sleep", mpp->alias);
		free(params);
		params = NULL;
		sleep(1);
		goto retry;
	}

fail:
	if (new_map && (retries < 0 || wait_for_events(mpp, vecs))) {
		condlog(0, "%s: failed to create new map", mpp->alias);
		remove_map(mpp, vecs->pathvec, vecs->mpvec);
		return 1;
	}

	if (setup_multipath(vecs, mpp))
		return 1;

	sync_map_state(mpp);

	if (mpp->prflag != PRFLAG_SET)
		update_map_pr(mpp);
	if (mpp->prflag == PRFLAG_SET)
		pr_register_active_paths(mpp);

	if (retries < 0)
		condlog(0, "%s: failed reload in new map update", mpp->alias);
	return 0;
}

static int add_map_without_path (struct vectors *vecs, const char *alias)
{
	struct multipath __attribute__((cleanup(cleanup_multipath_and_paths)))
		*mpp = alloc_multipath();
	char __attribute__((cleanup(cleanup_charp))) *params = NULL;
	char __attribute__((cleanup(cleanup_charp))) *status = NULL;
	struct config *conf;
	char uuid[DM_UUID_LEN];
	int rc = DMP_ERR;

	if (!mpp || !(mpp->alias = strdup(alias)))
		return DMP_ERR;

	if ((rc = libmp_mapinfo(DM_MAP_BY_NAME | MAPINFO_MPATH_ONLY | MAPINFO_CHECK_UUID,
				(mapid_t) { .str = mpp->alias },
				(mapinfo_t) {
					.uuid = uuid,
					.dmi = &mpp->dmi,
					.size = &mpp->size,
					.target = &params,
					.status = &status,
				})) != DMP_OK)
		return rc;

	strlcpy(mpp->wwid, uuid + UUID_PREFIX_LEN, sizeof(mpp->wwid));

	if (!strlen(mpp->wwid))
		condlog(1, "%s: adding map with empty WWID", mpp->alias);

	conf = get_multipath_config();
	mpp->mpe = find_mpe(conf->mptable, mpp->wwid);
	put_multipath_config(conf);

	if ((rc = update_multipath_table__(mpp, vecs->pathvec, 0, params, status)) != DMP_OK)
		return DMP_ERR;

	if (!vector_alloc_slot(vecs->mpvec))
		return DMP_ERR;
	vector_set_slot(vecs->mpvec, steal_ptr(mpp));

	/*
	 * We can't pass mpp here, steal_ptr() has just nullified it.
	 * vector_set_slot() just set the last slot, use that.
	 */
	if (update_map(VECTOR_LAST_SLOT(vecs->mpvec), vecs, 1) != 0) /* map removed */
		return DMP_ERR;

	return DMP_OK;
}

static int
coalesce_maps(struct vectors *vecs, vector nmpv)
{
	struct multipath * ompp;
	vector ompv = vecs->mpvec;
	int i, reassign_maps;
	struct config *conf;

	conf = get_multipath_config();
	reassign_maps = conf->reassign_maps;
	put_multipath_config(conf);
	vector_foreach_slot (ompv, ompp, i) {
		condlog(3, "%s: coalesce map", ompp->alias);
		if (!find_mp_by_wwid(nmpv, ompp->wwid)) {
			/*
			 * remove all current maps not allowed by the
			 * current configuration
			 */
			if (dm_flush_map(ompp->alias) != DM_FLUSH_OK) {
				condlog(0, "%s: unable to flush devmap",
					ompp->alias);
				/*
				 * may be just because the device is open
				 */
				if (setup_multipath(vecs, ompp) != 0) {
					i--;
					continue;
				}
				if (!vector_alloc_slot(nmpv))
					return 1;

				vector_set_slot(nmpv, ompp);

				vector_del_slot(ompv, i);
				i--;
			}
			else
				condlog(2, "%s devmap removed", ompp->alias);
		} else if (reassign_maps) {
			condlog(3, "%s: Reassign existing device-mapper"
				" devices", ompp->alias);
			dm_reassign(ompp->alias);
		}
	}
	return 0;
}

static void
sync_maps_state(vector mpvec)
{
	unsigned int i;
	struct multipath *mpp;

	vector_foreach_slot (mpvec, mpp, i)
		sync_map_state(mpp);
}

int
flush_map(struct multipath * mpp, struct vectors * vecs)
{
	int r = dm_suspend_and_flush_map(mpp->alias, 0);
	if (r != DM_FLUSH_OK) {
		if (r == DM_FLUSH_FAIL_CANT_RESTORE)
			remove_feature(&mpp->features, "queue_if_no_path");
		condlog(0, "%s: can't flush", mpp->alias);
		return r;
	}

	condlog(2, "%s: map flushed", mpp->alias);
	remove_map_and_stop_waiter(mpp, vecs);

	return 0;
}

static int
uev_add_map (struct uevent * uev, struct vectors * vecs)
{
	char *alias;
	int major = -1, minor = -1, rc;

	condlog(3, "%s: add map (uevent)", uev->kernel);
	alias = uevent_get_dm_name(uev);
	if (!alias) {
		condlog(3, "%s: No DM_NAME in uevent", uev->kernel);
		major = uevent_get_major(uev);
		minor = uevent_get_minor(uev);
		alias = dm_mapname(major, minor);
		if (!alias) {
			condlog(2, "%s: mapname not found for %d:%d",
				uev->kernel, major, minor);
			return 1;
		}
	}
	pthread_cleanup_push(cleanup_lock, &vecs->lock);
	lock(&vecs->lock);
	pthread_testcancel();
	rc = ev_add_map(uev->kernel, alias, vecs);
	lock_cleanup_pop(vecs->lock);
	free(alias);
	return rc;
}

/*
 * ev_add_map expects that the multipath device already exists in kernel
 * before it is called. It just adds a device to multipathd or updates an
 * existing device.
 */
int
ev_add_map (char * dev, const char * alias, struct vectors * vecs)
{
	struct multipath * mpp;
	int reassign_maps, rc;
	struct config *conf;

	mpp = find_mp_by_alias(vecs->mpvec, alias);

	if (mpp) {
		if (mpp->wait_for_udev > 1) {
			condlog(2, "%s: performing delayed actions",
				mpp->alias);
			if (update_map(mpp, vecs, 0))
				/* setup multipathd removed the map */
				return 1;
		}
		conf = get_multipath_config();
		reassign_maps = conf->reassign_maps;
		put_multipath_config(conf);
		dm_get_info(mpp->alias, &mpp->dmi);
		if (mpp->wait_for_udev) {
			mpp->wait_for_udev = 0;
			if (!need_to_delay_reconfig(vecs) &&
			    unblock_reconfigure())
				return 0;
		}
		/*
		 * Not really an error -- we generate our own uevent
		 * if we create a multipath mapped device as a result
		 * of uev_add_path
		 */
		if (reassign_maps) {
			condlog(3, "%s: Reassign existing device-mapper devices",
				alias);
			dm_reassign(alias);
		}
		return 0;
	}
	condlog(2, "%s: adding map", alias);

	/*
	 * now we can register the map
	 */
	if ((rc = add_map_without_path(vecs, alias)) == DMP_OK) {
		condlog(2, "%s: devmap %s registered", alias, dev);
		return 0;
	} else if (rc == DMP_NO_MATCH) {
		condlog(4, "%s: not a multipath map", alias);
		return 0;
	} else {
		condlog(2, "%s: ev_add_map failed", dev);
		return 1;
	}
}

static int
uev_remove_map (struct uevent * uev, struct vectors * vecs)
{
	char *alias;
	int minor;
	struct multipath *mpp;

	condlog(3, "%s: remove map (uevent)", uev->kernel);
	alias = uevent_get_dm_name(uev);
	if (!alias) {
		condlog(3, "%s: No DM_NAME in uevent, ignoring", uev->kernel);
		return 0;
	}
	minor = uevent_get_minor(uev);

	pthread_cleanup_push(cleanup_lock, &vecs->lock);
	lock(&vecs->lock);
	pthread_testcancel();
	mpp = find_mp_by_minor(vecs->mpvec, minor);

	if (!mpp) {
		condlog(2, "%s: devmap not registered, can't remove",
			uev->kernel);
		goto out;
	}
	if (strcmp(mpp->alias, alias)) {
		condlog(2, "%s: map alias mismatch: have \"%s\", got \"%s\")",
			uev->kernel, mpp->alias, alias);
		goto out;
	}

	dm_queue_if_no_path(mpp, 0);
	remove_map_and_stop_waiter(mpp, vecs);
out:
	lock_cleanup_pop(vecs->lock);
	free(alias);
	return 0;
}

static void
rescan_path(struct udev_device *ud)
{
	ud = udev_device_get_parent_with_subsystem_devtype(ud, "scsi",
							   "scsi_device");
	if (ud) {
		ssize_t ret =
			sysfs_attr_set_value(ud, "rescan", "1", strlen("1"));
		if (ret != strlen("1"))
			log_sysfs_attr_set_value(1, ret,
						 "%s: failed to trigger rescan",
						 udev_device_get_syspath(ud));
	}
}

/* Returns true if the path was removed */
bool
handle_path_wwid_change(struct path *pp, struct vectors *vecs)
{
	struct udev_device *udd;
	static const char add[] = "add";
	ssize_t ret;
	char dev[FILE_NAME_SIZE];
	bool removed = false;

	if (!pp || !pp->udev)
		return removed;

	strlcpy(dev, pp->dev, sizeof(dev));
	udd = udev_device_ref(pp->udev);
	if (ev_remove_path(pp, vecs, 1) & REMOVE_PATH_SUCCESS) {
		removed = true;
	} else if (pp->mpp) {
		pp->dmstate = PSTATE_FAILED;
		dm_fail_path(pp->mpp->alias, pp->dev_t);
	}
	rescan_path(udd);
	ret = sysfs_attr_set_value(udd, "uevent", add, sizeof(add) - 1);
	udev_device_unref(udd);
	if (ret != sizeof(add) - 1)
		log_sysfs_attr_set_value(1, ret,
					 "%s: failed to trigger add event", dev);
	return removed;
}

bool
check_path_wwid_change(struct path *pp)
{
	char wwid[WWID_SIZE];
	int len = 0;
	size_t i;

	if (!strlen(pp->wwid))
		return false;

	/* Get the real fresh device wwid by sgio. sysfs still has old
	 * data, so only get_vpd_sgio will work to get the new wwid */
	len = get_vpd_sgio(pp->fd, 0x83, 0, wwid, WWID_SIZE);

	if (len <= 0) {
		condlog(2, "%s: failed to check wwid by sgio: len = %d",
			pp->dev, len);
		return false;
	}

	/*Strip any trailing blanks */
	for (i = strlen(wwid); i > 0 && wwid[i-1] == ' '; i--);
		/* no-op */
	wwid[i] = '\0';
	condlog(4, "%s: Got wwid %s by sgio", pp->dev, wwid);

	if (strncmp(wwid, pp->wwid, WWID_SIZE)) {
		condlog(0, "%s: wwid '%s' doesn't match wwid '%s' from device",
			pp->dev, pp->wwid, wwid);
		return true;
	}

	return false;
}

/*
 * uev_add_path can call uev_update_path, and uev_update_path can call
 * uev_add_path
 */
static int uev_update_path (struct uevent *uev, struct vectors * vecs);

static int
uev_add_path (struct uevent *uev, struct vectors * vecs, int need_do_map)
{
	struct path *pp;
	int ret = 0, i;
	struct config *conf;
	bool partial_init = false;

	condlog(3, "%s: add path (uevent)", uev->kernel);
	if (strstr(uev->kernel, "..") != NULL) {
		/*
		 * Don't allow relative device names in the pathvec
		 */
		condlog(0, "%s: path name is invalid", uev->kernel);
		return 1;
	}

	pthread_cleanup_push(cleanup_lock, &vecs->lock);
	lock(&vecs->lock);
	pthread_testcancel();
	pp = find_path_by_dev(vecs->pathvec, uev->kernel);
	if (pp) {
		int r;
		struct multipath *prev_mpp = NULL;

		if (pp->initialized == INIT_PARTIAL) {
			partial_init = true;
			goto out;
		} else if (pp->initialized == INIT_REMOVED) {
			condlog(3, "%s: re-adding removed path", pp->dev);
			pp->initialized = INIT_NEW;
			prev_mpp = pp->mpp;
			if (prev_mpp == NULL)
				condlog(0, "Bug: %s was in INIT_REMOVED state without being a multipath member",
					pp->dev);
			pp->mpp = NULL;
			/* make sure get_uid() is called */
			pp->wwid[0] = '\0';
		} else
			condlog(3,
				"%s: spurious uevent, path already in pathvec",
				uev->kernel);

		if (!pp->mpp && !strlen(pp->wwid)) {
			condlog(3, "%s: reinitialize path", uev->kernel);
			udev_device_unref(pp->udev);
			pp->udev = udev_device_ref(uev->udev);
			conf = get_multipath_config();
			pthread_cleanup_push(put_multipath_config, conf);
			r = pathinfo(pp, conf,
				     DI_ALL | DI_BLACKLIST);
			pthread_cleanup_pop(1);
			if (r == PATHINFO_OK && !prev_mpp)
				ret = ev_add_path(pp, vecs, need_do_map);
			else if (r == PATHINFO_OK &&
				 !strncmp(pp->wwid, prev_mpp->wwid, WWID_SIZE)) {
				/*
				 * Path was unsuccessfully removed, but now
				 * re-added, and still belongs to the right map
				 * - all fine, reinstate asap
				 */
				pp->mpp = prev_mpp;
				pp->tick = 1;
				ret = 0;
			} else if (prev_mpp) {
				/*
				 * Bad: re-added path still hangs in wrong map
				 * Make another attempt to remove the path
				 */
				pp->mpp = prev_mpp;
				if (!(ev_remove_path(pp, vecs, true) &
				      REMOVE_PATH_SUCCESS)) {
					/*
					 * Failure in ev_remove_path will keep
					 * path in pathvec in INIT_REMOVED state
					 * Fail the path to make sure it isn't
					 * used anymore.
					 */
					pp->dmstate = PSTATE_FAILED;
					dm_fail_path(pp->mpp->alias, pp->dev_t);
					condlog(1, "%s: failed to re-add path still mapped in %s",
						pp->dev, pp->mpp->alias);
					ret = 1;
				} else if (r == PATHINFO_OK)
					/*
					 * Path successfully freed, move on to
					 * "new path" code path below
					 */
					pp = NULL;
			} else if (r == PATHINFO_SKIPPED) {
				condlog(3, "%s: remove blacklisted path",
					uev->kernel);
				i = find_slot(vecs->pathvec, (void *)pp);
				if (i != -1)
					vector_del_slot(vecs->pathvec, i);
				free_path(pp);
			} else {
				condlog(0, "%s: failed to reinitialize path",
					uev->kernel);
				ret = 1;
			}
		}
	}
	if (pp)
		goto out;

	/*
	 * get path vital state
	 */
	conf = get_multipath_config();
	pthread_cleanup_push(put_multipath_config, conf);
	ret = alloc_path_with_pathinfo(conf, uev->udev,
				       uev->wwid, DI_ALL, &pp);
	pthread_cleanup_pop(1);
	if (!pp) {
		if (ret == PATHINFO_SKIPPED)
			ret = 0;
		else {
			condlog(3, "%s: failed to get path info", uev->kernel);
			ret = 1;
		}
		goto out;
	}
	ret = store_path(vecs->pathvec, pp);
	if (!ret) {
		conf = get_multipath_config();
		pp->checkint = conf->checkint;
		put_multipath_config(conf);
		ret = ev_add_path(pp, vecs, need_do_map);
	} else {
		condlog(0, "%s: failed to store path info, "
			"dropping event",
			uev->kernel);
		free_path(pp);
		ret = 1;
	}
out:
	lock_cleanup_pop(vecs->lock);
	if (partial_init)
		return uev_update_path(uev, vecs);
	return ret;
}

static int
sysfs_get_ro (struct path *pp)
{
	int ro;
	char buff[3]; /* Either "0\n\0" or "1\n\0" */

	if (!pp->udev)
		return -1;

	if (!sysfs_attr_get_value_ok(pp->udev, "ro", buff, sizeof(buff))) {
		condlog(3, "%s: Cannot read ro attribute in sysfs", pp->dev);
		return -1;
	}

	if (sscanf(buff, "%d\n", &ro) != 1 || ro < 0 || ro > 1) {
		condlog(3, "%s: Cannot parse ro attribute", pp->dev);
		return -1;
	}

	return ro;
}

/*
 * returns:
 * 0: added
 * 1: error
 */
int
ev_add_path (struct path * pp, struct vectors * vecs, int need_do_map)
{
	struct multipath * mpp;
	char *params __attribute__((cleanup(cleanup_charp))) = NULL;
	int retries = 3;
	int start_waiter = 0;
	int ret;
	int ro;
	unsigned char prflag = PRFLAG_UNSET;

	/*
	 * need path UID to go any further
	 */
	if (strlen(pp->wwid) == 0) {
		condlog(0, "%s: failed to get path uid", pp->dev);
		goto fail; /* leave path added to pathvec */
	}
	mpp = find_mp_by_wwid(vecs->mpvec, pp->wwid);
	if (mpp && pp->size && mpp->size != pp->size) {
		condlog(0, "%s: failed to add new path %s, device size mismatch", mpp->alias, pp->dev);
		int i = find_slot(vecs->pathvec, (void *)pp);
		if (i != -1)
			vector_del_slot(vecs->pathvec, i);
		free_path(pp);
		return 1;
	}
	if (mpp)
		trigger_path_udev_change(pp, true);
	if (mpp && mpp->wait_for_udev &&
	    (pathcount(mpp, PATH_UP) > 0 ||
	     (pathcount(mpp, PATH_GHOST) > 0 &&
	      path_get_tpgs(pp) != TPGS_IMPLICIT &&
	      mpp->ghost_delay_tick <= 0))) {
		/* if wait_for_udev is set and valid paths exist */
		condlog(3, "%s: delaying path addition until %s is fully initialized",
			pp->dev, mpp->alias);
		mpp->wait_for_udev = 2;
		orphan_path(pp, "waiting for create to complete");
		return 0;
	}

	pp->mpp = mpp;
rescan:
	if (mpp) {
		condlog(4,"%s: adopting all paths for path %s",
			mpp->alias, pp->dev);
		if (adopt_paths(vecs->pathvec, mpp, NULL) || pp->mpp != mpp ||
		    find_slot(mpp->paths, pp) == -1)
			goto fail; /* leave path added to pathvec */

		verify_paths(mpp);
		mpp->action = ACT_RELOAD;
		prflag = mpp->prflag;
		mpath_pr_event_handle(pp);
	} else {
		if (!should_multipath(pp, vecs->pathvec, vecs->mpvec)) {
			orphan_path(pp, "only one path");
			return 0;
		}
		condlog(4,"%s: creating new map", pp->dev);
		if ((mpp = add_map_with_path(vecs, pp, 1, NULL))) {
			mpp->action = ACT_CREATE;
			/*
			 * We don't depend on ACT_CREATE, as domap will
			 * set it to ACT_NOTHING when complete.
			 */
			start_waiter = 1;
		}
		else
			goto fail; /* leave path added to pathvec */
	}

	/* ro check - if new path is ro, force map to be ro as well */
	ro = sysfs_get_ro(pp);
	if (ro == 1)
		mpp->force_readonly = 1;

	if (!need_do_map)
		return 0;

	if (!dm_map_present(mpp->alias)) {
		mpp->action = ACT_CREATE;
		start_waiter = 1;
	}
	/*
	 * push the map to the device-mapper
	 */
	if (setup_map(mpp, &params, vecs)) {
		condlog(0, "%s: failed to setup map for addition of new "
			"path %s", mpp->alias, pp->dev);
		goto fail_map;
	}
	/*
	 * reload the map for the multipath mapped device
	 */
	ret = domap(mpp, params, 1);
	while (ret == DOMAP_RETRY && retries-- > 0) {
		condlog(0, "%s: retry domap for addition of new "
			"path %s", mpp->alias, pp->dev);
		sleep(1);
		ret = domap(mpp, params, 1);
	}
	if (ret == DOMAP_FAIL || ret == DOMAP_RETRY) {
		condlog(0, "%s: failed in domap for addition of new "
			"path %s", mpp->alias, pp->dev);
		/*
		 * deal with asynchronous uevents :((
		 */
		if (mpp->action == ACT_RELOAD && retries-- > 0) {
			condlog(0, "%s: ev_add_path sleep", mpp->alias);
			sleep(1);
			update_mpp_paths(mpp, vecs->pathvec);
			free(params);
			params = NULL;
			goto rescan;
		}
		else if (mpp->action == ACT_RELOAD)
			condlog(0, "%s: giving up reload", mpp->alias);
		else
			goto fail_map;
	}

	if ((mpp->action == ACT_CREATE ||
	     (mpp->action == ACT_NOTHING && start_waiter && !mpp->waiter)) &&
	    wait_for_events(mpp, vecs))
			goto fail_map;

	/*
	 * update our state from kernel regardless of create or reload
	 */
	if (setup_multipath(vecs, mpp))
		goto fail; /* if setup_multipath fails, it removes the map */

	sync_map_state(mpp);

	if (retries >= 0) {
		if (start_waiter)
			update_map_pr(mpp);
		if (mpp->prflag == PRFLAG_SET && prflag != PRFLAG_SET)
				pr_register_active_paths(mpp);
		condlog(2, "%s [%s]: path added to devmap %s",
			pp->dev, pp->dev_t, mpp->alias);
		return 0;
	} else
		goto fail;

fail_map:
	remove_map(mpp, vecs->pathvec, vecs->mpvec);
fail:
	orphan_path(pp, "failed to add path");
	return 1;
}

static int
uev_remove_path (struct uevent *uev, struct vectors * vecs, int need_do_map)
{
	struct path *pp;

	condlog(3, "%s: remove path (uevent)", uev->kernel);
	delete_foreign(uev->udev);

	pthread_cleanup_push(cleanup_lock, &vecs->lock);
	lock(&vecs->lock);
	pthread_testcancel();
	pp = find_path_by_dev(vecs->pathvec, uev->kernel);
	if (pp)
		ev_remove_path(pp, vecs, need_do_map);
	lock_cleanup_pop(vecs->lock);
	if (!pp) /* Not an error; path might have been purged earlier */
		condlog(0, "%s: path already removed", uev->kernel);
	return 0;
}

int
ev_remove_path (struct path *pp, struct vectors * vecs, int need_do_map)
{
	struct multipath * mpp;
	int i, retval = REMOVE_PATH_SUCCESS;
	char *params __attribute__((cleanup(cleanup_charp))) = NULL;

	/*
	 * avoid referring to the map of an orphaned path
	 */
	if ((mpp = pp->mpp)) {
		char devt[BLK_DEV_SIZE];

		/*
		 * Mark the path as removed. In case of success, we
		 * will delete it for good. Otherwise, it will be deleted
		 * later, unless all attempts to reload this map fail.
		 */
		set_path_removed(pp);

		/*
		 * transform the mp->pg vector of vectors of paths
		 * into a mp->params string to feed the device-mapper
		 */
		if (update_mpp_paths(mpp, vecs->pathvec)) {
			condlog(0, "%s: failed to update paths",
				mpp->alias);
			goto fail;
		}

		/*
		 * we have to explicitly remove pp from mpp->paths,
		 * update_mpp_paths() doesn't do that.
		 */
		i = find_slot(mpp->paths, pp);
		if (i != -1)
			vector_del_slot(mpp->paths, i);

		/*
		 * remove the map IF removing the last path. If
		 * flush_map_nopaths succeeds, the path has been removed.
		 */
		if (VECTOR_SIZE(mpp->paths) == 0 &&
		    flush_map_nopaths(mpp, vecs))
			goto out;

		if (mpp->wait_for_udev) {
			mpp->wait_for_udev = 2;
			retval = REMOVE_PATH_DELAY;
			goto out;
		}

		if (!need_do_map) {
			retval = REMOVE_PATH_DELAY;
			goto out;
		}

		if (setup_map(mpp, &params, vecs)) {
			condlog(0, "%s: failed to setup map for"
				" removal of path %s", mpp->alias, pp->dev);
			goto fail;
		}
		/*
		 * reload the map
		 */
		mpp->action = ACT_RELOAD;
		if (domap(mpp, params, 1) == DOMAP_FAIL) {
			condlog(0, "%s: failed in domap for "
				"removal of path %s",
				mpp->alias, pp->dev);
			retval = REMOVE_PATH_FAILURE;
		}
		/*
		 * update mpp state from kernel even if domap failed.
		 * If the path was removed from the mpp, setup_multipath will
		 * free the path regardless of whether it succeeds or fails
		 */
		strlcpy(devt, pp->dev_t, sizeof(devt));
		if (setup_multipath(vecs, mpp))
			return REMOVE_PATH_MAP_ERROR;
		sync_map_state(mpp);

		if (retval == REMOVE_PATH_SUCCESS)
			condlog(2, "%s: path removed from map %s",
				devt, mpp->alias);
	} else {
		/* mpp == NULL */
		if ((i = find_slot(vecs->pathvec, (void *)pp)) != -1)
			vector_del_slot(vecs->pathvec, i);
		free_path(pp);
	}
out:
	return retval;

fail:
	condlog(0, "%s: error removing path. removing map %s", pp->dev,
		mpp->alias);
	remove_map_and_stop_waiter(mpp, vecs);
	return REMOVE_PATH_MAP_ERROR;
}

int
finish_path_init(struct path *pp, struct vectors * vecs)
{
	int r;
	struct config *conf;

	if (pp->udev && pp->uid_attribute && *pp->uid_attribute &&
	    !udev_device_get_is_initialized(pp->udev))
		return 0;
	conf = get_multipath_config();
	pthread_cleanup_push(put_multipath_config, conf);
	r = pathinfo(pp, conf, DI_ALL|DI_BLACKLIST);
	pthread_cleanup_pop(1);

	if (r == PATHINFO_OK)
		return 0;

	condlog(0, "%s: error fully initializing path, removing", pp->dev);
	ev_remove_path(pp, vecs, 1);
	return -1;
}

static bool
needs_ro_update(struct multipath *mpp, int ro)
{
	struct pathgroup * pgp;
	struct path * pp;
	unsigned int i, j;

	if (!mpp || ro < 0)
		return false;
	if (!has_dm_info(mpp))
		return true;
	if (mpp->dmi.read_only == ro)
		return false;
	if (ro == 1)
		return true;
	vector_foreach_slot (mpp->pg, pgp, i) {
		vector_foreach_slot (pgp->paths, pp, j) {
			if (sysfs_get_ro(pp) == 1)
				return false;
		}
	}
	return true;
}

int resize_map(struct multipath *mpp, unsigned long long size,
	       struct vectors * vecs)
{
	int ret = 0;
	char *params __attribute__((cleanup(cleanup_charp))) = NULL;
	unsigned long long orig_size = mpp->size;

	mpp->size = size;
	update_mpp_paths(mpp, vecs->pathvec);
	if (setup_map(mpp, &params, vecs) != 0) {
		condlog(0, "%s: failed to setup map for resize : %s",
			mpp->alias, strerror(errno));
		mpp->size = orig_size;
		ret = 1;
		goto out;
	}
	mpp->action = ACT_RESIZE;
	mpp->force_udev_reload = 1;
	if (domap(mpp, params, 1) == DOMAP_FAIL) {
		condlog(0, "%s: failed to resize map : %s", mpp->alias,
			strerror(errno));
		mpp->size = orig_size;
		ret = 1;
	}
out:
	if (setup_multipath(vecs, mpp) != 0)
		return 2;
	sync_map_state(mpp);

	return ret;
}

static int
uev_update_path (struct uevent *uev, struct vectors * vecs)
{
	int ro, retval = 0, rc;
	struct path * pp;
	struct config *conf;
	int needs_reinit = 0;

	switch ((rc = change_foreign(uev->udev))) {
	case FOREIGN_OK:
		/* known foreign path, ignore event */
		return 0;
	case FOREIGN_IGNORED:
		break;
	case FOREIGN_ERR:
		condlog(3, "%s: error in change_foreign", __func__);
		break;
	default:
		condlog(1, "%s: return code %d of change_foreign is unsupported",
			__func__, rc);
		break;
	}

	pthread_cleanup_push(cleanup_lock, &vecs->lock);
	lock(&vecs->lock);
	pthread_testcancel();

	pp = find_path_by_dev(vecs->pathvec, uev->kernel);
	if (pp) {
		struct multipath *mpp = pp->mpp;
		char wwid[WWID_SIZE];
		int auto_resize;

		conf = get_multipath_config();
		auto_resize = conf->auto_resize;
		put_multipath_config(conf);

		if (pp->initialized == INIT_REQUESTED_UDEV) {
			needs_reinit = 1;
			goto out;
		}
		/* Don't deal with other types of failed initialization
		 * now. check_path will handle it */
		if (!strlen(pp->wwid) && pp->initialized != INIT_PARTIAL)
			goto out;

		strcpy(wwid, pp->wwid);
		rc = get_uid(pp, pp->state, uev->udev, 0);

		if (rc != 0)
			strcpy(pp->wwid, wwid);
		else if (strlen(wwid) &&
			 strncmp(wwid, pp->wwid, WWID_SIZE) != 0) {
			condlog(0, "%s: path wwid changed from '%s' to '%s'",
				uev->kernel, wwid, pp->wwid);
			ev_remove_path(pp, vecs, 1);
			needs_reinit = 1;
			goto out;
		} else if (pp->initialized == INIT_PARTIAL) {
			udev_device_unref(pp->udev);
			pp->udev = udev_device_ref(uev->udev);
			if (finish_path_init(pp, vecs) < 0) {
				retval = 1;
				goto out;
			}
		} else {
			udev_device_unref(pp->udev);
			pp->udev = udev_device_ref(uev->udev);
			conf = get_multipath_config();
			pthread_cleanup_push(put_multipath_config, conf);
			if (pathinfo(pp, conf, DI_SYSFS|DI_NOIO) != PATHINFO_OK)
				condlog(1, "%s: pathinfo failed after change uevent",
					uev->kernel);
			pthread_cleanup_pop(1);
		}

		ro = uevent_get_disk_ro(uev);
		if (needs_ro_update(mpp, ro)) {
			condlog(2, "%s: update path write_protect to '%d' (uevent)", uev->kernel, ro);

			if (mpp->wait_for_udev)
				mpp->wait_for_udev = 2;
			else {
				if (ro == 1)
					pp->mpp->force_readonly = 1;
				retval = reload_and_sync_map(mpp, vecs);
				if (retval == 2)
					condlog(2, "%s: map removed during reload", pp->dev);
				else {
					pp->mpp->force_readonly = 0;
					condlog(2, "%s: map %s reloaded (retval %d)", uev->kernel, mpp->alias, retval);
				}
			}
		}
		if (auto_resize != AUTO_RESIZE_NEVER && mpp &&
		    !mpp->wait_for_udev) {
			struct pathgroup *pgp;
			struct path *pp2;
			unsigned int i, j;
			unsigned long long orig_size = mpp->size;

			if (!pp->size || pp->size == mpp->size ||
			    (pp->size < mpp->size &&
			     auto_resize == AUTO_RESIZE_GROW_ONLY))
				goto out;

			vector_foreach_slot(mpp->pg, pgp, i)
				vector_foreach_slot (pgp->paths, pp2, j)
					if (pp2->size && pp2->size != pp->size)
						goto out;
			retval = resize_map(mpp, pp->size, vecs);
			if (retval == 2)
				condlog(2, "%s: map removed during resize", pp->dev);
			else if (retval == 0)
				condlog(2, "%s: resized map from %llu to %llu",
					mpp->alias, orig_size, pp->size);
		}
	}
out:
	lock_cleanup_pop(vecs->lock);
	if (!pp) {
		/* If the path is blacklisted, print a debug/non-default verbosity message. */
		if (uev->udev) {
			int flag = DI_SYSFS | DI_WWID;

			conf = get_multipath_config();
			pthread_cleanup_push(put_multipath_config, conf);
			retval = alloc_path_with_pathinfo(conf, uev->udev, uev->wwid, flag, NULL);
			pthread_cleanup_pop(1);

			if (retval == PATHINFO_SKIPPED) {
				condlog(3, "%s: spurious uevent, path is blacklisted", uev->kernel);
				return 0;
			}
		}

		condlog(0, "%s: spurious uevent, path not found", uev->kernel);
	}
	/* pp->initialized must not be INIT_PARTIAL if needs_reinit is set */
	if (needs_reinit)
		retval = uev_add_path(uev, vecs, 1);
	return retval;
}

static int
uev_pathfail_check(struct uevent *uev, struct vectors *vecs)
{
	char *action = NULL, *devt = NULL;
	struct path *pp;
	int r = 1;

	action = uevent_get_dm_action(uev);
	if (!action)
		return 1;
	if (strncmp(action, "PATH_FAILED", 11))
		goto out;
	devt = uevent_get_dm_path(uev);
	if (!devt) {
		condlog(3, "%s: No DM_PATH in uevent", uev->kernel);
		goto out;
	}

	pthread_cleanup_push(cleanup_lock, &vecs->lock);
	lock(&vecs->lock);
	pthread_testcancel();
	pp = find_path_by_devt(vecs->pathvec, devt);
	if (!pp)
		goto out_lock;
	r = io_err_stat_handle_pathfail(pp);
	if (r)
		condlog(3, "io_err_stat: %s: cannot handle pathfail uevent",
				pp->dev);
out_lock:
	lock_cleanup_pop(vecs->lock);
	free(devt);
	free(action);
	return r;
out:
	free(action);
	return 1;
}

static int
map_discovery (struct vectors * vecs)
{
	struct multipath * mpp;
	int i;

	if (dm_get_maps(vecs->mpvec))
		return 1;

	vector_foreach_slot (vecs->mpvec, mpp, i)
		if (update_multipath_table(mpp, vecs->pathvec, 0) != DMP_OK) {
			remove_map(mpp, vecs->pathvec, vecs->mpvec);
			i--;
		}

	return 0;
}

int
uev_trigger (struct uevent * uev, void * trigger_data)
{
	int r = 0;
	struct vectors * vecs;
	struct uevent *merge_uev, *tmp;
	enum daemon_status state;

	vecs = (struct vectors *)trigger_data;

	pthread_cleanup_push(config_cleanup, NULL);
	pthread_mutex_lock(&config_lock);
	while (running_state != DAEMON_IDLE &&
	       running_state != DAEMON_RUNNING &&
	       running_state != DAEMON_SHUTDOWN)
		pthread_cond_wait(&config_cond, &config_lock);
	state = running_state;
	pthread_cleanup_pop(1);

	if (state == DAEMON_SHUTDOWN)
		return 0;

	/*
	 * device map event
	 * Add events are ignored here as the tables
	 * are not fully initialised then.
	 */
	if (!strncmp(uev->kernel, "dm-", 3)) {
		if (!uevent_is_mpath(uev)) {
			if (!strncmp(uev->action, "change", 6))
				(void)add_foreign(uev->udev);
			else if (!strncmp(uev->action, "remove", 6))
				(void)delete_foreign(uev->udev);
			goto out;
		}
		if (!strncmp(uev->action, "change", 6)) {
			r = uev_add_map(uev, vecs);

			/*
			 * the kernel-side dm-mpath issues a PATH_FAILED event
			 * when it encounters a path IO error. It is reason-
			 * able be the entry of path IO error accounting pro-
			 * cess.
			 */
			uev_pathfail_check(uev, vecs);
		} else if (!strncmp(uev->action, "remove", 6)) {
			r = uev_remove_map(uev, vecs);
		}
		goto out;
	}

	/*
	 * path add/remove/change event, add/remove maybe merged
	 */
	list_for_each_entry_safe(merge_uev, tmp, &uev->merge_node, node) {
		if (!strncmp(merge_uev->action, "add", 3))
			r += uev_add_path(merge_uev, vecs, 0);
		if (!strncmp(merge_uev->action, "remove", 6))
			r += uev_remove_path(merge_uev, vecs, 0);
	}

	if (!strncmp(uev->action, "add", 3))
		r += uev_add_path(uev, vecs, 1);
	if (!strncmp(uev->action, "remove", 6))
		r += uev_remove_path(uev, vecs, 1);
	if (!strncmp(uev->action, "change", 6))
		r += uev_update_path(uev, vecs);

out:
	return r;
}

static void rcu_unregister(__attribute__((unused)) void *param)
{
	rcu_unregister_thread();
}

static void *
ueventloop (void * ap)
{
	struct udev *udev = ap;

	pthread_cleanup_push(rcu_unregister, NULL);
	rcu_register_thread();
	if (uevent_listen(udev))
		condlog(0, "error starting uevent listener");
	pthread_cleanup_pop(1);
	return NULL;
}

static void *
uevqloop (void * ap)
{
	pthread_cleanup_push(rcu_unregister, NULL);
	rcu_register_thread();
	if (uevent_dispatch(&uev_trigger, ap))
		condlog(0, "error starting uevent dispatcher");
	pthread_cleanup_pop(1);
	return NULL;
}
static void *
uxlsnrloop (void * ap)
{
	long ux_sock;

	pthread_cleanup_push(rcu_unregister, NULL);
	rcu_register_thread();

	ux_sock = ux_socket_listen(DEFAULT_SOCKET);
	if (ux_sock == -1) {
		condlog(1, "could not create uxsock: %d", errno);
		exit_daemon();
		goto out;
	}
	pthread_cleanup_push(uxsock_cleanup, (void *)ux_sock);

	if (cli_init()) {
		condlog(1, "Failed to init uxsock listener");
		exit_daemon();
		goto out_sock;
	}

	/* Tell main thread that thread has started */
	post_config_state(DAEMON_CONFIGURE);

	umask(077);

	/*
	 * Wait for initial reconfiguration to finish, while
	 * handling signals
	 */
	while (wait_for_state_change_if(DAEMON_CONFIGURE, 50)
	       == DAEMON_CONFIGURE)
		handle_signals(false);

	uxsock_listen(ux_sock, ap);

out_sock:
	pthread_cleanup_pop(1); /* uxsock_cleanup */
out:
	pthread_cleanup_pop(1); /* rcu_unregister */
	return NULL;
}

void
exit_daemon (void)
{
	post_config_state(DAEMON_SHUTDOWN);
}

static void
fail_path (struct path * pp, int del_active)
{
	if (!pp->mpp)
		return;

	condlog(2, "checker failed path %s in map %s",
		 pp->dev_t, pp->mpp->alias);

	dm_fail_path(pp->mpp->alias, pp->dev_t);
	if (del_active)
		update_queue_mode_del_path(pp->mpp);
}

/*
 * caller must have locked the path list before calling that function
 */
static void
reinstate_path (struct path * pp)
{
	if (!pp->mpp)
		return;

	if (dm_reinstate_path(pp->mpp->alias, pp->dev_t))
		condlog(0, "%s: reinstate failed", pp->dev_t);
	else {
		condlog(2, "%s: reinstated", pp->dev_t);
		update_queue_mode_add_path(pp->mpp);
	}
}

static void
enable_group(struct path * pp)
{
	struct pathgroup * pgp;

	/*
	 * if path is added through uev_add_path, pgindex can be unset.
	 * next update_strings() will set it, upon map reload event.
	 *
	 * we can safely return here, because upon map reload, all
	 * PG will be enabled.
	 */
	if (!pp->mpp->pg || !pp->pgindex)
		return;

	pgp = VECTOR_SLOT(pp->mpp->pg, pp->pgindex - 1);

	if (pgp->status == PGSTATE_DISABLED) {
		condlog(2, "%s: enable group #%i", pp->mpp->alias, pp->pgindex);
		dm_enablegroup(pp->mpp->alias, pp->pgindex);
	}
}

static void
mpvec_garbage_collector (struct vectors * vecs)
{
	struct multipath * mpp;
	int i;

	if (!vecs->mpvec)
		return;

	vector_foreach_slot (vecs->mpvec, mpp, i) {
		if (mpp && mpp->alias && !dm_map_present(mpp->alias)) {
			condlog(2, "%s: remove dead map", mpp->alias);
			remove_map_and_stop_waiter(mpp, vecs);
			i--;
		}
	}
}

/* This is called after a path has started working again. It the multipath
 * device for this path uses the followover failback type, and this is the
 * best pathgroup, and this is the first path in the pathgroup to come back
 * up, then switch to this pathgroup */
static int
followover_should_failback(struct path * pp)
{
	struct pathgroup * pgp;
	struct path *pp1;
	int i;

	if (pp->mpp->pgfailback != -FAILBACK_FOLLOWOVER ||
	    !pp->mpp->pg || !pp->pgindex ||
	    pp->pgindex != pp->mpp->bestpg)
		return 0;

	pgp = VECTOR_SLOT(pp->mpp->pg, pp->pgindex - 1);
	vector_foreach_slot(pgp->paths, pp1, i) {
		if (pp1 == pp)
			continue;
		if (pp1->chkrstate != PATH_DOWN && pp1->chkrstate != PATH_SHAKY)
			return 0;
	}
	return 1;
}

static void
missing_uev_wait_tick(struct vectors *vecs)
{
	struct multipath * mpp;
	int i;
	int timed_out = 0;

	vector_foreach_slot (vecs->mpvec, mpp, i) {
		if (mpp->wait_for_udev && --mpp->uev_wait_tick <= 0) {
			timed_out = 1;
			condlog(0, "%s: timeout waiting on creation uevent. enabling reloads", mpp->alias);
			if (mpp->wait_for_udev > 1 &&
			    update_map(mpp, vecs, 0)) {
				/* update_map removed map */
				i--;
				continue;
			}
			mpp->wait_for_udev = 0;
		}
	}

	if (timed_out && !need_to_delay_reconfig(vecs))
		unblock_reconfigure();
}

static void
ghost_delay_tick(struct vectors *vecs)
{
	struct multipath * mpp;
	int i;

	vector_foreach_slot (vecs->mpvec, mpp, i) {
		if (mpp->ghost_delay_tick <= 0)
			continue;
		if (--mpp->ghost_delay_tick <= 0) {
			condlog(0, "%s: timed out waiting for active path",
				mpp->alias);
			mpp->force_udev_reload = 1;
			if (update_map(mpp, vecs, 0) != 0) {
				/* update_map removed map */
				i--;
				continue;
			}
		}
	}
}

static void
deferred_failback_tick (struct vectors *vecs)
{
	struct multipath * mpp;
	unsigned int i;
	bool need_reload;

	vector_foreach_slot (vecs->mpvec, mpp, i) {
		/*
		 * deferred failback getting sooner
		 */
		if (mpp->pgfailback > 0 && mpp->failback_tick > 0) {
			mpp->failback_tick--;

			if (!mpp->failback_tick &&
			    need_switch_pathgroup(mpp, &need_reload)) {
				if (need_reload)
					reload_and_sync_map(mpp, vecs);
				else
					switch_pathgroup(mpp);
			}
		}
	}
}

static void
retry_count_tick(vector mpvec)
{
	struct multipath *mpp;
	unsigned int i;

	vector_foreach_slot (mpvec, mpp, i) {
		if (mpp->retry_tick > 0) {
			mpp->stat_total_queueing_time++;
			condlog(4, "%s: Retrying.. No active path", mpp->alias);
			if(--mpp->retry_tick == 0) {
				mpp->stat_map_failures++;
				dm_queue_if_no_path(mpp, 0);
				condlog(2, "%s: Disable queueing", mpp->alias);
			}
		}
	}
}

static void
partial_retrigger_tick(vector pathvec)
{
	struct path *pp;
	unsigned int i;

	vector_foreach_slot (pathvec, pp, i) {
		if (pp->initialized == INIT_PARTIAL && pp->udev &&
		    pp->partial_retrigger_delay > 0 &&
		    --pp->partial_retrigger_delay == 0) {
			const char *msg = udev_device_get_is_initialized(pp->udev) ?
					  "change" : "add";
			ssize_t len = strlen(msg);
			ssize_t ret = sysfs_attr_set_value(pp->udev, "uevent", msg,
							   len);

			if (len != ret)
				log_sysfs_attr_set_value(2, ret,
					"%s: failed to trigger %s event",
					pp->dev, msg);
		}
	}
}

static int update_prio(struct path *pp, int force_refresh_all)
{
	int oldpriority;
	struct path *pp1;
	struct pathgroup * pgp;
	int i, j, changed = 0;
	struct config *conf;

	oldpriority = pp->priority;
	if (pp->state != PATH_DOWN) {
		conf = get_multipath_config();
		pthread_cleanup_push(put_multipath_config, conf);
		pathinfo(pp, conf, DI_PRIO);
		pthread_cleanup_pop(1);
	}

	if (pp->priority != oldpriority)
		changed = 1;
	else if (!force_refresh_all)
		return 0;

	vector_foreach_slot (pp->mpp->pg, pgp, i) {
		vector_foreach_slot (pgp->paths, pp1, j) {
			if (pp1 == pp || pp1->state == PATH_DOWN)
				continue;
			oldpriority = pp1->priority;
			conf = get_multipath_config();
			pthread_cleanup_push(put_multipath_config, conf);
			pathinfo(pp1, conf, DI_PRIO);
			pthread_cleanup_pop(1);
			if (pp1->priority != oldpriority)
				changed = 1;
		}
	}
	return changed;
}

static int reload_map(struct vectors *vecs, struct multipath *mpp,
		      int is_daemon)
{
	char *params __attribute__((cleanup(cleanup_charp))) = NULL;
	int r;

	update_mpp_paths(mpp, vecs->pathvec);
	if (setup_map(mpp, &params, vecs)) {
		condlog(0, "%s: failed to setup map", mpp->alias);
		return 1;
	}
	select_action(mpp, vecs->mpvec, 1);

	r = domap(mpp, params, is_daemon);
	if (r == DOMAP_FAIL || r == DOMAP_RETRY) {
		condlog(3, "%s: domap (%u) failure "
			"for reload map", mpp->alias, r);
		return 1;
	}

	return 0;
}

int reload_and_sync_map(struct multipath *mpp, struct vectors *vecs)
{
	int ret = 0;

	if (reload_map(vecs, mpp, 1))
		ret = 1;
	if (setup_multipath(vecs, mpp) != 0)
		return 2;
	sync_map_state(mpp);

	return ret;
}

static int check_path_reinstate_state(struct path * pp) {
	struct timespec curr_time;

	/*
	 * This function is only called when the path state changes
	 * from "bad" to "good". pp->state reflects the *previous* state.
	 * If this was "bad", we know that a failure must have occurred
	 * beforehand, and count that.
	 * Note that we count path state _changes_ this way. If a path
	 * remains in "bad" state, failure count is not increased.
	 */

	if (!((pp->mpp->san_path_err_threshold > 0) &&
				(pp->mpp->san_path_err_forget_rate > 0) &&
				(pp->mpp->san_path_err_recovery_time >0))) {
		return 0;
	}

	if (pp->disable_reinstate) {
		/* If there are no other usable paths, reinstate the path */
		if (count_active_paths(pp->mpp) == 0) {
			condlog(2, "%s : reinstating path early", pp->dev);
			goto reinstate_path;
		}
		get_monotonic_time(&curr_time);

		/* If path became failed again or continue failed, should reset
		 * path san_path_err_forget_rate and path dis_reinstate_time to
		 * start a new stable check.
		 */
		if ((pp->state != PATH_UP) && (pp->state != PATH_GHOST) &&
			(pp->state != PATH_DELAYED)) {
			pp->san_path_err_forget_rate =
				pp->mpp->san_path_err_forget_rate;
			pp->dis_reinstate_time = curr_time.tv_sec;
		}

		if ((curr_time.tv_sec - pp->dis_reinstate_time ) > pp->mpp->san_path_err_recovery_time) {
			condlog(2,"%s : reinstate the path after err recovery time", pp->dev);
			goto reinstate_path;
		}
		return 1;
	}
	/* forget errors on a working path */
	if ((pp->state == PATH_UP || pp->state == PATH_GHOST) &&
			pp->path_failures > 0) {
		if (pp->san_path_err_forget_rate > 0){
			pp->san_path_err_forget_rate--;
		} else {
			/* for every san_path_err_forget_rate number of
			 * successful path checks decrement path_failures by 1
			 */
			pp->path_failures--;
			pp->san_path_err_forget_rate = pp->mpp->san_path_err_forget_rate;
		}
		return 0;
	}

	/* If the path isn't recovering from a failed state, do nothing */
	if (pp->state != PATH_DOWN && pp->state != PATH_SHAKY &&
			pp->state != PATH_TIMEOUT)
		return 0;

	if (pp->path_failures == 0)
		pp->san_path_err_forget_rate = pp->mpp->san_path_err_forget_rate;

	pp->path_failures++;

	/* if we don't know the currently time, we don't know how long to
	 * delay the path, so there's no point in checking if we should
	 */

	get_monotonic_time(&curr_time);
	/* when path failures has exceeded the san_path_err_threshold
	 * place the path in delayed state till san_path_err_recovery_time
	 * so that the customer can rectify the issue within this time. After
	 * the completion of san_path_err_recovery_time it should
	 * automatically reinstate the path
	 * (note: we know that san_path_err_threshold > 0 here).
	 */
	if (pp->path_failures > (unsigned int)pp->mpp->san_path_err_threshold) {
		condlog(2, "%s : hit error threshold. Delaying path reinstatement", pp->dev);
		pp->dis_reinstate_time = curr_time.tv_sec;
		pp->disable_reinstate = 1;

		return 1;
	} else {
		return 0;
	}

reinstate_path:
	pp->path_failures = 0;
	pp->disable_reinstate = 0;
	pp->san_path_err_forget_rate = 0;
	return 0;
}

static int
should_skip_path(struct path *pp){
	if (marginal_path_check_enabled(pp->mpp)) {
		if (pp->io_err_disable_reinstate && need_io_err_check(pp))
			return 1;
	} else if (san_path_check_enabled(pp->mpp)) {
		if (check_path_reinstate_state(pp))
			return 1;
	}
	return 0;
}

static void
start_path_check(struct path *pp)
{
	struct config *conf;

	if (path_sysfs_state(pp) ==  PATH_UP) {
		conf = get_multipath_config();
		pthread_cleanup_push(put_multipath_config, conf);
		start_checker(pp, conf, 1, PATH_UNCHECKED);
		pthread_cleanup_pop(1);
	} else {
		checker_clear_message(&pp->checker);
		condlog(3, "%s: state %s, checker not called",
			pp->dev, checker_state_name(pp->sysfs_state));
	}
}

static int
get_new_state(struct path *pp)
{
	int newstate = pp->sysfs_state;
	struct config *conf;

	if (newstate == PATH_UP)
		newstate = get_state(pp);
	/*
	 * Wait for uevent for removed paths;
	 * some LLDDs like zfcp keep paths unavailable
	 * without sending uevents.
	 */
	if (newstate == PATH_REMOVED)
		newstate = PATH_DOWN;

	if (newstate == PATH_WILD || newstate == PATH_UNCHECKED) {
		condlog(2, "%s: unusable path (%s) - checker failed",
			pp->dev, checker_state_name(newstate));
		LOG_MSG(2, pp);
		conf = get_multipath_config();
		pthread_cleanup_push(put_multipath_config, conf);
		pathinfo(pp, conf, 0);
		pthread_cleanup_pop(1);
	}
	return newstate;
}

static void
do_sync_mpp(struct vectors * vecs, struct multipath *mpp)
{
	int i, ret;
	struct path *pp;

	ret = update_multipath_strings(mpp, vecs->pathvec);
	if (ret != DMP_OK) {
		condlog(1, "%s: %s", mpp->alias, ret == DMP_NOT_FOUND ?
			"device not found" :
			"couldn't synchronize with kernel state");
		vector_foreach_slot (mpp->paths, pp, i)
			pp->dmstate = PSTATE_UNDEF;
		return;
	}
	set_no_path_retry(mpp);
}

static void
sync_mpp(struct vectors * vecs, struct multipath *mpp, unsigned int ticks)
{
	if (mpp->sync_tick)
		mpp->sync_tick -= (mpp->sync_tick > ticks) ? ticks :
				  mpp->sync_tick;
	if (mpp->sync_tick)
		return;

	do_sync_mpp(vecs, mpp);
}

static int
update_path_state (struct vectors * vecs, struct path * pp)
{
	int newstate;
	int new_path_up = 0;
	int chkr_new_path_up = 0;
	int disable_reinstate = 0;
	int oldchkrstate = pp->chkrstate;
	unsigned int checkint, max_checkint;
	struct config *conf;
	int marginal_pathgroups, marginal_changed = 0;
	bool need_reload;

	conf = get_multipath_config();
	checkint = conf->checkint;
	max_checkint = conf->max_checkint;
	marginal_pathgroups = conf->marginal_pathgroups;
	put_multipath_config(conf);

	newstate = get_new_state(pp);
	if (newstate == PATH_WILD || newstate == PATH_UNCHECKED)
		return CHECK_PATH_SKIPPED;
	/*
	 * Async IO in flight. Keep the previous path state
	 * and reschedule as soon as possible
	 */
	if (newstate == PATH_PENDING) {
		pp->tick = 1;
		return CHECK_PATH_SKIPPED;
	}
	if (pp->recheck_wwid == RECHECK_WWID_ON &&
	    (newstate == PATH_UP || newstate == PATH_GHOST) &&
	    ((pp->state != PATH_UP && pp->state != PATH_GHOST) ||
	     pp->dmstate == PSTATE_FAILED) &&
	    check_path_wwid_change(pp)) {
		condlog(0, "%s: path wwid change detected. Removing", pp->dev);
		return handle_path_wwid_change(pp, vecs)? CHECK_PATH_REMOVED :
							  CHECK_PATH_SKIPPED;
	}
	if (pp->mpp->synced_count == 0) {
		do_sync_mpp(vecs, pp->mpp);
		/* if update_multipath_strings orphaned the path, quit early */
		if (!pp->mpp)
			return CHECK_PATH_SKIPPED;
	}
	if ((newstate != PATH_UP && newstate != PATH_GHOST &&
	     newstate != PATH_PENDING) && (pp->state == PATH_DELAYED)) {
		/* If path state become failed again cancel path delay state */
		pp->state = newstate;
		/*
		 * path state bad again should change the check interval time
		 * to the shortest delay
		 */
		pp->checkint = checkint;
		return CHECK_PATH_CHECKED;
	}
	if ((newstate == PATH_UP || newstate == PATH_GHOST) &&
	    (san_path_check_enabled(pp->mpp) ||
	     marginal_path_check_enabled(pp->mpp))) {
		if (should_skip_path(pp)) {
			if (!pp->marginal && pp->state != PATH_DELAYED)
				condlog(2, "%s: path is now marginal", pp->dev);
			if (!marginal_pathgroups) {
				if (marginal_path_check_enabled(pp->mpp))
					/* to reschedule as soon as possible,
					 * so that this path can be recovered
					 * in time */
					pp->tick = 1;
				pp->state = PATH_DELAYED;
				return CHECK_PATH_CHECKED;
			}
			if (!pp->marginal) {
				pp->marginal = 1;
				marginal_changed = 1;
			}
		} else {
			if (pp->marginal || pp->state == PATH_DELAYED)
				condlog(2, "%s: path is no longer marginal",
					pp->dev);
			if (marginal_pathgroups && pp->marginal) {
				pp->marginal = 0;
				marginal_changed = 1;
			}
		}
	}

	/*
	 * don't reinstate failed path, if its in stand-by
	 * and if target supports only implicit tpgs mode.
	 * this will prevent unnecessary i/o by dm on stand-by
	 * paths if there are no other active paths in map.
	 */
	disable_reinstate = (newstate == PATH_GHOST &&
			     count_active_paths(pp->mpp) == 0 &&
			     path_get_tpgs(pp) == TPGS_IMPLICIT) ? 1 : 0;

	pp->chkrstate = newstate;
	if (newstate != pp->state) {
		int oldstate = pp->state;
		pp->state = newstate;

		LOG_MSG(1, pp);

		/*
		 * upon state change, reset the checkint
		 * to the shortest delay
		 */
		pp->checkint = checkint;

		if (newstate != PATH_UP && newstate != PATH_GHOST) {
			/*
			 * proactively fail path in the DM
			 */
			if (oldstate == PATH_UP ||
			    oldstate == PATH_GHOST)
				fail_path(pp, 1);
			else
				fail_path(pp, 0);

			/*
			 * cancel scheduled failback
			 */
			pp->mpp->failback_tick = 0;

			pp->mpp->stat_path_failures++;
			return CHECK_PATH_CHECKED;
		}

		if (newstate == PATH_UP || newstate == PATH_GHOST) {
			if (pp->mpp->prflag != PRFLAG_UNSET) {
				int prflag = pp->mpp->prflag;
				/*
				 * Check Persistent Reservation.
				 */
				condlog(2, "%s: checking persistent "
					"reservation registration", pp->dev);
				mpath_pr_event_handle(pp);
				if (pp->mpp->prflag == PRFLAG_SET &&
				    prflag != PRFLAG_SET)
					pr_register_active_paths(pp->mpp);
			}
		}

		/*
		 * reinstate this path
		 */
		if (!disable_reinstate)
			reinstate_path(pp);
		new_path_up = 1;

		if (oldchkrstate != PATH_UP && oldchkrstate != PATH_GHOST)
			chkr_new_path_up = 1;

		/*
		 * if at least one path is up in a group, and
		 * the group is disabled, re-enable it
		 */
		if (newstate == PATH_UP)
			enable_group(pp);
	}
	else if (newstate == PATH_UP || newstate == PATH_GHOST) {
		if ((pp->dmstate == PSTATE_FAILED ||
		    pp->dmstate == PSTATE_UNDEF) &&
		    !disable_reinstate)
			/* Clear IO errors */
			reinstate_path(pp);
		else {
			LOG_MSG(4, pp);
			if (pp->checkint != max_checkint) {
				/*
				 * double the next check delay.
				 * max at conf->max_checkint
				 */
				if (pp->checkint < (max_checkint / 2))
					pp->checkint = 2 * pp->checkint;
				else
					pp->checkint = max_checkint;

				condlog(4, "%s: delay next check %is",
					pp->dev_t, pp->checkint);
			}
		}
	}
	else if (newstate != PATH_UP && newstate != PATH_GHOST) {
		if (pp->dmstate == PSTATE_ACTIVE ||
		    pp->dmstate == PSTATE_UNDEF)
			fail_path(pp, 0);
		if (newstate == PATH_DOWN) {
			int log_checker_err;

			conf = get_multipath_config();
			log_checker_err = conf->log_checker_err;
			put_multipath_config(conf);
			if (log_checker_err == LOG_CHKR_ERR_ONCE)
				LOG_MSG(3, pp);
			else
				LOG_MSG(2, pp);
		}
	}

	pp->state = newstate;

	if (pp->mpp->wait_for_udev)
		return CHECK_PATH_CHECKED;
	/*
	 * path prio refreshing
	 */
	condlog(4, "path prio refresh");

	if (marginal_changed) {
		update_prio(pp, 1);
		reload_and_sync_map(pp->mpp, vecs);
	} else if (update_prio(pp, new_path_up) &&
		   pp->mpp->pgpolicyfn == (pgpolicyfn *)group_by_prio &&
		   pp->mpp->pgfailback == -FAILBACK_IMMEDIATE) {
		condlog(2, "%s: path priorities changed. reloading",
			pp->mpp->alias);
		reload_and_sync_map(pp->mpp, vecs);
	} else if (need_switch_pathgroup(pp->mpp, &need_reload)) {
		if (pp->mpp->pgfailback > 0 &&
		    (new_path_up || pp->mpp->failback_tick <= 0))
			pp->mpp->failback_tick = pp->mpp->pgfailback + 1;
		else if (pp->mpp->pgfailback == -FAILBACK_IMMEDIATE ||
			 (chkr_new_path_up && followover_should_failback(pp))) {
			if (need_reload)
				reload_and_sync_map(pp->mpp, vecs);
			else
				switch_pathgroup(pp->mpp);
		}
	}
	return CHECK_PATH_CHECKED;
}

static int
check_path (struct path * pp, unsigned int ticks)
{
	if (pp->initialized == INIT_REMOVED)
		return CHECK_PATH_SKIPPED;

	if (pp->tick)
		pp->tick -= (pp->tick > ticks) ? ticks : pp->tick;
	if (pp->tick)
		return CHECK_PATH_SKIPPED;

	if (pp->checkint == CHECKINT_UNDEF) {
		struct config *conf;

		condlog(0, "%s: BUG: checkint is not set", pp->dev);
		conf = get_multipath_config();
		pp->checkint = conf->checkint;
		put_multipath_config(conf);
	}

	start_path_check(pp);
	return CHECK_PATH_STARTED;
}

static int
update_path(struct vectors * vecs, struct path * pp, time_t start_secs)
{
	int r;
	unsigned int adjust_int, max_checkint;
	struct config *conf;
	time_t next_idx, goal_idx;

	r = update_path_state(vecs, pp);

	/*
	 * update_path_state() removed or orphaned the path.
	 */
	if (r == CHECK_PATH_REMOVED || !pp->mpp)
		return r;

	if (pp->tick != 0) {
		/* the path checker is pending */
		if (pp->state != PATH_DELAYED)
			pp->pending_ticks++;
		else
			pp->pending_ticks = 0;
		return r;
	}

	/* schedule the next check */
	pp->tick = pp->checkint;
	if (pp->pending_ticks >= pp->tick)
		pp->tick = 1;
	else
		pp->tick -= pp->pending_ticks;
	pp->pending_ticks = 0;

	if (pp->tick == 1)
		return r;

	conf = get_multipath_config();
	max_checkint = conf->max_checkint;
	adjust_int = conf->adjust_int;
	put_multipath_config(conf);
	/*
	 * every mpp has a goal_idx in the range of
	 * 0 <= goal_idx < conf->max_checkint
	 *
	 * The next check has an index, next_idx, in the range of
	 * 0 <= next_idx < conf->adjust_int
	 *
	 * If the difference between the goal index and the next check index
	 * is not a multiple of pp->checkint, then the device is not checking
	 * the paths at its goal index, and pp->tick will be decremented by
	 * one, to align it over time.
	 */
	goal_idx = (find_slot(vecs->mpvec, pp->mpp)) *
		   max_checkint / VECTOR_SIZE(vecs->mpvec);
	next_idx = (start_secs + pp->tick) % adjust_int;
	if ((goal_idx - next_idx) % pp->checkint != 0)
		pp->tick--;

	return r;
}

static int
check_uninitialized_path(struct path * pp, unsigned int ticks)
{
	int retrigger_tries;
	struct config *conf;

	if (pp->initialized != INIT_NEW && pp->initialized != INIT_FAILED &&
	    pp->initialized != INIT_MISSING_UDEV)
		return CHECK_PATH_SKIPPED;

	if (pp->tick)
		pp->tick -= (pp->tick > ticks) ? ticks : pp->tick;
	if (pp->tick)
		return CHECK_PATH_SKIPPED;

	conf = get_multipath_config();
	retrigger_tries = conf->retrigger_tries;
	pp->tick = conf->max_checkint;
	pp->checkint = conf->checkint;
	put_multipath_config(conf);

	if (pp->initialized == INIT_MISSING_UDEV) {
		if (pp->retriggers < retrigger_tries) {
			static const char change[] = "change";
			ssize_t ret;

			condlog(2, "%s: triggering change event to reinitialize",
				pp->dev);
			pp->initialized = INIT_REQUESTED_UDEV;
			pp->retriggers++;
			ret = sysfs_attr_set_value(pp->udev, "uevent", change,
						   sizeof(change) - 1);
			if (ret != sizeof(change) - 1)
				log_sysfs_attr_set_value(1, ret,
							 "%s: failed to trigger change event",
							 pp->dev);
			return CHECK_PATH_SKIPPED;
		} else {
			condlog(1, "%s: not initialized after %d udev retriggers",
				pp->dev, retrigger_tries);
			/*
			 * Make sure that the "add missing path" code path
			 * below may reinstate the path later, if it ever
			 * comes up again.
			 * The WWID needs not be cleared; if it was set, the
			 * state hadn't been INIT_MISSING_UDEV in the first
			 * place.
			 */
			pp->initialized = INIT_FAILED;
		}
	}

	start_path_check(pp);
	return CHECK_PATH_STARTED;
}

static int
update_uninitialized_path(struct vectors * vecs, struct path * pp)
{
	int newstate, ret;
	struct config *conf;

	if (pp->initialized != INIT_NEW && pp->initialized != INIT_FAILED &&
	    pp->initialized != INIT_MISSING_UDEV)
		return CHECK_PATH_SKIPPED;

	newstate = get_new_state(pp);

	if (!strlen(pp->wwid) &&
	    (pp->initialized == INIT_FAILED || pp->initialized == INIT_NEW) &&
	    (newstate == PATH_UP || newstate == PATH_GHOST)) {
		condlog(2, "%s: add missing path", pp->dev);
		conf = get_multipath_config();
		pthread_cleanup_push(put_multipath_config, conf);
		ret = pathinfo(pp, conf, DI_ALL | DI_BLACKLIST);
		pthread_cleanup_pop(1);
		/* INIT_OK implies ret == PATHINFO_OK */
		if (pp->initialized == INIT_OK) {
			ev_add_path(pp, vecs, 1);
			pp->tick = 1;
		} else if (ret == PATHINFO_SKIPPED) {
			int i;

			condlog(1, "%s: path blacklisted. removing", pp->dev);
			if ((i = find_slot(vecs->pathvec, (void *)pp)) != -1)
				vector_del_slot(vecs->pathvec, i);
			free_path(pp);
			return CHECK_PATH_REMOVED;
		}
	}
	return CHECK_PATH_CHECKED;
}

enum checker_state {
	CHECKER_STARTING,
	CHECKER_CHECKING_PATHS,
	CHECKER_UPDATING_PATHS,
	CHECKER_FINISHED,
};

static enum checker_state
check_paths(struct vectors *vecs, unsigned int ticks)
{
	unsigned int paths_checked = 0;
	struct timespec diff_time, start_time, end_time;
	struct path *pp;
	int i;

	get_monotonic_time(&start_time);

	vector_foreach_slot(vecs->pathvec, pp, i) {
		if (pp->is_checked != CHECK_PATH_UNCHECKED)
			continue;
		if (pp->mpp)
			pp->is_checked = check_path(pp, ticks);
		else
			pp->is_checked = check_uninitialized_path(pp, ticks);
		if (++paths_checked % 128 == 0 &&
		    (lock_has_waiters(&vecs->lock) || waiting_clients())) {
			get_monotonic_time(&end_time);
			timespecsub(&end_time, &start_time, &diff_time);
			if (diff_time.tv_sec > 0)
				return CHECKER_CHECKING_PATHS;
		}
	}
	return CHECKER_UPDATING_PATHS;
}

static enum checker_state
update_paths(struct vectors *vecs, int *num_paths_p, time_t start_secs)
{
	unsigned int paths_checked = 0;
	struct timespec diff_time, start_time, end_time;
	struct path *pp;
	int i, rc;

	get_monotonic_time(&start_time);

	vector_foreach_slot(vecs->pathvec, pp, i) {
		if (pp->is_checked != CHECK_PATH_STARTED)
			continue;
		if (pp->mpp)
			rc = update_path(vecs, pp, start_secs);
		else
			rc = update_uninitialized_path(vecs, pp);
		if (rc == CHECK_PATH_REMOVED)
			i--;
		else {
			pp->is_checked = rc;
			if (rc == CHECK_PATH_CHECKED)
				(*num_paths_p)++;
		}
		if (++paths_checked % 128 == 0 &&
		    (lock_has_waiters(&vecs->lock) || waiting_clients())) {
			get_monotonic_time(&end_time);
			timespecsub(&end_time, &start_time, &diff_time);
			if (diff_time.tv_sec > 0)
				return CHECKER_UPDATING_PATHS;
		}
	}
	return CHECKER_FINISHED;
}

static void *
checkerloop (void *ap)
{
	struct vectors *vecs;
	struct path *pp;
	int count = 0;
	struct timespec last_time;
	struct config *conf;
	int foreign_tick = 0;
#ifdef USE_SYSTEMD
	bool use_watchdog;
#endif

	pthread_cleanup_push(rcu_unregister, NULL);
	rcu_register_thread();
	mlockall(MCL_CURRENT | MCL_FUTURE);
	vecs = (struct vectors *)ap;

	/* Tweak start time for initial path check */
	get_monotonic_time(&last_time);
	last_time.tv_sec -= 1;

	/* use_watchdog is set from process environment and never changes */
	conf = get_multipath_config();
#ifdef USE_SYSTEMD
	use_watchdog = conf->use_watchdog;
#endif
	put_multipath_config(conf);

	while (1) {
		struct timespec diff_time, start_time, end_time;
		int num_paths = 0, strict_timing;
		unsigned int ticks = 0;
		enum checker_state checker_state = CHECKER_STARTING;

		if (set_config_state(DAEMON_RUNNING) != DAEMON_RUNNING)
			/* daemon shutdown */
			break;

		get_monotonic_time(&start_time);
		timespecsub(&start_time, &last_time, &diff_time);
		condlog(4, "tick (%ld.%06lu secs)",
			(long)diff_time.tv_sec, diff_time.tv_nsec / 1000);
		last_time = start_time;
		ticks = diff_time.tv_sec;
#ifdef USE_SYSTEMD
		if (use_watchdog)
			sd_notify(0, "WATCHDOG=1");
#endif
		while (checker_state != CHECKER_FINISHED) {
			struct multipath *mpp;
			int i;

			pthread_cleanup_push(cleanup_lock, &vecs->lock);
			lock(&vecs->lock);
			pthread_testcancel();
			vector_foreach_slot(vecs->mpvec, mpp, i)
				mpp->synced_count = 0;
			if (checker_state == CHECKER_STARTING) {
				vector_foreach_slot(vecs->mpvec, mpp, i)
					sync_mpp(vecs, mpp, ticks);
				vector_foreach_slot(vecs->pathvec, pp, i)
					pp->is_checked = CHECK_PATH_UNCHECKED;
				checker_state = CHECKER_CHECKING_PATHS;
			}
			if (checker_state == CHECKER_CHECKING_PATHS)
				checker_state = check_paths(vecs, ticks);
			if (checker_state == CHECKER_UPDATING_PATHS)
				checker_state = update_paths(vecs, &num_paths,
							     start_time.tv_sec);
			lock_cleanup_pop(vecs->lock);
			if (checker_state != CHECKER_FINISHED) {
				/* Yield to waiters */
				struct timespec wait = { .tv_nsec = 10000, };
				nanosleep(&wait, NULL);
			}
		}

		pthread_cleanup_push(cleanup_lock, &vecs->lock);
		lock(&vecs->lock);
		pthread_testcancel();
		deferred_failback_tick(vecs);
		retry_count_tick(vecs->mpvec);
		missing_uev_wait_tick(vecs);
		ghost_delay_tick(vecs);
		partial_retrigger_tick(vecs->pathvec);
		lock_cleanup_pop(vecs->lock);

		if (count)
			count--;
		else {
			pthread_cleanup_push(cleanup_lock, &vecs->lock);
			lock(&vecs->lock);
			pthread_testcancel();
			condlog(4, "map garbage collection");
			mpvec_garbage_collector(vecs);
			count = MAPGCINT;
			lock_cleanup_pop(vecs->lock);
		}

		get_monotonic_time(&end_time);
		timespecsub(&end_time, &start_time, &diff_time);
		if (num_paths) {
			unsigned int max_checkint;

			condlog(4, "checked %d path%s in %ld.%06lu secs",
				num_paths, num_paths > 1 ? "s" : "",
				(long)diff_time.tv_sec,
				diff_time.tv_nsec / 1000);
			conf = get_multipath_config();
			max_checkint = conf->max_checkint;
			put_multipath_config(conf);
			if (diff_time.tv_sec > (time_t)max_checkint)
				condlog(1, "path checkers took longer "
					"than %ld seconds, consider "
					"increasing max_polling_interval",
					(long)diff_time.tv_sec);
		}

		if (foreign_tick == 0) {
			conf = get_multipath_config();
			foreign_tick = conf->max_checkint;
			put_multipath_config(conf);
		}
		if (--foreign_tick == 0)
			check_foreign();

		post_config_state(DAEMON_IDLE);
		conf = get_multipath_config();
		strict_timing = conf->strict_timing;
		put_multipath_config(conf);
		if (!strict_timing)
			sleep(1);
		else {
			diff_time.tv_sec = 0;
			diff_time.tv_nsec =
			     1000UL * 1000 * 1000 - diff_time.tv_nsec;
			normalize_timespec(&diff_time);

			condlog(3, "waiting for %ld.%06lu secs",
				(long)diff_time.tv_sec,
				diff_time.tv_nsec / 1000);
			if (nanosleep(&diff_time, NULL) != 0) {
				condlog(3, "nanosleep failed with error %d",
					errno);
				conf = get_multipath_config();
				conf->strict_timing = 0;
				put_multipath_config(conf);
				break;
			}
		}
	}
	pthread_cleanup_pop(1);
	return NULL;
}

static int
configure (struct vectors * vecs, enum force_reload_types reload_type)
{
	struct multipath * mpp;
	struct path * pp;
	vector mpvec;
	int i, ret;
	struct config *conf;

	if (!vecs->pathvec && !(vecs->pathvec = vector_alloc())) {
		condlog(0, "couldn't allocate path vec in configure");
		return 1;
	}

	if (!vecs->mpvec && !(vecs->mpvec = vector_alloc())) {
		condlog(0, "couldn't allocate multipath vec in configure");
		return 1;
	}

	if (!(mpvec = vector_alloc())) {
		condlog(0, "couldn't allocate new maps vec in configure");
		return 1;
	}

	/*
	 * probe for current path (from sysfs) and map (from dm) sets
	 */
	ret = path_discovery(vecs->pathvec, DI_ALL);
	if (ret < 0) {
		condlog(0, "configure failed at path discovery");
		goto fail;
	}

	if (should_exit())
		goto fail;

	conf = get_multipath_config();
	pthread_cleanup_push(put_multipath_config, conf);
	vector_foreach_slot (vecs->pathvec, pp, i){
		if (filter_path(conf, pp) > 0){
			vector_del_slot(vecs->pathvec, i);
			free_path(pp);
			i--;
		}
	}
	pthread_cleanup_pop(1);

	if (map_discovery(vecs)) {
		condlog(0, "configure failed at map discovery");
		goto fail;
	}

	if (should_exit())
		goto fail;

	ret = coalesce_paths(vecs, mpvec, NULL, reload_type, CMD_NONE);
	if (ret != CP_OK) {
		condlog(0, "configure failed while coalescing paths");
		goto fail;
	}

	if (should_exit())
		goto fail;

	/*
	 * may need to remove some maps which are no longer relevant
	 * e.g., due to blacklist changes in conf file
	 */
	if (coalesce_maps(vecs, mpvec)) {
		condlog(0, "configure failed while coalescing maps");
		goto fail;
	}

	if (should_exit())
		goto fail;

	sync_maps_state(mpvec);
	vector_foreach_slot(mpvec, mpp, i){
		if (remember_wwid(mpp->wwid) == 1)
			trigger_paths_udev_change(mpp, true);
		update_map_pr(mpp);
		if (mpp->prflag == PRFLAG_SET)
			pr_register_active_paths(mpp);
	}

	/*
	 * purge dm of old maps and save new set of maps formed by
	 * considering current path state
	 */
	remove_maps(vecs);
	vecs->mpvec = mpvec;

	/*
	 * start dm event waiter threads for these new maps
	 */
	vector_foreach_slot(vecs->mpvec, mpp, i) {
		if (wait_for_events(mpp, vecs)) {
			remove_map(mpp, vecs->pathvec, vecs->mpvec);
			i--;
			continue;
		}
		if (setup_multipath(vecs, mpp))
			i--;
	}
	return 0;

fail:
	vector_free(mpvec);
	return 1;
}

int
need_to_delay_reconfig(struct vectors * vecs)
{
	struct multipath *mpp;
	int i;

	if (!VECTOR_SIZE(vecs->mpvec))
		return 0;

	vector_foreach_slot(vecs->mpvec, mpp, i) {
		if (mpp->wait_for_udev)
			return 1;
	}
	return 0;
}

void rcu_free_config(struct rcu_head *head)
{
	struct config *conf = container_of(head, struct config, rcu);

	free_config(conf);
}

static bool reconfigure_check_uid_attrs(const struct vector_s *old_attrs,
					const struct vector_s *new_attrs)
{
	int i;
	char *old;

	if (VECTOR_SIZE(old_attrs) != VECTOR_SIZE(new_attrs))
		return true;

	vector_foreach_slot(old_attrs, old, i) {
		char *new = VECTOR_SLOT(new_attrs, i);

		if (strcmp(old, new))
			return true;
	}

	return false;
}

static void reconfigure_check(struct config *old, struct config *new)
{
	int old_marginal_pathgroups;

	old_marginal_pathgroups = old->marginal_pathgroups;
	if ((old_marginal_pathgroups == MARGINAL_PATHGROUP_FPIN) !=
	    (new->marginal_pathgroups == MARGINAL_PATHGROUP_FPIN)) {
		condlog(1, "multipathd must be restarted to turn %s fpin marginal paths",
			(old_marginal_pathgroups == MARGINAL_PATHGROUP_FPIN)?
			"off" : "on");
		new->marginal_pathgroups = old_marginal_pathgroups;
	}

	if (reconfigure_check_uid_attrs(&old->uid_attrs, &new->uid_attrs)) {
		int i;
		void *ptr;

		condlog(1, "multipathd must be restarted to change uid_attrs, keeping old values");
		vector_foreach_slot(&new->uid_attrs, ptr, i)
			free(ptr);
		vector_reset(&new->uid_attrs);
		new->uid_attrs = old->uid_attrs;

		/* avoid uid_attrs being freed in rcu_free_config() */
		old->uid_attrs.allocated = 0;
		old->uid_attrs.slot = NULL;
	}
}

static int
reconfigure (struct vectors *vecs, enum force_reload_types reload_type)
{
	struct config * old, *conf;

	conf = load_config(DEFAULT_CONFIGFILE);
	if (!conf)
		return 1;

	if (verbosity)
		libmp_verbosity = verbosity;
	setlogmask(LOG_UPTO(libmp_verbosity + 3));
	condlog(2, "%s: setting up paths and maps", __func__);

	/*
	 * free old map and path vectors ... they use old conf state
	 */
	if (VECTOR_SIZE(vecs->mpvec))
		remove_maps_and_stop_waiters(vecs);

	free_pathvec(vecs->pathvec, FREE_PATHS);
	vecs->pathvec = NULL;
	delete_all_foreign();

	reset_checker_classes();
	if (bindings_read_only)
		conf->bindings_read_only = bindings_read_only;

	if (check_alias_settings(conf))
		return 1;

	uxsock_timeout = conf->uxsock_timeout;

	old = rcu_dereference(multipath_conf);
	reconfigure_check(old, conf);

	conf->sequence_nr = old->sequence_nr + 1;
	rcu_assign_pointer(multipath_conf, conf);
	call_rcu(&old->rcu, rcu_free_config);
#ifdef FPIN_EVENT_HANDLER
	fpin_clean_marginal_dev_list(NULL);
#endif
	configure(vecs, reload_type);

	return 0;
}

static struct vectors *
init_vecs (void)
{
	struct vectors * vecs;

	vecs = (struct vectors *)calloc(1, sizeof(struct vectors));

	if (!vecs)
		return NULL;

	init_lock(&vecs->lock);

	return vecs;
}

static void *
signal_set(int signo, void (*func) (int))
{
	int r;
	struct sigaction sig;
	struct sigaction osig;

	sig.sa_handler = func;
	sigemptyset(&sig.sa_mask);
	sig.sa_flags = 0;

	r = sigaction(signo, &sig, &osig);

	if (r < 0)
		return (SIG_ERR);
	else
		return (osig.sa_handler);
}

void
handle_signals(bool nonfatal)
{
	if (exit_sig) {
		condlog(3, "exit (signal)");
		exit_sig = 0;
		exit_daemon();
	}
	if (!nonfatal)
		return;
	if (reconfig_sig) {
		condlog(3, "reconfigure (signal)");
		schedule_reconfigure(FORCE_RELOAD_WEAK);
	}
	if (log_reset_sig) {
		condlog(3, "reset log (signal)");
		if (logsink == LOGSINK_SYSLOG)
			log_thread_reset();
	}
	reconfig_sig = 0;
	log_reset_sig = 0;
}

static void
sighup(__attribute__((unused)) int sig)
{
	reconfig_sig = 1;
}

static void
sigend(__attribute__((unused)) int sig)
{
	exit_sig = 1;
}

static void
sigusr1(__attribute__((unused)) int sig)
{
	log_reset_sig = 1;
}

static void
sigusr2(__attribute__((unused)) int sig)
{
	condlog(3, "SIGUSR2 received");
}

static void
signal_init(void)
{
	sigset_t set;

	/* block all signals */
	sigfillset(&set);
	/* SIGPIPE occurs if logging fails */
	sigdelset(&set, SIGPIPE);
	pthread_sigmask(SIG_SETMASK, &set, NULL);

	/* Other signals will be unblocked in the uxlsnr thread */
	signal_set(SIGHUP, sighup);
	signal_set(SIGUSR1, sigusr1);
	signal_set(SIGUSR2, sigusr2);
	signal_set(SIGINT, sigend);
	signal_set(SIGTERM, sigend);
	signal_set(SIGPIPE, sigend);
}

static void
setscheduler (void)
{
	int res;
	static struct sched_param sched_param;
	struct rlimit rlim;

	if (getrlimit(RLIMIT_RTPRIO, &rlim) < 0 || rlim.rlim_max == 0)
		return;

	sched_param.sched_priority = rlim.rlim_max > INT_MAX ? INT_MAX :
				     rlim.rlim_max;
	res = sched_get_priority_max(SCHED_RR);
	if (res > 0 && res < sched_param.sched_priority)
		sched_param.sched_priority = res;

	res = sched_setscheduler(0, SCHED_RR, &sched_param);

	if (res == -1)
		condlog(2, "Could not set SCHED_RR at priority %d",
			sched_param.sched_priority);
	return;
}

static void set_oom_adj(void)
{
	FILE *fp;

	if (getenv("OOMScoreAdjust")) {
		condlog(3, "Using systemd provided OOMScoreAdjust");
		return;
	}
#ifdef OOM_SCORE_ADJ_MIN
	fp = fopen("/proc/self/oom_score_adj", "w");
	if (fp) {
		fprintf(fp, "%i", OOM_SCORE_ADJ_MIN);
		fclose(fp);
		return;
	}
#endif
	fp = fopen("/proc/self/oom_adj", "w");
	if (fp) {
		fprintf(fp, "%i", OOM_ADJUST_MIN);
		fclose(fp);
		return;
	}
	condlog(0, "couldn't adjust oom score");
}

static void cleanup_pidfile(void)
{
	if (pid_fd >= 0)
		close(pid_fd);
	condlog(3, "unlink pidfile");
	unlink(DEFAULT_PIDFILE);
}

static void cleanup_conf(void) {
	struct config *conf;

	conf = rcu_dereference(multipath_conf);
	if (!conf)
		return;
	rcu_assign_pointer(multipath_conf, NULL);
	call_rcu(&conf->rcu, rcu_free_config);
}

static void cleanup_maps(struct vectors *vecs)
{
	int queue_without_daemon, i;
	struct multipath *mpp;
	struct config *conf;

	conf = get_multipath_config();
	queue_without_daemon = conf->queue_without_daemon;
	put_multipath_config(conf);
	if (queue_without_daemon == QUE_NO_DAEMON_OFF)
		vector_foreach_slot(vecs->mpvec, mpp, i)
			dm_queue_if_no_path(mpp, 0);
	remove_maps_and_stop_waiters(vecs);
	vecs->mpvec = NULL;
}

static void cleanup_paths(struct vectors *vecs)
{
	free_pathvec(vecs->pathvec, FREE_PATHS);
	vecs->pathvec = NULL;
}

static void cleanup_vecs(void)
{
	if (!gvecs)
		return;
	/*
	 * We can't take the vecs lock here, because exit() may
	 * have been called from the child() thread, holding the lock already.
	 * Anyway, by the time we get here, all threads that might access
	 * vecs should have been joined already (in cleanup_threads).
	 */
	cleanup_maps(gvecs);
	cleanup_paths(gvecs);
	pthread_mutex_destroy(&gvecs->lock.mutex);
	free(gvecs);
	gvecs = NULL;
}

static void cleanup_threads(void)
{
	stop_io_err_stat_thread();

	if (check_thr_started)
		pthread_cancel(check_thr);
	if (uevent_thr_started)
		pthread_cancel(uevent_thr);
	if (uxlsnr_thr_started)
		pthread_cancel(uxlsnr_thr);
	if (uevq_thr_started)
		pthread_cancel(uevq_thr);
	if (dmevent_thr_started)
		pthread_cancel(dmevent_thr);
	if (fpin_thr_started)
		pthread_cancel(fpin_thr);
	if (fpin_consumer_thr_started)
		pthread_cancel(fpin_consumer_thr);


	if (check_thr_started)
		pthread_join(check_thr, NULL);
	if (uevent_thr_started)
		pthread_join(uevent_thr, NULL);
	if (uxlsnr_thr_started)
		pthread_join(uxlsnr_thr, NULL);
	if (uevq_thr_started)
		pthread_join(uevq_thr, NULL);
	if (dmevent_thr_started)
		pthread_join(dmevent_thr, NULL);
	if (fpin_thr_started)
		pthread_join(fpin_thr, NULL);
	if (fpin_consumer_thr_started)
		pthread_join(fpin_consumer_thr, NULL);


	/*
	 * As all threads are joined now, and we're in DAEMON_SHUTDOWN
	 * state, no new waiter threads will be created anymore.
	 */
	pthread_attr_destroy(&waiter_attr);
}

#ifndef URCU_VERSION
#  define URCU_VERSION 0
#endif
#if (URCU_VERSION >= 0x000800)
/*
 * Use a non-default call_rcu_data for child().
 *
 * We do this to avoid a memory leak from liburcu.
 * liburcu never frees the default rcu handler (see comments on
 * call_rcu_data_free() in urcu-call-rcu-impl.h), its thread
 * can't be joined with pthread_join(), leaving a memory leak.
 *
 * Therefore we create our own, which can be destroyed and joined.
 * The cleanup handler needs to call rcu_barrier(), which is only
 * available in user-space RCU v0.8 and newer. See
 * https://lists.lttng.org/pipermail/lttng-dev/2021-May/029958.html
 */
static struct call_rcu_data *setup_rcu(void)
{
	struct call_rcu_data *crdp;

	rcu_init();
	rcu_register_thread();
	crdp = create_call_rcu_data(0UL, -1);
	if (crdp != NULL)
		set_thread_call_rcu_data(crdp);
	return crdp;
}

static struct call_rcu_data *mp_rcu_data;

static void cleanup_rcu(void)
{
	pthread_t rcu_thread;

	/* Wait for any pending RCU calls */
	rcu_barrier();
	if (mp_rcu_data != NULL) {
		rcu_thread = get_call_rcu_thread(mp_rcu_data);
		/* detach this thread from the RCU thread */
		set_thread_call_rcu_data(NULL);
		synchronize_rcu();
		/* tell RCU thread to exit */
		call_rcu_data_free(mp_rcu_data);
		pthread_join(rcu_thread, NULL);
	}
	rcu_unregister_thread();
}
#endif /* URCU_VERSION */

static void cleanup_child(void)
{
	cleanup_threads();
	cleanup_vecs();
	cleanup_bindings();
	if (poll_dmevents)
		cleanup_dmevent_waiter();

	cleanup_pidfile();
	if (logsink == LOGSINK_SYSLOG)
		log_thread_stop();

	cleanup_conf();
}

static int sd_notify_exit(int err)
{
#ifdef USE_SYSTEMD
	char msg[24];

	snprintf(msg, sizeof(msg), "ERRNO=%d", err);
	sd_notify(0, msg);
#endif
	return err;
}

static int
child (__attribute__((unused)) void *param)
{
	pthread_attr_t log_attr, misc_attr, uevent_attr;
	struct vectors * vecs;
	int rc;
	struct config *conf;
	char *envp;
	enum daemon_status state;
	int exit_code = 1;
	int fpin_marginal_paths = 0;

	init_unwinder();
	mlockall(MCL_CURRENT | MCL_FUTURE);
	signal_init();
#if (URCU_VERSION >= 0x000800)
	mp_rcu_data = setup_rcu();
	if (atexit(cleanup_rcu))
		fprintf(stderr, "failed to register RCU cleanup handler\n");
#else
	rcu_init();
#endif
	if (atexit(cleanup_child))
		fprintf(stderr, "failed to register cleanup handlers\n");

	setup_thread_attr(&misc_attr, 64 * 1024, 0);
	setup_thread_attr(&uevent_attr, DEFAULT_UEVENT_STACKSIZE * 1024, 0);
	setup_thread_attr(&waiter_attr, 32 * 1024, 1);

	if (logsink == LOGSINK_SYSLOG) {
		setup_thread_attr(&log_attr, 64 * 1024, 0);
		log_thread_start(&log_attr);
		pthread_attr_destroy(&log_attr);
	}
	pid_fd = pidfile_create(DEFAULT_PIDFILE, daemon_pid);
	if (pid_fd < 0) {
		condlog(1, "failed to create pidfile");
		exit(1);
	}

	post_config_state(DAEMON_START);

	condlog(2, "multipathd v%d.%d.%d%s: start up",
		MULTIPATH_VERSION(VERSION_CODE), EXTRAVERSION);
	condlog(3, "read " DEFAULT_CONFIGFILE);

	if (verbosity)
		libmp_verbosity = verbosity;
	conf = load_config(DEFAULT_CONFIGFILE);
	if (verbosity)
		libmp_verbosity = verbosity;
	setlogmask(LOG_UPTO(libmp_verbosity + 3));

	if (!conf) {
		condlog(0, "failed to load configuration");
		goto failed;
	}

	if (bindings_read_only)
		conf->bindings_read_only = bindings_read_only;
	uxsock_timeout = conf->uxsock_timeout;
	rcu_assign_pointer(multipath_conf, conf);
	if (init_checkers()) {
		condlog(0, "failed to initialize checkers");
		goto failed;
	}
	if (init_prio()) {
		condlog(0, "failed to initialize prioritizers");
		goto failed;
	}
	/* Failing this is non-fatal */

	init_foreign(conf->enable_foreign);

	if (poll_dmevents)
		poll_dmevents = dmevent_poll_supported();

	envp = getenv("LimitNOFILE");

	if (envp)
		condlog(2,"Using systemd provided open fds limit of %s", envp);
	else
		set_max_fds(conf->max_fds);

	vecs = gvecs = init_vecs();
	if (!vecs)
		goto failed;

	setscheduler();
	set_oom_adj();
#ifdef FPIN_EVENT_HANDLER
	if (conf->marginal_pathgroups == MARGINAL_PATHGROUP_FPIN)
		fpin_marginal_paths = 1;
#endif
	/*
	 * Startup done, invalidate configuration
	 */
	conf = NULL;

	pthread_cleanup_push(config_cleanup, NULL);
	pthread_mutex_lock(&config_lock);

	rc = pthread_create(&uxlsnr_thr, &misc_attr, uxlsnrloop, vecs);
	if (!rc) {
		/* Wait for uxlsnr startup */
		while (running_state == DAEMON_START)
			pthread_cond_wait(&config_cond, &config_lock);
		state = running_state;
	}
	pthread_cleanup_pop(1);

	if (rc) {
		condlog(0, "failed to create cli listener: %d", rc);
		goto failed;
	}
	else {
		uxlsnr_thr_started = true;
		if (state != DAEMON_CONFIGURE) {
			condlog(0, "cli listener failed to start");
			goto failed;
		}
	}

	if (poll_dmevents) {
		if (init_dmevent_waiter(vecs)) {
			condlog(0, "failed to allocate dmevents waiter info");
			goto failed;
		}
		if ((rc = pthread_create(&dmevent_thr, &misc_attr,
					 wait_dmevents, NULL))) {
			condlog(0, "failed to create dmevent waiter thread: %d",
				rc);
			goto failed;
		} else
			dmevent_thr_started = true;
	}

	/*
	 * Start uevent listener early to catch events
	 */
	if ((rc = pthread_create(&uevent_thr, &uevent_attr, ueventloop, udev))) {
		condlog(0, "failed to create uevent thread: %d", rc);
		goto failed;
	} else
		uevent_thr_started = true;
	pthread_attr_destroy(&uevent_attr);

	/*
	 * start threads
	 */
	if ((rc = pthread_create(&check_thr, &misc_attr, checkerloop, vecs))) {
		condlog(0,"failed to create checker loop thread: %d", rc);
		goto failed;
	} else
		check_thr_started = true;
	if ((rc = pthread_create(&uevq_thr, &misc_attr, uevqloop, vecs))) {
		condlog(0, "failed to create uevent dispatcher: %d", rc);
		goto failed;
	} else
		uevq_thr_started = true;

	if (fpin_marginal_paths) {
		if ((rc = pthread_create(&fpin_thr, &misc_attr,
			fpin_fabric_notification_receiver, NULL))) {
			condlog(0, "failed to create the fpin receiver thread: %d", rc);
			goto failed;
		} else
			fpin_thr_started = true;

		if ((rc = pthread_create(&fpin_consumer_thr,
			&misc_attr, fpin_els_li_consumer, vecs))) {
			condlog(0, "failed to create the fpin consumer thread thread: %d", rc);
			goto failed;
		} else
			fpin_consumer_thr_started = true;
	}
	pthread_attr_destroy(&misc_attr);

	while (1) {
		int rc = 0;

		pthread_cleanup_push(config_cleanup, NULL);
		pthread_mutex_lock(&config_lock);
		while (running_state != DAEMON_CONFIGURE &&
		       running_state != DAEMON_SHUTDOWN &&
		       /*
			* Check if another reconfigure request was scheduled
			* while we last ran reconfigure().
			* We have to test delayed_reconfig here
			* to avoid a busy loop
			*/
		       (reconfigure_pending == FORCE_RELOAD_NONE
			 || delayed_reconfig))
			pthread_cond_wait(&config_cond, &config_lock);

		if (running_state != DAEMON_CONFIGURE &&
		    running_state != DAEMON_SHUTDOWN)
			/* This sets running_state to DAEMON_CONFIGURE */
			post_config_state__(DAEMON_CONFIGURE);
		state = running_state;
		pthread_cleanup_pop(1);
		if (state == DAEMON_SHUTDOWN)
			break;

		/* handle DAEMON_CONFIGURE */
		pthread_cleanup_push(cleanup_lock, &vecs->lock);
		lock(&vecs->lock);
		pthread_testcancel();
		if (!need_to_delay_reconfig(vecs)) {
			enum force_reload_types reload_type;

			pthread_mutex_lock(&config_lock);
			reload_type = reconfigure_pending == FORCE_RELOAD_YES ?
				FORCE_RELOAD_YES : FORCE_RELOAD_WEAK;
			reconfigure_pending = FORCE_RELOAD_NONE;
			delayed_reconfig = false;
			pthread_mutex_unlock(&config_lock);

			rc = reconfigure(vecs, reload_type);
		} else {
			pthread_mutex_lock(&config_lock);
			delayed_reconfig = true;
			pthread_mutex_unlock(&config_lock);
			condlog(3, "delaying reconfigure()");
		}
		lock_cleanup_pop(vecs->lock);
		if (!rc)
			post_config_state(DAEMON_IDLE);
		else {
			condlog(0, "fatal error applying configuration - aborting");
			exit_daemon();
		}
	}

	exit_code = 0;
failed:
	condlog(2, "multipathd: shut down");
	/* All cleanup is done in the cleanup_child() exit handler */
	return sd_notify_exit(exit_code);
}

static void cleanup_close(int *pfd)
{
	if (*pfd != -1 && *pfd != STDIN_FILENO && *pfd != STDOUT_FILENO &&
	    *pfd != STDERR_FILENO)
		close(*pfd);
}

static int
daemonize(void)
{
	int pid;
	int dev_null_fd __attribute__((cleanup(cleanup_close))) = -1;

	if( (pid = fork()) < 0){
		fprintf(stderr, "Failed first fork : %s\n", strerror(errno));
		return -1;
	}
	else if (pid != 0)
		return pid;

	setsid();

	if ( (pid = fork()) < 0)
		fprintf(stderr, "Failed second fork : %s\n", strerror(errno));
	else if (pid != 0)
		_exit(0);

	if (chdir("/") < 0)
		fprintf(stderr, "cannot chdir to '/', continuing\n");

	dev_null_fd = open("/dev/null", O_RDWR);
	if (dev_null_fd < 0){
		fprintf(stderr, "cannot open /dev/null for input & output : %s\n",
			strerror(errno));
		_exit(0);
	}

	if (dup2(dev_null_fd, STDIN_FILENO) < 0) {
		fprintf(stderr, "cannot dup2 /dev/null to stdin : %s\n",
			strerror(errno));
		_exit(0);
	}
	if (dup2(dev_null_fd, STDOUT_FILENO) < 0) {
		fprintf(stderr, "cannot dup2 /dev/null to stdout : %s\n",
			strerror(errno));
		_exit(0);
	}
	if (dup2(dev_null_fd, STDERR_FILENO) < 0) {
		fprintf(stderr, "cannot dup /dev/null to stderr : %s\n",
			strerror(errno));
		_exit(0);
	}
	daemon_pid = getpid();
	return 0;
}

int
main (int argc, char *argv[])
{
	extern char *optarg;
	extern int optind;
	int arg;
	int err = 0;
	int foreground = 0;
	struct config *conf;
	char *opt_k_arg = NULL;
	bool opt_k = false;

	ANNOTATE_BENIGN_RACE_SIZED(&multipath_conf, sizeof(multipath_conf),
				   "Manipulated through RCU");
	ANNOTATE_BENIGN_RACE_SIZED(&uxsock_timeout, sizeof(uxsock_timeout),
		"Suppress complaints about this scalar variable");

	logsink = LOGSINK_SYSLOG;

	/* make sure we don't lock any path */
	if (chdir("/") < 0)
		fprintf(stderr, "can't chdir to root directory : %s\n",
			strerror(errno));
	umask(umask(077) | 022);

	pthread_cond_init_mono(&config_cond);

	if (atexit(dm_lib_exit))
		condlog(3, "failed to register exit handler for libdm");

	libmultipath_init();
	if (atexit(libmultipath_exit))
		condlog(3, "failed to register exit handler for libmultipath");
	libmp_udev_set_sync_support(0);

	while ((arg = getopt(argc, argv, ":dsv:k::Bniw")) != EOF ) {
		switch(arg) {
		case 'd':
			foreground = 1;
			if (logsink == LOGSINK_SYSLOG)
				logsink = LOGSINK_STDERR_WITH_TIME;
			break;
		case 'v':
			if (sizeof(optarg) > sizeof(char *) ||
			    !isdigit(optarg[0]))
				exit(1);

			libmp_verbosity = verbosity = atoi(optarg);
			break;
		case 's':
			logsink = LOGSINK_STDERR_WITHOUT_TIME;
			break;
		case 'k':
			opt_k = true;
			opt_k_arg = optarg;
			break;
		case 'B':
			bindings_read_only = 1;
			break;
		case 'n':
			condlog(0, "WARNING: ignoring deprecated option -n, use 'ignore_wwids = no' instead");
			break;
		case 'w':
			poll_dmevents = 0;
			break;
		default:
			fprintf(stderr, "Invalid argument '-%c'\n",
				optopt);
			exit(1);
		}
	}
	if (opt_k || optind < argc) {
		char cmd[CMDSIZE];
		char * s = cmd;
		char * c = s;

		logsink = LOGSINK_STDERR_WITH_TIME;
		if (verbosity)
			libmp_verbosity = verbosity;
		conf = load_config(DEFAULT_CONFIGFILE);
		if (!conf)
			exit(1);
		if (verbosity)
			libmp_verbosity = verbosity;
		uxsock_timeout = conf->uxsock_timeout;
		memset(cmd, 0x0, CMDSIZE);
		if (opt_k)
			s = opt_k_arg;
		else {
			while (optind < argc) {
				if (strchr(argv[optind], ' '))
					c += snprintf(c, s + CMDSIZE - c,
						      "\"%s\" ", argv[optind]);
				else
					c += snprintf(c, s + CMDSIZE - c,
						      "%s ", argv[optind]);
				optind++;
				if (c >= s + CMDSIZE) {
					fprintf(stderr, "multipathd command too large\n");
					exit(1);
				}
			}
			c += snprintf(c, s + CMDSIZE - c, "\n");
		}
		if (!s) {
			char tmo_buf[16];

			snprintf(tmo_buf, sizeof(tmo_buf), "%d",
				 uxsock_timeout + 100);
			if (execl(BINDIR "/multipathc", "multipathc",
				  tmo_buf, NULL) == -1) {
				condlog(0, "ERROR: failed to execute multipathc: %m");
				err = 1;
			}
		} else
			err = uxclnt(s, uxsock_timeout + 100);
		free_config(conf);
		return err;
	}

	if (getuid() != 0) {
		fprintf(stderr, "need to be root\n");
		exit(1);
	}

	if (foreground) {
		if (!isatty(fileno(stdout)))
			setbuf(stdout, NULL);
		err = 0;
		daemon_pid = getpid();
	} else
		err = daemonize();

	if (err < 0)
		/* error */
		exit(1);
	else if (err > 0)
		/* parent dies */
		exit(0);
	else
		/* child lives */
		return (child(NULL));
}

void *  mpath_pr_event_handler_fn (void * pathp )
{
	struct multipath * mpp;
	unsigned int i;
	int ret, isFound;
	struct path * pp = (struct path *)pathp;
	struct prout_param_descriptor *param;
	struct prin_resp *resp;

	rcu_register_thread();
	mpp = pp->mpp;

	resp = mpath_alloc_prin_response(MPATH_PRIN_RKEY_SA);
	if (!resp){
		condlog(0,"%s Alloc failed for prin response", pp->dev);
		goto out;
	}

	mpp->prflag = PRFLAG_UNSET;
	ret = prin_do_scsi_ioctl(pp->dev, MPATH_PRIN_RKEY_SA, resp, 0);
	if (ret != MPATH_PR_SUCCESS )
	{
		condlog(0,"%s : pr in read keys service action failed. Error=%d", pp->dev, ret);
		goto out;
	}

	condlog(3, " event pr=%d addlen=%d",resp->prin_descriptor.prin_readkeys.prgeneration,
			resp->prin_descriptor.prin_readkeys.additional_length );

	if (resp->prin_descriptor.prin_readkeys.additional_length == 0 )
	{
		condlog(1, "%s: No key found. Device may not be registered.", pp->dev);
		goto out;
	}
	condlog(2, "Multipath  reservation_key: 0x%" PRIx64 " ",
		get_be64(mpp->reservation_key));

	isFound =0;
	for (i = 0; i < resp->prin_descriptor.prin_readkeys.additional_length/8; i++ )
	{
		condlog(2, "PR IN READKEYS[%d]  reservation key:",i);
		dumpHex((char *)&resp->prin_descriptor.prin_readkeys.key_list[i*8], 8 , -1);
		if (!memcmp(&mpp->reservation_key, &resp->prin_descriptor.prin_readkeys.key_list[i*8], 8))
		{
			condlog(2, "%s: pr key found in prin readkeys response", mpp->alias);
			isFound =1;
			break;
		}
	}
	if (!isFound)
	{
		condlog(0, "%s: Either device not registered or ", pp->dev);
		condlog(0, "host is not authorised for registration. Skip path");
		goto out;
	}

	param = (struct prout_param_descriptor *)calloc(1, sizeof(struct prout_param_descriptor));
	if (!param)
		goto out;

	param->sa_flags = mpp->sa_flags;
	memcpy(param->sa_key, &mpp->reservation_key, 8);
	param->num_transportid = 0;

	condlog(3, "device %s:%s", pp->dev, pp->mpp->wwid);

	ret = prout_do_scsi_ioctl(pp->dev, MPATH_PROUT_REG_IGN_SA, 0, 0, param, 0);
	if (ret != MPATH_PR_SUCCESS )
	{
		condlog(0,"%s: Reservation registration failed. Error: %d", pp->dev, ret);
	}
	mpp->prflag = PRFLAG_SET;

	free(param);
out:
	if (resp)
		free(resp);
	rcu_unregister_thread();
	return NULL;
}

int mpath_pr_event_handle(struct path *pp)
{
	pthread_t thread;
	int rc;
	pthread_attr_t attr;
	struct multipath * mpp;

	if (pp->bus != SYSFS_BUS_SCSI)
		goto no_pr;

	mpp = pp->mpp;

	if (!get_be64(mpp->reservation_key))
		goto no_pr;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	rc = pthread_create(&thread, NULL , mpath_pr_event_handler_fn, pp);
	if (rc) {
		condlog(0, "%s: ERROR; return code from pthread_create() is %d", pp->dev, rc);
		return -1;
	}
	pthread_attr_destroy(&attr);
	rc = pthread_join(thread, NULL);
	return 0;

no_pr:
	pp->mpp->prflag = PRFLAG_UNSET;
	return 0;
}
