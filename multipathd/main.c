#include <unistd.h>
#include <sys/stat.h>
#include <libdevmapper.h>
#include <wait.h>
#include <sys/mman.h>

/*
 * libsysfs
 */
#include <sysfs/libsysfs.h>
#include <sysfs/dlist.h>

/*
 * libcheckers
 */
#include <checkers.h>
#include <path_state.h>

/*
 * libmultipath
 */
#include <parser.h>
#include <vector.h>
#include <memory.h>
#include <config.h>
#include <callout.h>
#include <util.h>
#include <blacklist.h>
#include <hwtable.h>
#include <defaults.h>
#include <structs.h>
#include <dmparser.h>
#include <devmapper.h>
#include <dict.h>
#include <discovery.h>
#include <debug.h>
#include <propsel.h>
#include <uevent.h>
#include <switchgroup.h>
#include <path_state.h>
#include <print.h>

#include "main.h"
#include "pidfile.h"
#include "uxlsnr.h"
#include "uxclnt.h"
#include "cli.h"
#include "cli_handlers.h"

#define FILE_NAME_SIZE 256
#define CMDSIZE 160

#define LOG_MSG(a,b) \
	if (strlen(b)) { \
		condlog(a, "%s: %s", pp->dev_t, b); \
		memset(b, 0, MAX_CHECKER_MSG_SIZE); \
	}

#ifdef LCKDBG
#define lock(a) \
	fprintf(stderr, "%s:%s(%i) lock %p\n", __FILE__, __FUNCTION__, __LINE__, a); \
	pthread_mutex_lock(a)
#define unlock(a) \
	fprintf(stderr, "%s:%s(%i) unlock %p\n", __FILE__, __FUNCTION__, __LINE__, a); \
	pthread_mutex_unlock(a)
#define lock_cleanup_pop(a) \
	fprintf(stderr, "%s:%s(%i) unlock %p\n", __FILE__, __FUNCTION__, __LINE__, a); \
	pthread_cleanup_pop(1);
#else
#define lock(a) pthread_mutex_lock(a)
#define unlock(a) pthread_mutex_unlock(a)
#define lock_cleanup_pop(a) pthread_cleanup_pop(1);
#endif

pthread_cond_t exit_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t exit_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * structs
 */
struct event_thread {
	struct dm_task *dmt;
	pthread_t thread;
	int event_nr;
	char mapname[WWID_SIZE];
	struct vectors *vecs;
};

static struct event_thread *
alloc_waiter (void)
{

	struct event_thread * wp;

	wp = (struct event_thread *)MALLOC(sizeof(struct event_thread));

	return wp;
}

static void
free_waiter (void * data)
{
	struct event_thread * wp = (struct event_thread *)data;

	if (wp->dmt)
		dm_task_destroy(wp->dmt);
	FREE(wp);
}

static void
stop_waiter_thread (struct multipath * mpp, struct vectors * vecs)
{
	struct event_thread * wp = (struct event_thread *)mpp->waiter;
	pthread_t thread;
	
	if (!wp) {
		condlog(3, "%s: no waiter thread", mpp->alias);
		return;
	}
	wp = wp->thread;

	if (!wp) {
		condlog(3, "%s: thread not started", mpp->alias);
		return;
	}
	condlog(2, "%s: stop event checker thread", wp->mapname);
	pthread_kill(thread, SIGHUP);
}

static void
cleanup_lock (void * data)
{
	pthread_mutex_unlock((pthread_mutex_t *)data);
}

static void
adopt_paths (struct vectors * vecs, struct multipath * mpp)
{
	int i;
	struct path * pp;

	if (!mpp)
		return;

	vector_foreach_slot (vecs->pathvec, pp, i) {
		if (!strncmp(mpp->wwid, pp->wwid, WWID_SIZE)) {
			condlog(4, "%s ownership set", pp->dev_t);
			pp->mpp = mpp;
		}
	}
}

static void
orphan_path (struct path * pp)
{
	pp->mpp = NULL;
	pp->checkfn = NULL;
	pp->dmstate = PSTATE_UNDEF;
	pp->checker_context = NULL;
	pp->getuid = NULL;
	pp->getprio = NULL;

	if (pp->fd >= 0)
		close(pp->fd);

	pp->fd = -1;
}

static void
orphan_paths (struct vectors * vecs, struct multipath * mpp)
{
	int i;
	struct path * pp;

	vector_foreach_slot (vecs->pathvec, pp, i) {
		if (pp->mpp == mpp) {
			condlog(4, "%s is orphaned", pp->dev_t);
			orphan_path(pp);
		}
	}
}

static int
update_multipath_table (struct multipath *mpp, vector pathvec)
{
	if (!mpp)
		return 1;

	if (dm_get_map(mpp->alias, &mpp->size, mpp->params))
		return 1;

	if (disassemble_map(pathvec, mpp->params, mpp))
		return 1;

	return 0;
}

static int
update_multipath_status (struct multipath *mpp)
{
	if (!mpp)
		return 1;

	if(dm_get_status(mpp->alias, mpp->status))
		return 1;

	if (disassemble_status(mpp->status, mpp))
		return 1;

	return 0;
}

static int
update_multipath_strings (struct multipath *mpp, vector pathvec)
{
	if (mpp->selector) {
		FREE(mpp->selector);
		mpp->selector = NULL;
	}

	if (mpp->features) {
		FREE(mpp->features);
		mpp->features = NULL;
	}

	if (mpp->hwhandler) {
		FREE(mpp->hwhandler);
		mpp->hwhandler = NULL;
	}

	free_pgvec(mpp->pg, KEEP_PATHS);
	mpp->pg = NULL;

	if (update_multipath_table(mpp, pathvec))
		return 1;

	if (update_multipath_status(mpp))
		return 1;

	return 0;
}

static void
set_multipath_wwid (struct multipath * mpp)
{
	if (mpp->wwid)
		return;

	dm_get_uuid(mpp->alias, mpp->wwid);
}

static int
pathcount (struct multipath *mpp, int state)
{
	struct pathgroup *pgp;
	struct path *pp;
	int i, j;
	int count = 0;

	vector_foreach_slot (mpp->pg, pgp, i)
		vector_foreach_slot (pgp->paths, pp, j)
			if (pp->state == state)
				count++;
	return count;
}

/*
 * mpp->no_path_retry:
 *   -2 (QUEUE) : queue_if_no_path enabled, never turned off
 *   -1 (FAIL)  : fail_if_no_path
 *    0 (UNDEF) : nothing
 *   >0         : queue_if_no_path enabled, turned off after polling n times
 */
static void
update_queue_mode_del_path(struct multipath *mpp)
{
	if (--mpp->nr_active == 0 && mpp->no_path_retry > 0) {
		/*
		 * Enter retry mode.
		 * meaning of +1: retry_tick may be decremented in
		 *                checkerloop before starting retry.
		 */
		mpp->retry_tick = mpp->no_path_retry * conf->checkint + 1;
		condlog(1, "%s: Entering recovery mode: max_retries=%d",
			mpp->alias, mpp->no_path_retry);
	}
	condlog(2, "%s: remaining active paths: %d", mpp->alias, mpp->nr_active);
}

static void
update_queue_mode_add_path(struct multipath *mpp)
{
	if (mpp->nr_active++ == 0 && mpp->no_path_retry > 0) {
		/* come back to normal mode from retry mode */
		mpp->retry_tick = 0;
		dm_queue_if_no_path(mpp->alias, 1);
		condlog(2, "%s: queue_if_no_path enabled", mpp->alias);
		condlog(1, "%s: Recovered to normal mode", mpp->alias);
	}
	condlog(2, "%s: remaining active paths: %d", mpp->alias, mpp->nr_active);
}

static void
set_no_path_retry(struct multipath *mpp)
{
	mpp->retry_tick = 0;
	mpp->nr_active = pathcount(mpp, PATH_UP);
	select_no_path_retry(mpp);

	switch (mpp->no_path_retry) {
	case NO_PATH_RETRY_UNDEF:
		break;
	case NO_PATH_RETRY_FAIL:
		dm_queue_if_no_path(mpp->alias, 0);
		break;
	case NO_PATH_RETRY_QUEUE:
		dm_queue_if_no_path(mpp->alias, 1);
		break;
	default:
		dm_queue_if_no_path(mpp->alias, 1);
		if (mpp->nr_active == 0) {
			/* Enter retry mode */
			mpp->retry_tick = mpp->no_path_retry * conf->checkint;
			condlog(1, "%s: Entering recovery mode: max_retries=%d",
				mpp->alias, mpp->no_path_retry);
		}
		break;
	}
}

static struct hwentry *
extract_hwe_from_path(struct multipath * mpp)
{
	struct path * pp;
	struct pathgroup * pgp;

	pgp = VECTOR_SLOT(mpp->pg, 0);
	pp = VECTOR_SLOT(pgp->paths, 0);

	return pp->hwe;
}

static void
remove_map (struct multipath * mpp, struct vectors * vecs)
{
	int i;

	stop_waiter_thread(mpp, vecs);

	/*
	 * clear references to this map
	 */
	orphan_paths(vecs, mpp);

	/*
	 * purge the multipath vector
	 */
	i = find_slot(vecs->mpvec, (void *)mpp);
	vector_del_slot(vecs->mpvec, i);

	/*
	 * final free
	 */
	free_multipath(mpp, KEEP_PATHS);
	mpp = NULL;
}

static void
remove_maps (struct vectors * vecs)
{
	int i;
	struct multipath * mpp;

	vector_foreach_slot (vecs->mpvec, mpp, i) {
		remove_map(mpp, vecs);
		i--;
	}

	vector_free(vecs->mpvec);
	vecs->mpvec = NULL;
}

static int
setup_multipath (struct vectors * vecs, struct multipath * mpp)
{
	set_multipath_wwid(mpp);
	mpp->mpe = find_mpe(mpp->wwid);
	condlog(4, "discovered map %s", mpp->alias);

	if (update_multipath_strings(mpp, vecs->pathvec))
		goto out;

	adopt_paths(vecs, mpp);
	select_pgfailback(mpp);
	mpp->hwe = extract_hwe_from_path(mpp);
	set_no_path_retry(mpp);

	return 0;
out:
	condlog(0, "%s: failed to setup multipath", mpp->alias);
	remove_map(mpp, vecs);
	return 1;
}

static int
need_switch_pathgroup (struct multipath * mpp, int refresh)
{
	struct pathgroup * pgp;
	struct path * pp;
	int i, j;

	if (!mpp || mpp->pgfailback == -FAILBACK_MANUAL)
		return 0;

	/*
	 * Refresh path priority values
	 */
	if (refresh)
		vector_foreach_slot (mpp->pg, pgp, i)
			vector_foreach_slot (pgp->paths, pp, j)
				pathinfo(pp, conf->hwtable, DI_PRIO);

	select_path_group(mpp); /* sets mpp->nextpg */
	pgp = VECTOR_SLOT(mpp->pg, mpp->nextpg - 1);

	if (pgp && pgp->status != PGSTATE_ACTIVE)
		return 1;

	return 0;
}

static void
switch_pathgroup (struct multipath * mpp)
{
	struct pathgroup * pgp;
	
	pgp = VECTOR_SLOT(mpp->pg, mpp->nextpg - 1);
	
	if (pgp && pgp->status != PGSTATE_ACTIVE) {
		dm_switchgroup(mpp->alias, mpp->nextpg);
		condlog(2, "%s: switch to path group #%i",
			 mpp->alias, mpp->nextpg);
	}
}

static int
update_multipath (struct vectors *vecs, char *mapname)
{
	struct multipath *mpp;
	struct pathgroup  *pgp;
	struct path *pp;
	int i, j;
	int r = 1;

	mpp = find_mp(vecs->mpvec, mapname);

	if (!mpp)
		goto out;

	free_pgvec(mpp->pg, KEEP_PATHS);
	mpp->pg = NULL;

	if (setup_multipath(vecs, mpp))
		goto out; /* mpp freed in setup_multipath */

	/*
	 * compare checkers states with DM states
	 */
	vector_foreach_slot (mpp->pg, pgp, i) {
		vector_foreach_slot (pgp->paths, pp, j) {
			if (pp->dmstate != PSTATE_FAILED)
				continue;

			if (pp->state != PATH_DOWN) {
				condlog(2, "%s: mark as failed", pp->dev_t);
				pp->state = PATH_DOWN;
				update_queue_mode_del_path(mpp);

				/*
				 * if opportune,
				 * schedule the next check earlier
				 */
				if (pp->tick > conf->checkint)
					pp->tick = conf->checkint;
			}
		}
	}
	r = 0;
out:
	if (r)
		condlog(0, "failed to update multipath");

	return r;
}

static sigset_t unblock_sighup(void)
{
	sigset_t set, old;

	sigemptyset(&set);
	sigaddset(&set, SIGHUP);
	pthread_sigmask(SIG_UNBLOCK, &set, &old);
	return old;
}

/*
 * returns the reschedule delay
 * negative means *stop*
 */
static int
waiteventloop (struct event_thread * waiter)
{
	sigset_t set;
	int event_nr;
	int r;

	if (!waiter->event_nr)
		waiter->event_nr = dm_geteventnr(waiter->mapname);

	if (!(waiter->dmt = dm_task_create(DM_DEVICE_WAITEVENT)))
		return 1;

	if (!dm_task_set_name(waiter->dmt, waiter->mapname)) {
		dm_task_destroy(waiter->dmt);
		return 1;
	}

	if (waiter->event_nr && !dm_task_set_event_nr(waiter->dmt,
						      waiter->event_nr)) {
		dm_task_destroy(waiter->dmt);
		return 1;
	}

	dm_task_no_open_count(waiter->dmt);
	
	/* accept wait interruption */
	set = unblock_sighup();

	/* interruption spits messages */
	dm_shut_log();

	/* wait */
	r = dm_task_run(waiter->dmt);

	/* wait is over : event or interrupt */
	pthread_sigmask(SIG_SETMASK, &set, NULL);
	//dm_restore_log();

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
		pthread_cleanup_push(cleanup_lock, waiter->vecs->lock);
		lock(waiter->vecs->lock);
		r = update_multipath(waiter->vecs, waiter->mapname);
		lock_cleanup_pop(waiter->vecs->lock);

		if (r)
			return -1; /* stop the thread */

		event_nr = dm_geteventnr(waiter->mapname);

		if (waiter->event_nr == event_nr)
			return 1; /* upon problem reschedule 1s later */

		waiter->event_nr = event_nr;
	}
	return -1; /* never reach there */
}

static void *
waitevent (void * et)
{
	int r;
	struct event_thread *waiter;

	mlockall(MCL_CURRENT | MCL_FUTURE);

	waiter = (struct event_thread *)et;
	pthread_cleanup_push(free_waiter, et);

	while (1) {
		r = waiteventloop(waiter);

		if (r < 0)
			break;

		sleep(r);
	}

	pthread_cleanup_pop(1);
	return NULL;
}

static int
start_waiter_thread (struct multipath * mpp, struct vectors * vecs)
{
	pthread_attr_t attr;
	struct event_thread * wp;

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

int
uev_add_map (char * devname, struct vectors * vecs)
{
	int major, minor;
	char dev_t[BLK_DEV_SIZE];
	char * alias;
	struct multipath * mpp;

	if (sysfs_get_dev(sysfs_path, devname, dev_t, BLK_DEV_SIZE))
		return 1;

	if (sscanf(dev_t, "%d:%d", &major, &minor) != 2)
		return 1;

	alias = dm_mapname(major, minor);
		
	if (!alias)
		return 1;
	
	if (!dm_type(alias, DEFAULT_TARGET)) {
		condlog(4, "%s: not a multipath map", alias);
		FREE(alias);
		return 0;
	}

	mpp = find_mp(vecs->mpvec, alias);

	if (mpp) {
		/*
		 * this should not happen,
		 * we missed a remove map event (not sent ?)
		 */
		condlog(2, "%s: already registered", alias);
		remove_map(mpp, vecs);
	}

	/*
	 * now we can allocate
	 */
	mpp = alloc_multipath();

	if (!mpp)
		return 1;

	mpp->minor = minor;
	mpp->alias = alias;

	if (setup_multipath(vecs, mpp))
		return 1; /* mpp freed in setup_multipath */

	if (!vector_alloc_slot(vecs->mpvec))
		goto out;

	vector_set_slot(vecs->mpvec, mpp);
	adopt_paths(vecs, mpp);

	if (start_waiter_thread(mpp, vecs))
		goto out;

	return 0;
out:
	condlog(2, "%s: add devmap failed", mpp->alias);
	remove_map(mpp, vecs);
	return 1;
}

int
uev_remove_map (char * devname, struct vectors * vecs)
{
	int minor;
	struct multipath * mpp;

	if (sscanf(devname, "dm-%d", &minor) != 1)
		return 1;

	mpp = find_mp_by_minor(vecs->mpvec, minor);

	if (!mpp) {
		condlog(3, "%s: devmap not registered, can't remove",
			devname);
		return 0;
	}

	condlog(2, "remove %s devmap", mpp->alias);
	remove_map(mpp, vecs);

	return 0;
}

int
uev_add_path (char * devname, struct vectors * vecs)
{
	struct path * pp;

	pp = find_path_by_dev(vecs->pathvec, devname);

	if (pp) {
		condlog(3, "%s: already in pathvec");
		return 1;
	}
	pp = store_pathinfo(vecs->pathvec, conf->hwtable,
		       devname, DI_SYSFS | DI_WWID);

	if (!pp) {
		condlog(0, "%s: failed to store path info", devname);
		return 1;
	}

	condlog(2, "%s: path checker registered", devname);
	pp->mpp = find_mp_by_wwid(vecs->mpvec, pp->wwid);

	if (pp->mpp) {
		condlog(4, "%s: ownership set to %s",
				pp->dev_t, pp->mpp->alias);
	} else {
		condlog(4, "%s: orphaned", pp->dev_t);
		orphan_path(pp);
	}

	return 0;
}

int
uev_remove_path (char * devname, struct vectors * vecs)
{
	int i;
	struct path * pp;

	pp = find_path_by_dev(vecs->pathvec, devname);

	if (!pp) {
		condlog(3, "%s: not in pathvec");
		return 1;
	}

	if (pp->mpp && pp->state == PATH_UP)
		update_queue_mode_del_path(pp->mpp);

	condlog(2, "remove %s path checker", devname);
	i = find_slot(vecs->pathvec, (void *)pp);
	vector_del_slot(vecs->pathvec, i);
	free_path(pp);

	return 0;
}

int
show_paths (char ** r, int * len, struct vectors * vecs)
{
	int i;
	struct path * pp;
	char * c;
	char * reply;
	struct path_layout pl;

	get_path_layout(&pl, vecs->pathvec);
	reply = MALLOC(MAX_REPLY_LEN);

	if (!reply)
		return 1;

	c = reply;
	c += snprint_path_header(c, reply + MAX_REPLY_LEN - c,
				 PRINT_PATH_CHECKER, &pl);

	vector_foreach_slot(vecs->pathvec, pp, i)
		c += snprint_path(c, reply + MAX_REPLY_LEN - c,
			       	  PRINT_PATH_CHECKER, pp, &pl);

	*r = reply;
	*len = (int)(c - reply + 1);
	return 0;
}

int
show_maps (char ** r, int *len, struct vectors * vecs)
{
	int i;
	struct multipath * mpp;
	char * c;
	char * reply;
	struct map_layout ml;

	get_map_layout(&ml, vecs->mpvec);
	reply = MALLOC(MAX_REPLY_LEN);

	if (!reply)
		return 1;

	c = reply;
	c += snprint_map_header(c, reply + MAX_REPLY_LEN - c,
				PRINT_MAP_FAILBACK, &ml);

	vector_foreach_slot(vecs->mpvec, mpp, i)
		c += snprint_map(c, reply + MAX_REPLY_LEN - c,
			       	 PRINT_MAP_FAILBACK, mpp, &ml);

	*r = reply;
	*len = (int)(c - reply + 1);
	return 0;
}

int
dump_pathvec (char ** r, int * len, struct vectors * vecs)
{
	int i;
	struct path * pp;
	char * reply;
	char * p;

	*len = VECTOR_SIZE(vecs->pathvec) * sizeof(struct path);
	reply = (char *)MALLOC(*len);
	*r = reply;

	if (!reply)
		return 1;

	p = reply;

	vector_foreach_slot (vecs->pathvec, pp, i) {
		memcpy((void *)p, pp, sizeof(struct path));
		p += sizeof(struct path);
	}

	/* return negative to hint caller not to add "ok" to the dump */
	return -1;
}

static int
map_discovery (struct vectors * vecs)
{
	int i;
	struct multipath * mpp;

	if (dm_get_maps(vecs->mpvec, "multipath"))
		return 1;

	vector_foreach_slot (vecs->mpvec, mpp, i) {
		if (setup_multipath(vecs, mpp))
			return 1;
		mpp->minor = dm_get_minor(mpp->alias);
		start_waiter_thread(mpp, vecs);
	}

	return 0;
}

int
reconfigure (struct vectors * vecs)
{
	struct config * old = conf;
	struct multipath * mpp;
	struct path * pp;
	int i;

	conf = NULL;

	if (load_config(DEFAULT_CONFIGFILE)) {
		conf = old;
		condlog(2, "reconfigure failed, continue with old config");
		return 1;
	}
	conf->verbosity = old->verbosity;
	free_config(old);

	vector_foreach_slot (vecs->mpvec, mpp, i) {
		mpp->mpe = find_mpe(mpp->wwid);
		mpp->hwe = extract_hwe_from_path(mpp);
		adopt_paths(vecs, mpp);
		set_no_path_retry(mpp);
	}
	vector_foreach_slot (vecs->pathvec, pp, i) {
		select_checkfn(pp);
		select_getuid(pp);
		select_getprio(pp);
	}
	condlog(2, "reconfigured");
	return 0;
}

int
uxsock_trigger (char * str, char ** reply, int * len, void * trigger_data)
{
	struct vectors * vecs;
	int r;
	
	*reply = NULL;
	*len = 0;
	vecs = (struct vectors *)trigger_data;

	pthread_cleanup_push(cleanup_lock, vecs->lock);
	lock(vecs->lock);

	r = parse_cmd(str, reply, len, vecs);

	if (r > 0) {
		*reply = STRDUP("fail\n");
		*len = strlen(*reply) + 1;
		r = 1;
	}
	else if (!r && *len == 0) {
		*reply = STRDUP("ok\n");
		*len = strlen(*reply) + 1;
		r = 0;
	}
	/* else if (r < 0) leave *reply alone */

	lock_cleanup_pop(vecs->lock);
	return r;
}

static int
uev_discard(char * devpath)
{
	char a[10], b[10];

	/*
	 * keep only block devices, discard partitions
	 */
	if (sscanf(devpath, "/block/%10s", a) != 1 ||
	    sscanf(devpath, "/block/%10[^/]/%10s", a, b) == 2) {
		condlog(4, "discard event on %s", devpath);
		return 1;
	}
	return 0;
}

int 
uev_trigger (struct uevent * uev, void * trigger_data)
{
	int r = 0;
	char devname[32];
	struct vectors * vecs;

	vecs = (struct vectors *)trigger_data;

	if (uev_discard(uev->devpath))
		goto out;

	basename(uev->devpath, devname);
	lock(vecs->lock);

	/*
	 * device map add/remove event
	 */
	if (!strncmp(devname, "dm-", 3)) {
		if (!strncmp(uev->action, "add", 3)) {
			r = uev_add_map(devname, vecs);
			goto out;
		}
#if 0
		if (!strncmp(uev->action, "remove", 6)) {
			r = uev_remove_map(devname, vecs);
			goto out;
		}
#endif
		goto out;
	}
	
	/*
	 * path add/remove event
	 */
	if (blacklist(conf->blist, devname))
		goto out;

	if (!strncmp(uev->action, "add", 3)) {
		r = uev_add_path(devname, vecs);
		goto out;
	}
	if (!strncmp(uev->action, "remove", 6)) {
		r = uev_remove_path(devname, vecs);
		goto out;
	}

out:
	unlock(vecs->lock);
	return r;
}

static void *
ueventloop (void * ap)
{
	if (uevent_listen(&uev_trigger, ap))
		fprintf(stderr, "error starting uevent listener");
		
	return NULL;
}

static void *
uxlsnrloop (void * ap)
{
	if (load_keys())
		return NULL;
	
	if (alloc_handlers())
		return NULL;

	add_handler(LIST+PATHS, cli_list_paths);
	add_handler(LIST+MAPS, cli_list_maps);
	add_handler(ADD+PATH, cli_add_path);
	add_handler(DEL+PATH, cli_del_path);
	add_handler(ADD+MAP, cli_add_map);
	add_handler(DEL+MAP, cli_del_map);
	add_handler(SWITCH+MAP+GROUP, cli_switch_group);
	add_handler(DUMP+PATHVEC, cli_dump_pathvec);
	add_handler(RECONFIGURE, cli_reconfigure);

	uxsock_listen(&uxsock_trigger, ap);

	return NULL;
}

static int
exit_daemon (int status)
{
	if (status != 0)
		fprintf(stderr, "bad exit status. see daemon.log\n");

	condlog(3, "unlink pidfile");
	unlink(DEFAULT_PIDFILE);

	lock(&exit_mutex);
	pthread_cond_signal(&exit_cond);
	unlock(&exit_mutex);

	return status;
}

static void
fail_path (struct path * pp)
{
	if (!pp->mpp)
		return;

	condlog(2, "checker failed path %s in map %s",
		 pp->dev_t, pp->mpp->alias);

	dm_fail_path(pp->mpp->alias, pp->dev_t);
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

	if (dm_reinstate(pp->mpp->alias, pp->dev_t))
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
	if (!pp->pgindex)
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

	vector_foreach_slot (vecs->mpvec, mpp, i) {
		if (mpp && mpp->alias && !dm_map_present(mpp->alias)) {
			condlog(2, "%s: remove dead map", mpp->alias);
			remove_map(mpp, vecs);
			i--;
		}
	}
}

static void
defered_failback_tick (vector mpvec)
{
	struct multipath * mpp;
	int i;

	vector_foreach_slot (mpvec, mpp, i) {
		/*
		 * defered failback getting sooner
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
	int i;

	vector_foreach_slot (mpvec, mpp, i) {
		if (mpp->retry_tick) {
			condlog(4, "%s: Retrying.. No active path", mpp->alias);
			if(--mpp->retry_tick == 0) {
				dm_queue_if_no_path(mpp->alias, 0);
				condlog(2, "%s: Disable queueing", mpp->alias);
			}
		}
	}
}

static void *
checkerloop (void *ap)
{
	struct vectors *vecs;
	struct path *pp;
	int i, count = 0;
	int newstate;
	char checker_msg[MAX_CHECKER_MSG_SIZE];

	mlockall(MCL_CURRENT | MCL_FUTURE);

	memset(checker_msg, 0, MAX_CHECKER_MSG_SIZE);
	vecs = (struct vectors *)ap;

	condlog(2, "path checkers start up");

	/*
	 * init the path check interval
	 */
	vector_foreach_slot (vecs->pathvec, pp, i) {
		pp->checkint = conf->checkint;
	}

	while (1) {
		pthread_cleanup_push(cleanup_lock, vecs->lock);
		lock(vecs->lock);
		condlog(4, "tick");

		vector_foreach_slot (vecs->pathvec, pp, i) {
			if (!pp->mpp)
				continue;

			if (pp->tick && --pp->tick)
				continue; /* don't check this path yet */

			/*
			 * provision a next check soonest,
			 * in case we exit abnormaly from here
			 */
			pp->tick = conf->checkint;
			
			if (!pp->checkfn) {
				pathinfo(pp, conf->hwtable, DI_SYSFS);
				select_checkfn(pp);
			}

			if (!pp->checkfn) {
				condlog(0, "%s: checkfn is void", pp->dev);
				continue;
			}
			newstate = pp->checkfn(pp->fd, checker_msg,
					       &pp->checker_context);
			
			if (newstate != pp->state) {
				pp->state = newstate;
				LOG_MSG(1, checker_msg);

				/*
				 * upon state change, reset the checkint
				 * to the shortest delay
				 */
				pp->checkint = conf->checkint;

				if (newstate == PATH_DOWN ||
				    newstate == PATH_SHAKY) {
					/*
					 * proactively fail path in the DM
					 */
					fail_path(pp);

					/*
					 * cancel scheduled failback
					 */
					pp->mpp->failback_tick = 0;

					continue;
				}

				/*
				 * reinstate this path
				 */
				reinstate_path(pp);

				/*
				 * need to switch group ?
				 */
				update_multipath_strings(pp->mpp,
							 vecs->pathvec);

				/*
				 * schedule defered failback
				 */
				if (pp->mpp->pgfailback > 0)
					pp->mpp->failback_tick =
						pp->mpp->pgfailback + 1;
				else if (pp->mpp->pgfailback == -FAILBACK_IMMEDIATE &&
				    need_switch_pathgroup(pp->mpp, 1))
					switch_pathgroup(pp->mpp);

				/*
				 * if at least one path is up in a group, and
				 * the group is disabled, re-enable it
				 */
				if (newstate == PATH_UP)
					enable_group(pp);
			}
			else if (newstate == PATH_UP || newstate == PATH_GHOST) {
				LOG_MSG(4, checker_msg);
				/*
				 * double the next check delay.
				 * max at conf->max_checkint
				 */
				if (pp->checkint < (conf->max_checkint / 2))
					pp->checkint = 2 * pp->checkint;
				else
					pp->checkint = conf->max_checkint;

				pp->tick = pp->checkint;
				condlog(4, "%s: delay next check %is",
						pp->dev_t, pp->tick);

			}
			pp->state = newstate;

			/*
			 * path prio refreshing
			 */
			condlog(4, "path prio refresh");
			pathinfo(pp, conf->hwtable, DI_PRIO);

			if (need_switch_pathgroup(pp->mpp, 0)) {
				if (pp->mpp->pgfailback > 0)
					pp->mpp->failback_tick =
						pp->mpp->pgfailback + 1;
				else if (pp->mpp->pgfailback ==
						-FAILBACK_IMMEDIATE)
					switch_pathgroup(pp->mpp);
			}
		}
		defered_failback_tick(vecs->mpvec);
		retry_count_tick(vecs->mpvec);

		if (count)
			count--;
		else {
			condlog(4, "map garbage collection");
			mpvec_garbage_collector(vecs);
			count = MAPGCINT;
		}
		
		lock_cleanup_pop(vecs->lock);
		sleep(1);
	}
	return NULL;
}

static struct vectors *
init_paths (void)
{
	struct vectors * vecs;

	vecs = (struct vectors *)MALLOC(sizeof(struct vectors));

	if (!vecs)
		return NULL;

	vecs->lock = 
		(pthread_mutex_t *)MALLOC(sizeof(pthread_mutex_t));

	if (!vecs->lock)
		goto out;

	vecs->pathvec = vector_alloc();

	if (!vecs->pathvec)
		goto out1;
		
	vecs->mpvec = vector_alloc();

	if (!vecs->mpvec)
		goto out2;
	
	pthread_mutex_init(vecs->lock, NULL);

	return vecs;

out2:
	vector_free(vecs->pathvec);
out1:
	FREE(vecs->lock);
out:
	FREE(vecs);
	condlog(0, "failed to init paths");
	return NULL;
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

static void
sighup (int sig)
{
	condlog(3, "SIGHUP received");

#ifdef _DEBUG_
	dbg_free_final(NULL);
#endif
}

static void
sigend (int sig)
{
	exit_daemon(0);
}

static void
signal_init(void)
{
	signal_set(SIGHUP, sighup);
	signal_set(SIGINT, sigend);
	signal_set(SIGTERM, sigend);
	signal_set(SIGKILL, sigend);
}

static void
setscheduler (void)
{
        int res;
	static struct sched_param sched_param = {
		sched_priority: 99
	};

        res = sched_setscheduler (0, SCHED_RR, &sched_param);

        if (res == -1)
                condlog(LOG_WARNING, "Could not set SCHED_RR at priority 99");
	return;
}

static void
set_oom_adj (int val)
{
	FILE *fp;

	fp = fopen("/proc/self/oom_adj", "w");

	if (!fp)
		return;

	fprintf(fp, "%i", val);
	fclose(fp);
}
	
static int
child (void * param)
{
	pthread_t check_thr, uevent_thr, uxlsnr_thr;
	pthread_attr_t attr;
	struct vectors * vecs;

	mlockall(MCL_CURRENT | MCL_FUTURE);

	if (logsink)
		log_thread_start();

	condlog(2, "--------start up--------");
	condlog(2, "read " DEFAULT_CONFIGFILE);

	if (load_config(DEFAULT_CONFIGFILE))
		exit(1);

	setlogmask(LOG_UPTO(conf->verbosity + 3));

	/*
	 * fill the voids left in the config file
	 */
	if (!conf->checkint) {
		conf->checkint = CHECKINT;
		conf->max_checkint = MAX_CHECKINT;
	}

	if (pidfile_create(DEFAULT_PIDFILE, getpid())) {
		if (logsink)
			log_thread_stop();

		exit(1);
	}
	signal_init();
	setscheduler();
	set_oom_adj(-17);
	vecs = init_paths();

	if (!vecs)
		exit(1);

	if (sysfs_get_mnt_path(sysfs_path, FILE_NAME_SIZE)) {
		condlog(0, "can not find sysfs mount point");
		exit(1);
	}

	/*
	 * fetch paths and multipaths lists
	 * no paths and/or no multipaths are valid scenarii
	 * vectors maintenance will be driven by events
	 */
	path_discovery(vecs->pathvec, conf, DI_SYSFS | DI_WWID | DI_CHECKER);
	map_discovery(vecs);

	/*
	 * start threads
	 */
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 64 * 1024);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	
	pthread_create(&check_thr, &attr, checkerloop, vecs);
	pthread_create(&uevent_thr, &attr, ueventloop, vecs);
	pthread_create(&uxlsnr_thr, &attr, uxlsnrloop, vecs);

	pthread_cond_wait(&exit_cond, &exit_mutex);

	/*
	 * exit path
	 */
	lock(vecs->lock);
	remove_maps(vecs);
	free_pathvec(vecs->pathvec, FREE_PATHS);

	pthread_cancel(check_thr);
	pthread_cancel(uevent_thr);
	pthread_cancel(uxlsnr_thr);

	free_keys(keys);
	keys = NULL;
	free_handlers(handlers);
	handlers = NULL;
	free_polls();

	unlock(vecs->lock);
	pthread_mutex_destroy(vecs->lock);
	FREE(vecs->lock);
	vecs->lock = NULL;
	FREE(vecs);
	vecs = NULL;
	free_config(conf);
	conf = NULL;

	condlog(2, "--------shut down-------");
	
	if (logsink)
		log_thread_stop();

#ifdef _DEBUG_
	dbg_free_final(NULL);
#endif

	exit(0);
}

int
main (int argc, char *argv[])
{
	extern char *optarg;
	extern int optind;
	int arg;
	int err;
	
	logsink = 1;

	if (getuid() != 0) {
		fprintf(stderr, "need to be root\n");
		exit(1);
	}

	/* make sure we don't lock any path */
	chdir("/");
	umask(umask(077) | 022);

	conf = alloc_config();

	if (!conf)
		exit(1);

	while ((arg = getopt(argc, argv, ":dv:k::")) != EOF ) {
	switch(arg) {
		case 'd':
			logsink = 0;
			//debug=1; /* ### comment me out ### */
			break;
		case 'v':
			if (sizeof(optarg) > sizeof(char *) ||
			    !isdigit(optarg[0]))
				exit(1);

			conf->verbosity = atoi(optarg);
			break;
		case 'k':
			uxclnt(optarg);
			exit(0);
		default:
			;
		}
	}

	if (!logsink)
		err = 0;
	else
		err = fork();
	
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
