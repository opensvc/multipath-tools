/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 * Copyright (c) 2005 Kiyoshi Ueda, NEC
 * Copyright (c) 2005 Benjamin Marzinski, Redhat
 * Copyright (c) 2005 Edward Goggin, EMC
 */
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

#ifdef USE_SYSTEMD
static int use_watchdog;
#endif

/*
 * libmultipath
 */
#include "parser.h"
#include "vector.h"
#include "memory.h"
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
#include "uevent.h"
#include "log.h"
#include "uxsock.h"

#include "mpath_cmd.h"
#include "mpath_persist.h"

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
#include "wwids.h"
#include "foreign.h"
#include "../third-party/valgrind/drd.h"

#define FILE_NAME_SIZE 256
#define CMDSIZE 160

#define LOG_MSG(lvl, verb, pp)					\
do {								\
	if (pp->mpp && checker_selected(&pp->checker) &&	\
	    lvl <= verb) {					\
		if (pp->offline)				\
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

int logsink;
int uxsock_timeout;
int verbosity;
int bindings_read_only;
int ignore_new_devs;
#ifdef NO_DMEVENTS_POLL
int poll_dmevents = 0;
#else
int poll_dmevents = 1;
#endif
/* Don't access this variable without holding config_lock */
enum daemon_status running_state = DAEMON_INIT;
pid_t daemon_pid;
pthread_mutex_t config_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t config_cond;

static inline enum daemon_status get_running_state(void)
{
	enum daemon_status st;

	pthread_mutex_lock(&config_lock);
	st = running_state;
	pthread_mutex_unlock(&config_lock);
	return st;
}

/*
 * global copy of vecs for use in sig handlers
 */
struct vectors * gvecs;

struct udev * udev;

struct config *multipath_conf;

/* Local variables */
static volatile sig_atomic_t exit_sig;
static volatile sig_atomic_t reconfig_sig;
static volatile sig_atomic_t log_reset_sig;

const char *
daemon_status(void)
{
	switch (get_running_state()) {
	case DAEMON_INIT:
		return "init";
	case DAEMON_START:
		return "startup";
	case DAEMON_CONFIGURE:
		return "configure";
	case DAEMON_IDLE:
		return "idle";
	case DAEMON_RUNNING:
		return "running";
	case DAEMON_SHUTDOWN:
		return "shutdown";
	}
	return NULL;
}

/*
 * I love you too, systemd ...
 */
static const char *
sd_notify_status(enum daemon_status state)
{
	switch (state) {
	case DAEMON_INIT:
		return "STATUS=init";
	case DAEMON_START:
		return "STATUS=startup";
	case DAEMON_CONFIGURE:
		return "STATUS=configure";
	case DAEMON_IDLE:
	case DAEMON_RUNNING:
		return "STATUS=up";
	case DAEMON_SHUTDOWN:
		return "STATUS=shutdown";
	}
	return NULL;
}

#ifdef USE_SYSTEMD
static void do_sd_notify(enum daemon_status old_state,
			 enum daemon_status new_state)
{
	/*
	 * Checkerloop switches back and forth between idle and running state.
	 * No need to tell systemd each time.
	 * These notifications cause a lot of overhead on dbus.
	 */
	if ((new_state == DAEMON_IDLE || new_state == DAEMON_RUNNING) &&
	    (old_state == DAEMON_IDLE || old_state == DAEMON_RUNNING))
		return;
	sd_notify(0, sd_notify_status(new_state));
}
#endif

static void config_cleanup(void *arg)
{
	pthread_mutex_unlock(&config_lock);
}

/*
 * If the current status is @oldstate, wait for at most @ms milliseconds
 * for the state to change, and return the new state, which may still be
 * @oldstate.
 */
enum daemon_status wait_for_state_change_if(enum daemon_status oldstate,
					    unsigned long ms)
{
	enum daemon_status st;
	struct timespec tmo;

	if (oldstate == DAEMON_SHUTDOWN)
		return DAEMON_SHUTDOWN;

	pthread_mutex_lock(&config_lock);
	pthread_cleanup_push(config_cleanup, NULL);
	st = running_state;
	if (st == oldstate && clock_gettime(CLOCK_MONOTONIC, &tmo) == 0) {
		tmo.tv_nsec += ms * 1000 * 1000;
		normalize_timespec(&tmo);
		(void)pthread_cond_timedwait(&config_cond, &config_lock, &tmo);
		st = running_state;
	}
	pthread_cleanup_pop(1);
	return st;
}

/* must be called with config_lock held */
static void __post_config_state(enum daemon_status state)
{
	if (state != running_state && running_state != DAEMON_SHUTDOWN) {
		enum daemon_status old_state = running_state;

		running_state = state;
		pthread_cond_broadcast(&config_cond);
#ifdef USE_SYSTEMD
		do_sd_notify(old_state, state);
#endif
	}
}

void post_config_state(enum daemon_status state)
{
	pthread_mutex_lock(&config_lock);
	pthread_cleanup_push(config_cleanup, NULL);
	__post_config_state(state);
	pthread_cleanup_pop(1);
}

int set_config_state(enum daemon_status state)
{
	int rc = 0;

	pthread_cleanup_push(config_cleanup, NULL);
	pthread_mutex_lock(&config_lock);
	if (running_state != state) {
		enum daemon_status old_state = running_state;

		if (running_state == DAEMON_SHUTDOWN)
			rc = EINVAL;
		else if (running_state != DAEMON_IDLE) {
			struct timespec ts;

			get_monotonic_time(&ts);
			ts.tv_sec += 1;
			rc = pthread_cond_timedwait(&config_cond,
						    &config_lock, &ts);
		}
		if (!rc && (running_state != DAEMON_SHUTDOWN)) {
			running_state = state;
			pthread_cond_broadcast(&config_cond);
#ifdef USE_SYSTEMD
			do_sd_notify(old_state, state);
#endif
		}
	}
	pthread_cleanup_pop(1);
	return rc;
}

struct config *get_multipath_config(void)
{
	rcu_read_lock();
	return rcu_dereference(multipath_conf);
}

void put_multipath_config(void *arg)
{
	rcu_read_unlock();
}

static int
need_switch_pathgroup (struct multipath * mpp, int refresh)
{
	struct pathgroup * pgp;
	struct path * pp;
	unsigned int i, j;
	struct config *conf;
	int bestpg;

	if (!mpp)
		return 0;

	/*
	 * Refresh path priority values
	 */
	if (refresh) {
		vector_foreach_slot (mpp->pg, pgp, i) {
			vector_foreach_slot (pgp->paths, pp, j) {
				conf = get_multipath_config();
				pthread_cleanup_push(put_multipath_config,
						     conf);
				pathinfo(pp, conf, DI_PRIO);
				pthread_cleanup_pop(1);
			}
		}
	}

	if (!mpp->pg || VECTOR_SIZE(mpp->paths) == 0)
		return 0;

	bestpg = select_path_group(mpp);
	if (mpp->pgfailback == -FAILBACK_MANUAL)
		return 0;

	mpp->bestpg = bestpg;
	if (mpp->bestpg != mpp->nextpg)
		return 1;

	return 0;
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
		stop_waiter_thread(mpp, vecs);
	remove_map(mpp, vecs, PURGE_VEC);
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
			stop_waiter_thread(mpp, vecs);
	}
	else
		unwatch_all_dmevents();

	remove_maps(vecs);
}

static void
set_multipath_wwid (struct multipath * mpp)
{
	if (strlen(mpp->wwid))
		return;

	dm_get_uuid(mpp->alias, mpp->wwid, WWID_SIZE);
}

static void set_no_path_retry(struct multipath *mpp)
{
	char is_queueing = 0;

	mpp->nr_active = pathcount(mpp, PATH_UP) + pathcount(mpp, PATH_GHOST);
	if (mpp->features && strstr(mpp->features, "queue_if_no_path"))
		is_queueing = 1;

	switch (mpp->no_path_retry) {
	case NO_PATH_RETRY_UNDEF:
		break;
	case NO_PATH_RETRY_FAIL:
		if (is_queueing)
			dm_queue_if_no_path(mpp->alias, 0);
		break;
	case NO_PATH_RETRY_QUEUE:
		if (!is_queueing)
			dm_queue_if_no_path(mpp->alias, 1);
		break;
	default:
		if (mpp->nr_active > 0) {
			mpp->retry_tick = 0;
			if (!is_queueing)
				dm_queue_if_no_path(mpp->alias, 1);
		} else if (is_queueing && mpp->retry_tick == 0)
			enter_recovery_mode(mpp);
		break;
	}
}

int __setup_multipath(struct vectors *vecs, struct multipath *mpp,
		      int reset)
{
	if (dm_get_info(mpp->alias, &mpp->dmi)) {
		/* Error accessing table */
		condlog(3, "%s: cannot access table", mpp->alias);
		goto out;
	}

	if (update_multipath_strings(mpp, vecs->pathvec, 1)) {
		condlog(0, "%s: failed to setup multipath", mpp->alias);
		goto out;
	}

	if (reset) {
		set_no_path_retry(mpp);
		if (VECTOR_SIZE(mpp->paths) != 0)
			dm_cancel_deferred_remove(mpp);
	}

	return 0;
out:
	remove_map_and_stop_waiter(mpp, vecs);
	return 1;
}

int update_multipath (struct vectors *vecs, char *mapname, int reset)
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

	if (__setup_multipath(vecs, mpp, reset))
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
				int checkint;

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

static int
update_map (struct multipath *mpp, struct vectors *vecs, int new_map)
{
	int retries = 3;
	char params[PARAMS_SIZE] = {0};

retry:
	condlog(4, "%s: updating new map", mpp->alias);
	if (adopt_paths(vecs->pathvec, mpp)) {
		condlog(0, "%s: failed to adopt paths for new map update",
			mpp->alias);
		retries = -1;
		goto fail;
	}
	verify_paths(mpp, vecs);
	mpp->action = ACT_RELOAD;

	if (setup_map(mpp, params, PARAMS_SIZE, vecs)) {
		condlog(0, "%s: failed to setup new map in update", mpp->alias);
		retries = -1;
		goto fail;
	}
	if (domap(mpp, params, 1) == DOMAP_FAIL && retries-- > 0) {
		condlog(0, "%s: map_udate sleep", mpp->alias);
		sleep(1);
		goto retry;
	}
	dm_lib_release();

fail:
	if (new_map && (retries < 0 || wait_for_events(mpp, vecs))) {
		condlog(0, "%s: failed to create new map", mpp->alias);
		remove_map(mpp, vecs, 1);
		return 1;
	}

	if (setup_multipath(vecs, mpp))
		return 1;

	sync_map_state(mpp);

	if (retries < 0)
		condlog(0, "%s: failed reload in new map update", mpp->alias);
	return 0;
}

static struct multipath *
add_map_without_path (struct vectors *vecs, const char *alias)
{
	struct multipath * mpp = alloc_multipath();
	struct config *conf;

	if (!mpp)
		return NULL;
	if (!alias) {
		FREE(mpp);
		return NULL;
	}

	mpp->alias = STRDUP(alias);

	if (dm_get_info(mpp->alias, &mpp->dmi)) {
		condlog(3, "%s: cannot access table", mpp->alias);
		goto out;
	}
	set_multipath_wwid(mpp);
	conf = get_multipath_config();
	mpp->mpe = find_mpe(conf->mptable, mpp->wwid);
	put_multipath_config(conf);

	if (update_multipath_table(mpp, vecs->pathvec, 1))
		goto out;
	if (update_multipath_status(mpp))
		goto out;

	if (!vector_alloc_slot(vecs->mpvec))
		goto out;

	vector_set_slot(vecs->mpvec, mpp);

	if (update_map(mpp, vecs, 1) != 0) /* map removed */
		return NULL;

	return mpp;
out:
	remove_map(mpp, vecs, PURGE_VEC);
	return NULL;
}

static int
coalesce_maps(struct vectors *vecs, vector nmpv)
{
	struct multipath * ompp;
	vector ompv = vecs->mpvec;
	unsigned int i, reassign_maps;
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
			if (dm_flush_map(ompp->alias)) {
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
			else {
				dm_lib_release();
				condlog(2, "%s devmap removed", ompp->alias);
			}
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

static int
flush_map(struct multipath * mpp, struct vectors * vecs, int nopaths)
{
	int r;

	if (nopaths)
		r = dm_flush_map_nopaths(mpp->alias, mpp->deferred_remove);
	else
		r = dm_flush_map(mpp->alias);
	/*
	 * clear references to this map before flushing so we can ignore
	 * the spurious uevent we may generate with the dm_flush_map call below
	 */
	if (r) {
		/*
		 * May not really be an error -- if the map was already flushed
		 * from the device mapper by dmsetup(8) for instance.
		 */
		if (r == 1)
			condlog(0, "%s: can't flush", mpp->alias);
		else {
			condlog(2, "%s: devmap deferred remove", mpp->alias);
			mpp->deferred_remove = DEFERRED_REMOVE_IN_PROGRESS;
		}
		return r;
	}
	else {
		dm_lib_release();
		condlog(2, "%s: map flushed", mpp->alias);
	}

	orphan_paths(vecs->pathvec, mpp, "map flushed");
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
	FREE(alias);
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
	int delayed_reconfig, reassign_maps;
	struct config *conf;

	if (dm_is_mpath(alias) != 1) {
		condlog(4, "%s: not a multipath map", alias);
		return 0;
	}

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
		delayed_reconfig = conf->delayed_reconfig;
		reassign_maps = conf->reassign_maps;
		put_multipath_config(conf);
		if (mpp->wait_for_udev) {
			mpp->wait_for_udev = 0;
			if (delayed_reconfig &&
			    !need_to_delay_reconfig(vecs)) {
				condlog(2, "reconfigure (delayed)");
				set_config_state(DAEMON_CONFIGURE);
				return 0;
			}
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
	if ((mpp = add_map_without_path(vecs, alias))) {
		sync_map_state(mpp);
		condlog(2, "%s: devmap %s registered", alias, dev);
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

	remove_map_and_stop_waiter(mpp, vecs);
out:
	lock_cleanup_pop(vecs->lock);
	FREE(alias);
	return 0;
}

/* Called from CLI handler */
int
ev_remove_map (char * devname, char * alias, int minor, struct vectors * vecs)
{
	struct multipath * mpp;

	mpp = find_mp_by_minor(vecs->mpvec, minor);

	if (!mpp) {
		condlog(2, "%s: devmap not registered, can't remove",
			devname);
		return 1;
	}
	if (strcmp(mpp->alias, alias)) {
		condlog(2, "%s: minor number mismatch (map %d, event %d)",
			mpp->alias, mpp->dmi->minor, minor);
		return 1;
	}
	return flush_map(mpp, vecs, 0);
}

static int
uev_add_path (struct uevent *uev, struct vectors * vecs, int need_do_map)
{
	struct path *pp;
	int ret = 0, i;
	struct config *conf;

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

		condlog(3, "%s: spurious uevent, path already in pathvec",
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
			if (r == PATHINFO_OK)
				ret = ev_add_path(pp, vecs, need_do_map);
			else if (r == PATHINFO_SKIPPED) {
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
	lock_cleanup_pop(vecs->lock);
	if (pp)
		return ret;

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
			return 0;
		condlog(3, "%s: failed to get path info", uev->kernel);
		return 1;
	}
	pthread_cleanup_push(cleanup_lock, &vecs->lock);
	lock(&vecs->lock);
	pthread_testcancel();
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
	lock_cleanup_pop(vecs->lock);
	return ret;
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
	char params[PARAMS_SIZE] = {0};
	int retries = 3;
	int start_waiter = 0;
	int ret;

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
		if (adopt_paths(vecs->pathvec, mpp))
			goto fail; /* leave path added to pathvec */

		verify_paths(mpp, vecs);
		mpp->action = ACT_RELOAD;
	} else {
		if (!should_multipath(pp, vecs->pathvec, vecs->mpvec)) {
			orphan_path(pp, "only one path");
			return 0;
		}
		condlog(4,"%s: creating new map", pp->dev);
		if ((mpp = add_map_with_path(vecs, pp, 1))) {
			mpp->action = ACT_CREATE;
			/*
			 * We don't depend on ACT_CREATE, as domap will
			 * set it to ACT_NOTHING when complete.
			 */
			start_waiter = 1;
		}
		if (!start_waiter)
			goto fail; /* leave path added to pathvec */
	}

	/* persistent reservation check*/
	mpath_pr_event_handle(pp);

	if (!need_do_map)
		return 0;

	if (!dm_map_present(mpp->alias)) {
		mpp->action = ACT_CREATE;
		start_waiter = 1;
	}
	/*
	 * push the map to the device-mapper
	 */
	if (setup_map(mpp, params, PARAMS_SIZE, vecs)) {
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
			goto rescan;
		}
		else if (mpp->action == ACT_RELOAD)
			condlog(0, "%s: giving up reload", mpp->alias);
		else
			goto fail_map;
	}
	dm_lib_release();

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
		condlog(2, "%s [%s]: path added to devmap %s",
			pp->dev, pp->dev_t, mpp->alias);
		return 0;
	} else
		goto fail;

fail_map:
	remove_map(mpp, vecs, 1);
fail:
	orphan_path(pp, "failed to add path");
	return 1;
}

static int
uev_remove_path (struct uevent *uev, struct vectors * vecs, int need_do_map)
{
	struct path *pp;
	int ret;

	condlog(3, "%s: remove path (uevent)", uev->kernel);
	delete_foreign(uev->udev);

	pthread_cleanup_push(cleanup_lock, &vecs->lock);
	lock(&vecs->lock);
	pthread_testcancel();
	pp = find_path_by_dev(vecs->pathvec, uev->kernel);
	if (pp)
		ret = ev_remove_path(pp, vecs, need_do_map);
	lock_cleanup_pop(vecs->lock);
	if (!pp) {
		/* Not an error; path might have been purged earlier */
		condlog(0, "%s: path already removed", uev->kernel);
		return 0;
	}
	return ret;
}

int
ev_remove_path (struct path *pp, struct vectors * vecs, int need_do_map)
{
	struct multipath * mpp;
	int i, retval = 0;
	char params[PARAMS_SIZE] = {0};

	/*
	 * avoid referring to the map of an orphaned path
	 */
	if ((mpp = pp->mpp)) {
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
		 * Make sure mpp->hwe doesn't point to freed memory
		 * We call extract_hwe_from_path() below to restore mpp->hwe
		 */
		if (mpp->hwe == pp->hwe)
			mpp->hwe = NULL;

		if ((i = find_slot(mpp->paths, (void *)pp)) != -1)
			vector_del_slot(mpp->paths, i);

		/*
		 * remove the map IF removing the last path
		 */
		if (VECTOR_SIZE(mpp->paths) == 0) {
			char alias[WWID_SIZE];

			/*
			 * flush_map will fail if the device is open
			 */
			strlcpy(alias, mpp->alias, WWID_SIZE);
			if (mpp->flush_on_last_del == FLUSH_ENABLED) {
				condlog(2, "%s Last path deleted, disabling queueing", mpp->alias);
				mpp->retry_tick = 0;
				mpp->no_path_retry = NO_PATH_RETRY_FAIL;
				mpp->disable_queueing = 1;
				mpp->stat_map_failures++;
				dm_queue_if_no_path(mpp->alias, 0);
			}
			if (!flush_map(mpp, vecs, 1)) {
				condlog(2, "%s: removed map after"
					" removing all paths",
					alias);
				retval = 0;
				goto out;
			}
			/*
			 * Not an error, continue
			 */
		}

		if (mpp->hwe == NULL)
			extract_hwe_from_path(mpp);

		if (setup_map(mpp, params, PARAMS_SIZE, vecs)) {
			condlog(0, "%s: failed to setup map for"
				" removal of path %s", mpp->alias, pp->dev);
			goto fail;
		}

		if (mpp->wait_for_udev) {
			mpp->wait_for_udev = 2;
			goto out;
		}

		if (!need_do_map)
			goto out;
		/*
		 * reload the map
		 */
		mpp->action = ACT_RELOAD;
		if (domap(mpp, params, 1) == DOMAP_FAIL) {
			condlog(0, "%s: failed in domap for "
				"removal of path %s",
				mpp->alias, pp->dev);
			retval = 1;
		} else {
			/*
			 * update our state from kernel
			 */
			if (setup_multipath(vecs, mpp))
				return 1;
			sync_map_state(mpp);

			condlog(2, "%s [%s]: path removed from map %s",
				pp->dev, pp->dev_t, mpp->alias);
		}
	}

out:
	if ((i = find_slot(vecs->pathvec, (void *)pp)) != -1)
		vector_del_slot(vecs->pathvec, i);

	free_path(pp);

	return retval;

fail:
	remove_map_and_stop_waiter(mpp, vecs);
	return 1;
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
		condlog(1, "%s: return code %d of change_forein is unsupported",
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

		if (pp->initialized == INIT_REQUESTED_UDEV) {
			needs_reinit = 1;
			goto out;
		}
		/* Don't deal with other types of failed initialization
		 * now. check_path will handle it */
		if (!strlen(pp->wwid))
			goto out;

		strcpy(wwid, pp->wwid);
		rc = get_uid(pp, pp->state, uev->udev, 0);

		if (rc != 0)
			strcpy(pp->wwid, wwid);
		else if (strncmp(wwid, pp->wwid, WWID_SIZE) != 0) {
			condlog(0, "%s: path wwid changed from '%s' to '%s'",
				uev->kernel, wwid, pp->wwid);
			ev_remove_path(pp, vecs, 1);
			needs_reinit = 1;
			goto out;
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
		if (mpp && ro >= 0) {
			condlog(2, "%s: update path write_protect to '%d' (uevent)", uev->kernel, ro);

			if (mpp->wait_for_udev)
				mpp->wait_for_udev = 2;
			else {
				if (ro == 1)
					pp->mpp->force_readonly = 1;
				retval = update_path_groups(mpp, vecs, 0);
				if (retval == 2)
					condlog(2, "%s: map removed during reload", pp->dev);
				else {
					pp->mpp->force_readonly = 0;
					condlog(2, "%s: map %s reloaded (retval %d)", uev->kernel, mpp->alias, retval);
				}
			}
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
	FREE(devt);
	FREE(action);
	return r;
out:
	FREE(action);
	return 1;
}

static int
map_discovery (struct vectors * vecs)
{
	struct multipath * mpp;
	unsigned int i;

	if (dm_get_maps(vecs->mpvec))
		return 1;

	vector_foreach_slot (vecs->mpvec, mpp, i)
		if (update_multipath_table(mpp, vecs->pathvec, 1) ||
		    update_multipath_status(mpp)) {
			remove_map(mpp, vecs, 1);
			i--;
		}

	return 0;
}

int
uxsock_trigger (char * str, char ** reply, int * len, bool is_root,
		void * trigger_data)
{
	struct vectors * vecs;
	int r;

	*reply = NULL;
	*len = 0;
	vecs = (struct vectors *)trigger_data;

	if ((str != NULL) && (is_root == false) &&
	    (strncmp(str, "list", strlen("list")) != 0) &&
	    (strncmp(str, "show", strlen("show")) != 0)) {
		*reply = STRDUP("permission deny: need to be root");
		if (*reply)
			*len = strlen(*reply) + 1;
		return 1;
	}

	r = parse_cmd(str, reply, len, vecs, uxsock_timeout / 1000);

	if (r > 0) {
		if (r == ETIMEDOUT)
			*reply = STRDUP("timeout\n");
		else
			*reply = STRDUP("fail\n");
		if (*reply)
			*len = strlen(*reply) + 1;
		r = 1;
	}
	else if (!r && *len == 0) {
		*reply = STRDUP("ok\n");
		if (*reply)
			*len = strlen(*reply) + 1;
		r = 0;
	}
	/* else if (r < 0) leave *reply alone */

	return r;
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

static void rcu_unregister(void *param)
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

	set_handler_callback(LIST+PATHS, cli_list_paths);
	set_handler_callback(LIST+PATHS+FMT, cli_list_paths_fmt);
	set_handler_callback(LIST+PATHS+RAW+FMT, cli_list_paths_raw);
	set_handler_callback(LIST+PATH, cli_list_path);
	set_handler_callback(LIST+MAPS, cli_list_maps);
	set_handler_callback(LIST+STATUS, cli_list_status);
	set_unlocked_handler_callback(LIST+DAEMON, cli_list_daemon);
	set_handler_callback(LIST+MAPS+STATUS, cli_list_maps_status);
	set_handler_callback(LIST+MAPS+STATS, cli_list_maps_stats);
	set_handler_callback(LIST+MAPS+FMT, cli_list_maps_fmt);
	set_handler_callback(LIST+MAPS+RAW+FMT, cli_list_maps_raw);
	set_handler_callback(LIST+MAPS+TOPOLOGY, cli_list_maps_topology);
	set_handler_callback(LIST+TOPOLOGY, cli_list_maps_topology);
	set_handler_callback(LIST+MAPS+JSON, cli_list_maps_json);
	set_handler_callback(LIST+MAP+TOPOLOGY, cli_list_map_topology);
	set_handler_callback(LIST+MAP+FMT, cli_list_map_fmt);
	set_handler_callback(LIST+MAP+RAW+FMT, cli_list_map_fmt);
	set_handler_callback(LIST+MAP+JSON, cli_list_map_json);
	set_handler_callback(LIST+CONFIG+LOCAL, cli_list_config_local);
	set_handler_callback(LIST+CONFIG, cli_list_config);
	set_handler_callback(LIST+BLACKLIST, cli_list_blacklist);
	set_handler_callback(LIST+DEVICES, cli_list_devices);
	set_handler_callback(LIST+WILDCARDS, cli_list_wildcards);
	set_handler_callback(RESET+MAPS+STATS, cli_reset_maps_stats);
	set_handler_callback(RESET+MAP+STATS, cli_reset_map_stats);
	set_handler_callback(ADD+PATH, cli_add_path);
	set_handler_callback(DEL+PATH, cli_del_path);
	set_handler_callback(ADD+MAP, cli_add_map);
	set_handler_callback(DEL+MAP, cli_del_map);
	set_handler_callback(SWITCH+MAP+GROUP, cli_switch_group);
	set_unlocked_handler_callback(RECONFIGURE, cli_reconfigure);
	set_handler_callback(SUSPEND+MAP, cli_suspend);
	set_handler_callback(RESUME+MAP, cli_resume);
	set_handler_callback(RESIZE+MAP, cli_resize);
	set_handler_callback(RELOAD+MAP, cli_reload);
	set_handler_callback(RESET+MAP, cli_reassign);
	set_handler_callback(REINSTATE+PATH, cli_reinstate);
	set_handler_callback(FAIL+PATH, cli_fail);
	set_handler_callback(DISABLEQ+MAP, cli_disable_queueing);
	set_handler_callback(RESTOREQ+MAP, cli_restore_queueing);
	set_handler_callback(DISABLEQ+MAPS, cli_disable_all_queueing);
	set_handler_callback(RESTOREQ+MAPS, cli_restore_all_queueing);
	set_unlocked_handler_callback(QUIT, cli_quit);
	set_unlocked_handler_callback(SHUTDOWN, cli_shutdown);
	set_handler_callback(GETPRSTATUS+MAP, cli_getprstatus);
	set_handler_callback(SETPRSTATUS+MAP, cli_setprstatus);
	set_handler_callback(UNSETPRSTATUS+MAP, cli_unsetprstatus);
	set_handler_callback(FORCEQ+DAEMON, cli_force_no_daemon_q);
	set_handler_callback(RESTOREQ+DAEMON, cli_restore_no_daemon_q);
	set_handler_callback(GETPRKEY+MAP, cli_getprkey);
	set_handler_callback(SETPRKEY+MAP+KEY, cli_setprkey);
	set_handler_callback(UNSETPRKEY+MAP, cli_unsetprkey);
	set_handler_callback(SETMARGINAL+PATH, cli_set_marginal);
	set_handler_callback(UNSETMARGINAL+PATH, cli_unset_marginal);
	set_handler_callback(UNSETMARGINAL+MAP, cli_unset_all_marginal);

	umask(077);
	uxsock_listen(&uxsock_trigger, ux_sock, ap);

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
static int
reinstate_path (struct path * pp, int add_active)
{
	int ret = 0;

	if (!pp->mpp)
		return 0;

	if (dm_reinstate_path(pp->mpp->alias, pp->dev_t)) {
		condlog(0, "%s: reinstate failed", pp->dev_t);
		ret = 1;
	} else {
		condlog(2, "%s: reinstated", pp->dev_t);
		if (add_active)
			update_queue_mode_add_path(pp->mpp);
	}
	return ret;
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
	unsigned int i;

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
	unsigned int i;
	int timed_out = 0, delayed_reconfig;
	struct config *conf;

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

	conf = get_multipath_config();
	delayed_reconfig = conf->delayed_reconfig;
	put_multipath_config(conf);
	if (timed_out && delayed_reconfig &&
	    !need_to_delay_reconfig(vecs)) {
		condlog(2, "reconfigure (delayed)");
		set_config_state(DAEMON_CONFIGURE);
	}
}

static void
ghost_delay_tick(struct vectors *vecs)
{
	struct multipath * mpp;
	unsigned int i;

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
defered_failback_tick (vector mpvec)
{
	struct multipath * mpp;
	unsigned int i;

	vector_foreach_slot (mpvec, mpp, i) {
		/*
		 * deferred failback getting sooner
		 */
		if (mpp->pgfailback > 0 && mpp->failback_tick > 0) {
			mpp->failback_tick--;

			if (!mpp->failback_tick && need_switch_pathgroup(mpp, 1))
				switch_pathgroup(mpp);
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
				dm_queue_if_no_path(mpp->alias, 0);
				condlog(2, "%s: Disable queueing", mpp->alias);
			}
		}
	}
}

int update_prio(struct path *pp, int refresh_all)
{
	int oldpriority;
	struct path *pp1;
	struct pathgroup * pgp;
	int i, j, changed = 0;
	struct config *conf;

	if (refresh_all) {
		vector_foreach_slot (pp->mpp->pg, pgp, i) {
			vector_foreach_slot (pgp->paths, pp1, j) {
				oldpriority = pp1->priority;
				conf = get_multipath_config();
				pthread_cleanup_push(put_multipath_config,
						     conf);
				pathinfo(pp1, conf, DI_PRIO);
				pthread_cleanup_pop(1);
				if (pp1->priority != oldpriority)
					changed = 1;
			}
		}
		return changed;
	}
	oldpriority = pp->priority;
	conf = get_multipath_config();
	pthread_cleanup_push(put_multipath_config, conf);
	if (pp->state != PATH_DOWN)
		pathinfo(pp, conf, DI_PRIO);
	pthread_cleanup_pop(1);

	if (pp->priority == oldpriority)
		return 0;
	return 1;
}

int update_path_groups(struct multipath *mpp, struct vectors *vecs, int refresh)
{
	if (reload_map(vecs, mpp, refresh, 1))
		return 1;

	dm_lib_release();
	if (setup_multipath(vecs, mpp) != 0)
		return 2;
	sync_map_state(mpp);

	return 0;
}

static int check_path_reinstate_state(struct path * pp) {
	struct timespec curr_time;

	/*
	 * This function is only called when the path state changes
	 * from "bad" to "good". pp->state reflects the *previous* state.
	 * If this was "bad", we know that a failure must have occured
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
		if (pp->mpp->nr_active == 0) {
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
	 * so that the cutomer can rectify the issue within this time. After
	 * the completion of san_path_err_recovery_time it should
	 * automatically reinstate the path
	 */
	if (pp->path_failures > pp->mpp->san_path_err_threshold) {
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

/*
 * Returns '1' if the path has been checked, '-1' if it was blacklisted
 * and '0' otherwise
 */
int
check_path (struct vectors * vecs, struct path * pp, int ticks)
{
	int newstate;
	int new_path_up = 0;
	int chkr_new_path_up = 0;
	int add_active;
	int disable_reinstate = 0;
	int oldchkrstate = pp->chkrstate;
	int retrigger_tries, checkint, max_checkint, verbosity;
	struct config *conf;
	int marginal_pathgroups, marginal_changed = 0;
	int ret;

	if ((pp->initialized == INIT_OK ||
	     pp->initialized == INIT_REQUESTED_UDEV) && !pp->mpp)
		return 0;

	if (pp->tick)
		pp->tick -= (pp->tick > ticks) ? ticks : pp->tick;
	if (pp->tick)
		return 0; /* don't check this path yet */

	conf = get_multipath_config();
	retrigger_tries = conf->retrigger_tries;
	checkint = conf->checkint;
	max_checkint = conf->max_checkint;
	verbosity = conf->verbosity;
	marginal_pathgroups = conf->marginal_pathgroups;
	put_multipath_config(conf);

	if (pp->checkint == CHECKINT_UNDEF) {
		condlog(0, "%s: BUG: checkint is not set", pp->dev);
		pp->checkint = checkint;
	};

	if (!pp->mpp && pp->initialized == INIT_MISSING_UDEV) {
		if (pp->retriggers < retrigger_tries) {
			condlog(2, "%s: triggering change event to reinitialize",
				pp->dev);
			pp->initialized = INIT_REQUESTED_UDEV;
			pp->retriggers++;
			sysfs_attr_set_value(pp->udev, "uevent", "change",
					     strlen("change"));
			return 0;
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
			return 0;
		}
	}

	/*
	 * provision a next check soonest,
	 * in case we exit abnormaly from here
	 */
	pp->tick = checkint;

	newstate = path_offline(pp);
	if (newstate == PATH_UP) {
		conf = get_multipath_config();
		pthread_cleanup_push(put_multipath_config, conf);
		newstate = get_state(pp, conf, 1, newstate);
		pthread_cleanup_pop(1);
	} else {
		checker_clear_message(&pp->checker);
		condlog(3, "%s: state %s, checker not called",
			pp->dev, checker_state_name(newstate));
	}
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
		LOG_MSG(2, verbosity, pp);
		conf = get_multipath_config();
		pthread_cleanup_push(put_multipath_config, conf);
		pathinfo(pp, conf, 0);
		pthread_cleanup_pop(1);
		return 1;
	} else if ((newstate != PATH_UP && newstate != PATH_GHOST) &&
			(pp->state == PATH_DELAYED)) {
		/* If path state become failed again cancel path delay state */
		pp->state = newstate;
		return 1;
	}
	if (!pp->mpp) {
		if (!strlen(pp->wwid) &&
		    (pp->initialized == INIT_FAILED ||
		     pp->initialized == INIT_NEW) &&
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
			} else {
				/*
				 * We failed multiple times to initialize this
				 * path properly. Don't re-check too often.
				 */
				pp->checkint = max_checkint;
				if (ret == PATHINFO_SKIPPED)
					return -1;
			}
		}
		return 0;
	}
	/*
	 * Async IO in flight. Keep the previous path state
	 * and reschedule as soon as possible
	 */
	if (newstate == PATH_PENDING) {
		pp->tick = 1;
		return 0;
	}
	/*
	 * Synchronize with kernel state
	 */
	if (update_multipath_strings(pp->mpp, vecs->pathvec, 1)) {
		condlog(1, "%s: Could not synchronize with kernel state",
			pp->dev);
		pp->dmstate = PSTATE_UNDEF;
	}
	/* if update_multipath_strings orphaned the path, quit early */
	if (!pp->mpp)
		return 0;
	set_no_path_retry(pp->mpp);

	if ((newstate == PATH_UP || newstate == PATH_GHOST) &&
	    (san_path_check_enabled(pp->mpp) ||
	     marginal_path_check_enabled(pp->mpp))) {
		int was_marginal = pp->marginal;
		if (should_skip_path(pp)) {
			if (!marginal_pathgroups) {
				if (marginal_path_check_enabled(pp->mpp))
					/* to reschedule as soon as possible,
					 * so that this path can be recovered
					 * in time */
					pp->tick = 1;
				pp->state = PATH_DELAYED;
				return 1;
			}
			if (!was_marginal) {
				pp->marginal = 1;
				marginal_changed = 1;
			}
		} else if (marginal_pathgroups && was_marginal) {
			pp->marginal = 0;
			marginal_changed = 1;
		}
	}

	/*
	 * don't reinstate failed path, if its in stand-by
	 * and if target supports only implicit tpgs mode.
	 * this will prevent unnecessary i/o by dm on stand-by
	 * paths if there are no other active paths in map.
	 */
	disable_reinstate = (newstate == PATH_GHOST &&
			     pp->mpp->nr_active == 0 &&
			     path_get_tpgs(pp) == TPGS_IMPLICIT) ? 1 : 0;

	pp->chkrstate = newstate;
	if (newstate != pp->state) {
		int oldstate = pp->state;
		pp->state = newstate;

		LOG_MSG(1, verbosity, pp);

		/*
		 * upon state change, reset the checkint
		 * to the shortest delay
		 */
		conf = get_multipath_config();
		pp->checkint = conf->checkint;
		put_multipath_config(conf);

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
			return 1;
		}

		if (newstate == PATH_UP || newstate == PATH_GHOST) {
			if (pp->mpp->prflag) {
				/*
				 * Check Persistent Reservation.
				 */
				condlog(2, "%s: checking persistent "
					"reservation registration", pp->dev);
				mpath_pr_event_handle(pp);
			}
		}

		/*
		 * reinstate this path
		 */
		if (oldstate != PATH_UP &&
		    oldstate != PATH_GHOST)
			add_active = 1;
		else
			add_active = 0;
		if (!disable_reinstate && reinstate_path(pp, add_active)) {
			condlog(3, "%s: reload map", pp->dev);
			ev_add_path(pp, vecs, 1);
			pp->tick = 1;
			return 0;
		}
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
		    !disable_reinstate) {
			/* Clear IO errors */
			if (reinstate_path(pp, 0)) {
				condlog(3, "%s: reload map", pp->dev);
				ev_add_path(pp, vecs, 1);
				pp->tick = 1;
				return 0;
			}
		} else {
			LOG_MSG(4, verbosity, pp);
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
			pp->tick = pp->checkint;
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
				LOG_MSG(3, verbosity, pp);
			else
				LOG_MSG(2, verbosity, pp);
		}
	}

	pp->state = newstate;

	if (pp->mpp->wait_for_udev)
		return 1;
	/*
	 * path prio refreshing
	 */
	condlog(4, "path prio refresh");

	if (marginal_changed)
		update_path_groups(pp->mpp, vecs, 1);
	else if (update_prio(pp, new_path_up) &&
	    (pp->mpp->pgpolicyfn == (pgpolicyfn *)group_by_prio) &&
	     pp->mpp->pgfailback == -FAILBACK_IMMEDIATE)
		update_path_groups(pp->mpp, vecs, !new_path_up);
	else if (need_switch_pathgroup(pp->mpp, 0)) {
		if (pp->mpp->pgfailback > 0 &&
		    (new_path_up || pp->mpp->failback_tick <= 0))
			pp->mpp->failback_tick =
				pp->mpp->pgfailback + 1;
		else if (pp->mpp->pgfailback == -FAILBACK_IMMEDIATE ||
			 (chkr_new_path_up && followover_should_failback(pp)))
			switch_pathgroup(pp->mpp);
	}
	return 1;
}

static void *
checkerloop (void *ap)
{
	struct vectors *vecs;
	struct path *pp;
	int count = 0;
	unsigned int i;
	struct timespec last_time;
	struct config *conf;
	int foreign_tick = 0;

	pthread_cleanup_push(rcu_unregister, NULL);
	rcu_register_thread();
	mlockall(MCL_CURRENT | MCL_FUTURE);
	vecs = (struct vectors *)ap;
	condlog(2, "path checkers start up");

	/* Tweak start time for initial path check */
	get_monotonic_time(&last_time);
	last_time.tv_sec -= 1;

	while (1) {
		struct timespec diff_time, start_time, end_time;
		int num_paths = 0, ticks = 0, strict_timing, rc = 0;

		get_monotonic_time(&start_time);
		if (start_time.tv_sec && last_time.tv_sec) {
			timespecsub(&start_time, &last_time, &diff_time);
			condlog(4, "tick (%lu.%06lu secs)",
				diff_time.tv_sec, diff_time.tv_nsec / 1000);
			last_time = start_time;
			ticks = diff_time.tv_sec;
		} else {
			ticks = 1;
			condlog(4, "tick (%d ticks)", ticks);
		}
#ifdef USE_SYSTEMD
		if (use_watchdog)
			sd_notify(0, "WATCHDOG=1");
#endif
		rc = set_config_state(DAEMON_RUNNING);
		if (rc == ETIMEDOUT) {
			condlog(4, "timeout waiting for DAEMON_IDLE");
			continue;
		} else if (rc == EINVAL)
			/* daemon shutdown */
			break;

		pthread_cleanup_push(cleanup_lock, &vecs->lock);
		lock(&vecs->lock);
		pthread_testcancel();
		vector_foreach_slot (vecs->pathvec, pp, i) {
			rc = check_path(vecs, pp, ticks);
			if (rc < 0) {
				vector_del_slot(vecs->pathvec, i);
				free_path(pp);
				i--;
			} else
				num_paths += rc;
		}
		lock_cleanup_pop(vecs->lock);

		pthread_cleanup_push(cleanup_lock, &vecs->lock);
		lock(&vecs->lock);
		pthread_testcancel();
		defered_failback_tick(vecs->mpvec);
		retry_count_tick(vecs->mpvec);
		missing_uev_wait_tick(vecs);
		ghost_delay_tick(vecs);
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

		diff_time.tv_nsec = 0;
		if (start_time.tv_sec) {
			get_monotonic_time(&end_time);
			timespecsub(&end_time, &start_time, &diff_time);
			if (num_paths) {
				unsigned int max_checkint;

				condlog(4, "checked %d path%s in %lu.%06lu secs",
					num_paths, num_paths > 1 ? "s" : "",
					diff_time.tv_sec,
					diff_time.tv_nsec / 1000);
				conf = get_multipath_config();
				max_checkint = conf->max_checkint;
				put_multipath_config(conf);
				if (diff_time.tv_sec > max_checkint)
					condlog(1, "path checkers took longer "
						"than %lu seconds, consider "
						"increasing max_polling_interval",
						diff_time.tv_sec);
			}
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
			if (diff_time.tv_nsec) {
				diff_time.tv_sec = 0;
				diff_time.tv_nsec =
				     1000UL * 1000 * 1000 - diff_time.tv_nsec;
			} else
				diff_time.tv_sec = 1;

			condlog(3, "waiting for %lu.%06lu secs",
				diff_time.tv_sec,
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

int
configure (struct vectors * vecs)
{
	struct multipath * mpp;
	struct path * pp;
	vector mpvec;
	int i, ret;
	struct config *conf;
	static int force_reload = FORCE_RELOAD_WEAK;

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

	/*
	 * create new set of maps & push changed ones into dm
	 * In the first call, use FORCE_RELOAD_WEAK to avoid making
	 * superfluous ACT_RELOAD ioctls. Later calls are done
	 * with FORCE_RELOAD_YES.
	 */
	ret = coalesce_paths(vecs, mpvec, NULL, force_reload, CMD_NONE);
	if (force_reload == FORCE_RELOAD_WEAK)
		force_reload = FORCE_RELOAD_YES;
	if (ret != CP_OK) {
		condlog(0, "configure failed while coalescing paths");
		goto fail;
	}

	/*
	 * may need to remove some maps which are no longer relevant
	 * e.g., due to blacklist changes in conf file
	 */
	if (coalesce_maps(vecs, mpvec)) {
		condlog(0, "configure failed while coalescing maps");
		goto fail;
	}

	dm_lib_release();

	sync_maps_state(mpvec);
	vector_foreach_slot(mpvec, mpp, i){
		if (remember_wwid(mpp->wwid) == 1)
			trigger_paths_udev_change(mpp, true);
		update_map_pr(mpp);
	}

	/*
	 * purge dm of old maps
	 */
	remove_maps(vecs);

	/*
	 * save new set of maps formed by considering current path state
	 */
	vector_free(vecs->mpvec);
	vecs->mpvec = mpvec;

	/*
	 * start dm event waiter threads for these new maps
	 */
	vector_foreach_slot(vecs->mpvec, mpp, i) {
		if (wait_for_events(mpp, vecs)) {
			remove_map(mpp, vecs, 1);
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

int
reconfigure (struct vectors * vecs)
{
	struct config * old, *conf;

	conf = load_config(DEFAULT_CONFIGFILE);
	if (!conf)
		return 1;

	/*
	 * free old map and path vectors ... they use old conf state
	 */
	if (VECTOR_SIZE(vecs->mpvec))
		remove_maps_and_stop_waiters(vecs);

	free_pathvec(vecs->pathvec, FREE_PATHS);
	vecs->pathvec = NULL;
	delete_all_foreign();

	/* Re-read any timezone changes */
	tzset();

	dm_tgt_version(conf->version, TGT_MPATH);
	if (verbosity)
		conf->verbosity = verbosity;
	if (bindings_read_only)
		conf->bindings_read_only = bindings_read_only;
	uxsock_timeout = conf->uxsock_timeout;

	old = rcu_dereference(multipath_conf);
	rcu_assign_pointer(multipath_conf, conf);
	call_rcu(&old->rcu, rcu_free_config);

	configure(vecs);


	return 0;
}

static struct vectors *
init_vecs (void)
{
	struct vectors * vecs;

	vecs = (struct vectors *)MALLOC(sizeof(struct vectors));

	if (!vecs)
		return NULL;

	pthread_mutex_init(&vecs->lock.mutex, NULL);

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
		condlog(2, "exit (signal)");
		exit_sig = 0;
		exit_daemon();
	}
	if (!nonfatal)
		return;
	if (reconfig_sig) {
		condlog(2, "reconfigure (signal)");
		set_config_state(DAEMON_CONFIGURE);
	}
	if (log_reset_sig) {
		condlog(2, "reset log (signal)");
		if (logsink == 1)
			log_thread_reset();
	}
	reconfig_sig = 0;
	log_reset_sig = 0;
}

static void
sighup (int sig)
{
	reconfig_sig = 1;
}

static void
sigend (int sig)
{
	exit_sig = 1;
}

static void
sigusr1 (int sig)
{
	log_reset_sig = 1;
}

static void
sigusr2 (int sig)
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
	static struct sched_param sched_param = {
		.sched_priority = 99
	};

	res = sched_setscheduler (0, SCHED_RR, &sched_param);

	if (res == -1)
		condlog(LOG_WARNING, "Could not set SCHED_RR at priority 99");
	return;
}

static void
set_oom_adj (void)
{
#ifdef OOM_SCORE_ADJ_MIN
	int retry = 1;
	char *file = "/proc/self/oom_score_adj";
	int score = OOM_SCORE_ADJ_MIN;
#else
	int retry = 0;
	char *file = "/proc/self/oom_adj";
	int score = OOM_ADJUST_MIN;
#endif
	FILE *fp;
	struct stat st;
	char *envp;

	envp = getenv("OOMScoreAdjust");
	if (envp) {
		condlog(3, "Using systemd provided OOMScoreAdjust");
		return;
	}
	do {
		if (stat(file, &st) == 0){
			fp = fopen(file, "w");
			if (!fp) {
				condlog(0, "couldn't fopen %s : %s", file,
					strerror(errno));
				return;
			}
			fprintf(fp, "%i", score);
			fclose(fp);
			return;
		}
		if (errno != ENOENT) {
			condlog(0, "couldn't stat %s : %s", file,
				strerror(errno));
			return;
		}
#ifdef OOM_ADJUST_MIN
		file = "/proc/self/oom_adj";
		score = OOM_ADJUST_MIN;
#else
		retry = 0;
#endif
	} while (retry--);
	condlog(0, "couldn't adjust oom score");
}

static int
child (void * param)
{
	pthread_t check_thr, uevent_thr, uxlsnr_thr, uevq_thr, dmevent_thr;
	pthread_attr_t log_attr, misc_attr, uevent_attr;
	struct vectors * vecs;
	struct multipath * mpp;
	int i;
#ifdef USE_SYSTEMD
	unsigned long checkint;
	int startup_done = 0;
#endif
	int rc;
	int pid_fd = -1;
	struct config *conf;
	char *envp;
	int queue_without_daemon;
	enum daemon_status state;

	mlockall(MCL_CURRENT | MCL_FUTURE);
	signal_init();
	rcu_init();

	setup_thread_attr(&misc_attr, 64 * 1024, 0);
	setup_thread_attr(&uevent_attr, DEFAULT_UEVENT_STACKSIZE * 1024, 0);
	setup_thread_attr(&waiter_attr, 32 * 1024, 1);
	setup_thread_attr(&io_err_stat_attr, 32 * 1024, 0);

	if (logsink == 1) {
		setup_thread_attr(&log_attr, 64 * 1024, 0);
		log_thread_start(&log_attr);
		pthread_attr_destroy(&log_attr);
	}
	pid_fd = pidfile_create(DEFAULT_PIDFILE, daemon_pid);
	if (pid_fd < 0) {
		condlog(1, "failed to create pidfile");
		if (logsink == 1)
			log_thread_stop();
		exit(1);
	}

	post_config_state(DAEMON_START);

	condlog(2, "--------start up--------");
	condlog(2, "read " DEFAULT_CONFIGFILE);

	conf = load_config(DEFAULT_CONFIGFILE);
	if (!conf)
		goto failed;

	if (verbosity)
		conf->verbosity = verbosity;
	if (bindings_read_only)
		conf->bindings_read_only = bindings_read_only;
	uxsock_timeout = conf->uxsock_timeout;
	rcu_assign_pointer(multipath_conf, conf);
	if (init_checkers(conf->multipath_dir)) {
		condlog(0, "failed to initialize checkers");
		goto failed;
	}
	if (init_prio(conf->multipath_dir)) {
		condlog(0, "failed to initialize prioritizers");
		goto failed;
	}
	/* Failing this is non-fatal */

	init_foreign(conf->multipath_dir, conf->enable_foreign);

	if (poll_dmevents)
		poll_dmevents = dmevent_poll_supported();
	setlogmask(LOG_UPTO(conf->verbosity + 3));

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

#ifdef USE_SYSTEMD
	envp = getenv("WATCHDOG_USEC");
	if (envp && sscanf(envp, "%lu", &checkint) == 1) {
		/* Value is in microseconds */
		conf->max_checkint = checkint / 1000000;
		/* Rescale checkint */
		if (conf->checkint > conf->max_checkint)
			conf->checkint = conf->max_checkint;
		else
			conf->checkint = conf->max_checkint / 4;
		condlog(3, "enabling watchdog, interval %d max %d",
			conf->checkint, conf->max_checkint);
		use_watchdog = conf->checkint;
	}
#endif
	/*
	 * Startup done, invalidate configuration
	 */
	conf = NULL;

	pthread_cleanup_push(config_cleanup, NULL);
	pthread_mutex_lock(&config_lock);

	__post_config_state(DAEMON_IDLE);
	rc = pthread_create(&uxlsnr_thr, &misc_attr, uxlsnrloop, vecs);
	if (!rc) {
		/* Wait for uxlsnr startup */
		while (running_state == DAEMON_IDLE)
			pthread_cond_wait(&config_cond, &config_lock);
		state = running_state;
	}
	pthread_cleanup_pop(1);

	if (rc) {
		condlog(0, "failed to create cli listener: %d", rc);
		goto failed;
	}
	else if (state != DAEMON_CONFIGURE) {
		condlog(0, "cli listener failed to start");
		goto failed;
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
		}
	}

	/*
	 * Start uevent listener early to catch events
	 */
	if ((rc = pthread_create(&uevent_thr, &uevent_attr, ueventloop, udev))) {
		condlog(0, "failed to create uevent thread: %d", rc);
		goto failed;
	}
	pthread_attr_destroy(&uevent_attr);

	/*
	 * start threads
	 */
	if ((rc = pthread_create(&check_thr, &misc_attr, checkerloop, vecs))) {
		condlog(0,"failed to create checker loop thread: %d", rc);
		goto failed;
	}
	if ((rc = pthread_create(&uevq_thr, &misc_attr, uevqloop, vecs))) {
		condlog(0, "failed to create uevent dispatcher: %d", rc);
		goto failed;
	}
	pthread_attr_destroy(&misc_attr);

	while (1) {
		pthread_cleanup_push(config_cleanup, NULL);
		pthread_mutex_lock(&config_lock);
		while (running_state != DAEMON_CONFIGURE &&
		       running_state != DAEMON_SHUTDOWN)
			pthread_cond_wait(&config_cond, &config_lock);
		state = running_state;
		pthread_cleanup_pop(1);
		if (state == DAEMON_SHUTDOWN)
			break;
		if (state == DAEMON_CONFIGURE) {
			pthread_cleanup_push(cleanup_lock, &vecs->lock);
			lock(&vecs->lock);
			pthread_testcancel();
			if (!need_to_delay_reconfig(vecs)) {
				reconfigure(vecs);
			} else {
				conf = get_multipath_config();
				conf->delayed_reconfig = 1;
				put_multipath_config(conf);
			}
			lock_cleanup_pop(vecs->lock);
			post_config_state(DAEMON_IDLE);
#ifdef USE_SYSTEMD
			if (!startup_done) {
				sd_notify(0, "READY=1");
				startup_done = 1;
			}
#endif
		}
	}

	lock(&vecs->lock);
	conf = get_multipath_config();
	queue_without_daemon = conf->queue_without_daemon;
	put_multipath_config(conf);
	if (queue_without_daemon == QUE_NO_DAEMON_OFF)
		vector_foreach_slot(vecs->mpvec, mpp, i)
			dm_queue_if_no_path(mpp->alias, 0);
	remove_maps_and_stop_waiters(vecs);
	unlock(&vecs->lock);

	pthread_cancel(check_thr);
	pthread_cancel(uevent_thr);
	pthread_cancel(uxlsnr_thr);
	pthread_cancel(uevq_thr);
	if (poll_dmevents)
		pthread_cancel(dmevent_thr);

	pthread_join(check_thr, NULL);
	pthread_join(uevent_thr, NULL);
	pthread_join(uxlsnr_thr, NULL);
	pthread_join(uevq_thr, NULL);
	if (poll_dmevents)
		pthread_join(dmevent_thr, NULL);

	stop_io_err_stat_thread();

	lock(&vecs->lock);
	free_pathvec(vecs->pathvec, FREE_PATHS);
	vecs->pathvec = NULL;
	unlock(&vecs->lock);

	pthread_mutex_destroy(&vecs->lock.mutex);
	FREE(vecs);
	vecs = NULL;

	cleanup_foreign();
	cleanup_checkers();
	cleanup_prio();
	if (poll_dmevents)
		cleanup_dmevent_waiter();

	dm_lib_release();
	dm_lib_exit();

	/* We're done here */
	condlog(3, "unlink pidfile");
	unlink(DEFAULT_PIDFILE);

	condlog(2, "--------shut down-------");

	if (logsink == 1)
		log_thread_stop();

	/*
	 * Freeing config must be done after condlog() and dm_lib_exit(),
	 * because logging functions like dlog() and dm_write_log()
	 * reference the config.
	 */
	conf = rcu_dereference(multipath_conf);
	rcu_assign_pointer(multipath_conf, NULL);
	call_rcu(&conf->rcu, rcu_free_config);
	udev_unref(udev);
	udev = NULL;
	pthread_attr_destroy(&waiter_attr);
	pthread_attr_destroy(&io_err_stat_attr);
#ifdef _DEBUG_
	dbg_free_final(NULL);
#endif

#ifdef USE_SYSTEMD
	sd_notify(0, "ERRNO=0");
#endif
	exit(0);

failed:
#ifdef USE_SYSTEMD
	sd_notify(0, "ERRNO=1");
#endif
	if (pid_fd >= 0)
		close(pid_fd);
	exit(1);
}

static int
daemonize(void)
{
	int pid;
	int dev_null_fd;

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

	close(STDIN_FILENO);
	if (dup(dev_null_fd) < 0) {
		fprintf(stderr, "cannot dup /dev/null to stdin : %s\n",
			strerror(errno));
		_exit(0);
	}
	close(STDOUT_FILENO);
	if (dup(dev_null_fd) < 0) {
		fprintf(stderr, "cannot dup /dev/null to stdout : %s\n",
			strerror(errno));
		_exit(0);
	}
	close(STDERR_FILENO);
	if (dup(dev_null_fd) < 0) {
		fprintf(stderr, "cannot dup /dev/null to stderr : %s\n",
			strerror(errno));
		_exit(0);
	}
	close(dev_null_fd);
	daemon_pid = getpid();
	return 0;
}

int
main (int argc, char *argv[])
{
	extern char *optarg;
	extern int optind;
	int arg;
	int err;
	int foreground = 0;
	struct config *conf;

	ANNOTATE_BENIGN_RACE_SIZED(&multipath_conf, sizeof(multipath_conf),
				   "Manipulated through RCU");
	ANNOTATE_BENIGN_RACE_SIZED(&uxsock_timeout, sizeof(uxsock_timeout),
		"Suppress complaints about this scalar variable");

	logsink = 1;

	if (getuid() != 0) {
		fprintf(stderr, "need to be root\n");
		exit(1);
	}

	/* make sure we don't lock any path */
	if (chdir("/") < 0)
		fprintf(stderr, "can't chdir to root directory : %s\n",
			strerror(errno));
	umask(umask(077) | 022);

	pthread_cond_init_mono(&config_cond);

	udev = udev_new();
	libmp_udev_set_sync_support(0);

	while ((arg = getopt(argc, argv, ":dsv:k::Bniw")) != EOF ) {
		switch(arg) {
		case 'd':
			foreground = 1;
			if (logsink > 0)
				logsink = 0;
			//debug=1; /* ### comment me out ### */
			break;
		case 'v':
			if (sizeof(optarg) > sizeof(char *) ||
			    !isdigit(optarg[0]))
				exit(1);

			verbosity = atoi(optarg);
			break;
		case 's':
			logsink = -1;
			break;
		case 'k':
			logsink = 0;
			conf = load_config(DEFAULT_CONFIGFILE);
			if (!conf)
				exit(1);
			if (verbosity)
				conf->verbosity = verbosity;
			uxsock_timeout = conf->uxsock_timeout;
			err = uxclnt(optarg, uxsock_timeout + 100);
			free_config(conf);
			return err;
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
	if (optind < argc) {
		char cmd[CMDSIZE];
		char * s = cmd;
		char * c = s;

		logsink = 0;
		conf = load_config(DEFAULT_CONFIGFILE);
		if (!conf)
			exit(1);
		if (verbosity)
			conf->verbosity = verbosity;
		uxsock_timeout = conf->uxsock_timeout;
		memset(cmd, 0x0, CMDSIZE);
		while (optind < argc) {
			if (strchr(argv[optind], ' '))
				c += snprintf(c, s + CMDSIZE - c, "\"%s\" ", argv[optind]);
			else
				c += snprintf(c, s + CMDSIZE - c, "%s ", argv[optind]);
			optind++;
		}
		c += snprintf(c, s + CMDSIZE - c, "\n");
		err = uxclnt(s, uxsock_timeout + 100);
		free_config(conf);
		return err;
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
	int i, ret, isFound;
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
		ret = MPATH_PR_SUCCESS;
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
		ret = MPATH_PR_OTHER;
		goto out;
	}

	param= malloc(sizeof(struct prout_param_descriptor));
	memset(param, 0 , sizeof(struct prout_param_descriptor));
	param->sa_flags = mpp->sa_flags;
	memcpy(param->sa_key, &mpp->reservation_key, 8);
	param->num_transportid = 0;

	condlog(3, "device %s:%s", pp->dev, pp->mpp->wwid);

	ret = prout_do_scsi_ioctl(pp->dev, MPATH_PROUT_REG_IGN_SA, 0, 0, param, 0);
	if (ret != MPATH_PR_SUCCESS )
	{
		condlog(0,"%s: Reservation registration failed. Error: %d", pp->dev, ret);
	}
	mpp->prflag = 1;

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
		return 0;

	mpp = pp->mpp;

	if (!get_be64(mpp->reservation_key))
		return -1;

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
}
