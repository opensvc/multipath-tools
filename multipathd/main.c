#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libdevmapper.h>
#include <signal.h>
#include <wait.h>
#include <sched.h>
#include <errno.h>
#include <sys/mount.h>
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

#include "main.h"
#include "copy.h"
#include "clone_platform.h"
#include "pidfile.h"
#include "uxlsnr.h"
#include "uxclnt.h"

#define FILE_NAME_SIZE 256
#define CMDSIZE 160
#define MAX_REPLY_LEN 1000

#define CALLOUT_DIR "/var/cache/multipathd"

#define LOG_MSG(a,b) \
	if (strlen(a)) { \
		condlog(1, "%s: %s", b, a); \
		memset(a, 0, MAX_CHECKER_MSG_SIZE); \
	}

#ifdef LCKDBG
#define lock(a) \
	fprintf(stderr, "%s:%s(%i) lock %p\n", __FILE__, __FUNCTION__, __LINE__, a); \
	pthread_mutex_lock(a)
#define unlock(a) \
	fprintf(stderr, "%s:%s(%i) unlock %p\n", __FILE__, __FUNCTION__, __LINE__, a); \
	pthread_mutex_unlock(a)
#else
#define lock(a) pthread_mutex_lock(a)
#define unlock(a) pthread_mutex_unlock(a)
#endif

/*
 * structs
 */
struct paths {
	pthread_mutex_t *lock;
	vector pathvec;
	vector mpvec;
};

struct event_thread {
	pthread_t *thread;
	int event_nr;
	char mapname[WWID_SIZE];
	struct paths *allpaths;
};

static void *
alloc_waiter (void)
{

	struct event_thread * wp;

	wp = MALLOC(sizeof(struct event_thread));

	if (!wp)
		return NULL;

	wp->thread = MALLOC(sizeof(pthread_t));

	if (!wp->thread)
		goto out;
		
	return wp;

out:
	free(wp);
	condlog(0, "failed to alloc waiter");
	return NULL;
}

static void
set_paths_owner (struct paths * allpaths, struct multipath * mpp)
{
	int i;
	struct path * pp;

	if (!mpp)
		return;

	vector_foreach_slot (allpaths->pathvec, pp, i) {
		if (!strncmp(mpp->wwid, pp->wwid, WWID_SIZE)) {
			condlog(4, "%s ownership set", pp->dev_t);
			pp->mpp = mpp;
		}
	}
}

static void
unset_paths_owner (struct paths * allpaths, struct multipath * mpp)
{
	int i;
	struct path * pp;

	vector_foreach_slot (allpaths->pathvec, pp, i) {
		if (pp->mpp == mpp) {
			condlog(4, "%s is orphaned", pp->dev_t);
			pp->mpp = NULL;
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

	if(disassemble_map(pathvec, mpp->params, mpp))
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
	if (update_multipath_table(mpp, pathvec))
		return 1;

	if (update_multipath_status(mpp))
		return 1;

	return 0;
}

static int
setup_multipath (struct paths * allpaths, struct multipath * mpp)
{
	char * wwid;

	wwid = get_mpe_wwid(mpp->alias);

	if (wwid) {
		strncpy(mpp->wwid, wwid, WWID_SIZE);
		wwid = NULL;
	} else
		strncpy(mpp->wwid, mpp->alias, WWID_SIZE);

	condlog(4, "discovered map %s", mpp->alias);

	if (update_multipath_strings(mpp, allpaths->pathvec))
		goto out;

	set_paths_owner(allpaths, mpp);
	mpp->mpe = find_mpe(mpp->wwid);
	select_pgfailback(mpp);

	return 0;
out:
	free_multipath(mpp, KEEP_PATHS);
	condlog(0, "failed to setup multipath");
	return 1;
}

static void
switch_pathgroup (struct multipath * mpp)
{
	struct pathgroup * pgp;
	struct path * pp;
	int i, j;
	
	if (!mpp || mpp->pgfailback == FAILBACK_MANUAL)
		return;
	/*
	 * Refresh path priority values
	 */
	vector_foreach_slot (mpp->pg, pgp, i)
		vector_foreach_slot (pgp->paths, pp, j)
			pathinfo(pp, conf->hwtable, DI_PRIO);

	select_path_group(mpp); /* sets mpp->nextpg */
	pgp = VECTOR_SLOT(mpp->pg, mpp->nextpg - 1);
	
	if (pgp && pgp->status != PGSTATE_ACTIVE) {
		dm_switchgroup(mpp->alias, mpp->nextpg);
		condlog(2, "%s: switch to path group #%i",
			 mpp->alias, mpp->nextpg);
	}
}

static int
update_multipath (struct paths *allpaths, char *mapname)
{
	struct multipath *mpp;
	struct pathgroup  *pgp;
	struct path *pp;
	int i, j;
	int r = 1;

	lock(allpaths->lock);
	mpp = find_mp(allpaths->mpvec, mapname);

	if (!mpp)
		goto out;

	free_pgvec(mpp->pg, KEEP_PATHS);
	mpp->pg = NULL;

	setup_multipath(allpaths, mpp);

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
	unlock(allpaths->lock);

	if (r)
		condlog(0, "failed to update multipath");

	return r;
}

/*
 * returns the reschedule delay
 * negative means *stop*
 */
static int
waiteventloop (struct event_thread * waiter)
{
	struct dm_task *dmt;
	int event_nr;
	int r = 1; /* upon problem reschedule 1s later */

	if (!waiter->event_nr)
		waiter->event_nr = dm_geteventnr(waiter->mapname);

	if (!(dmt = dm_task_create(DM_DEVICE_WAITEVENT)))
		goto out;

	if (!dm_task_set_name(dmt, waiter->mapname))
		goto out;

	if (waiter->event_nr && !dm_task_set_event_nr(dmt, waiter->event_nr))
		goto out;

	dm_task_no_open_count(dmt);

	dm_task_run(dmt);

	waiter->event_nr++;

	/*
	 * upon event ...
	 */
	while (1) {
		condlog(2, "devmap event (%i) on %s",
				waiter->event_nr, waiter->mapname);

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
		if (update_multipath(waiter->allpaths, waiter->mapname)) {
			r = -1; /* stop the thread */
			goto out;
		}
		event_nr = dm_geteventnr(waiter->mapname);

		if (waiter->event_nr == event_nr)
			break;

		waiter->event_nr = event_nr;
	}

out:
	dm_task_destroy(dmt);
	return r;
}

static void *
waitevent (void * et)
{
	int r;
	struct event_thread *waiter;

	mlockall(MCL_CURRENT | MCL_FUTURE);

	waiter = (struct event_thread *)et;
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	while (1) {
		r = waiteventloop(waiter);

		if (r < 0)
			break;

		sleep(r);
	}

	pthread_exit(waiter->thread);

	return NULL;
}

static void
free_waiter (struct event_thread * wp)
{
	free(wp->thread);
	free(wp);
}

static int
stop_waiter_thread (struct multipath * mpp, struct paths * allpaths)
{
	struct event_thread * wp;

	if (!mpp)
		return 0;

	wp = (struct event_thread *)mpp->waiter;

	if (!wp)
		return 1;

	condlog(2, "reap event checker : %s", wp->mapname);
	pthread_cancel(*wp->thread);
	free_waiter(wp);

	return 0;
}

static int
start_waiter_thread (struct multipath * mpp, struct paths * allpaths)
{
	pthread_attr_t attr;
	struct event_thread * wp;

	if (!mpp)
		return 0;

	if (pthread_attr_init(&attr))
		return 1;

	pthread_attr_setstacksize(&attr, 32 * 1024);
	wp = alloc_waiter();

	if (!wp)
		return 1;

	mpp->waiter = (void *)wp;
	strncpy(wp->mapname, mpp->alias, WWID_SIZE);
	wp->allpaths = allpaths;

	if (pthread_create(wp->thread, &attr, waitevent, wp)) {
		condlog(0, "%s: cannot create event checker", wp->mapname);
		goto out;
	}
	condlog(2, "%s: event checker started", wp->mapname);

	return 0;
out:
	free_waiter(wp);
	mpp->waiter = NULL;
	condlog(0, "failed to start waiter thread");
	return 1;
}

static void
remove_map (struct multipath * mpp, struct paths * allpaths)
{
	int i;

	stop_waiter_thread(mpp, allpaths);
	i = find_slot(allpaths->mpvec, (void *)mpp);
	vector_del_slot(allpaths->mpvec, i);
	free_multipath(mpp, KEEP_PATHS);
	unset_paths_owner(allpaths, mpp);
}

static int
uev_add_map (char * devname, struct paths * allpaths)
{
	int major, minor;
	char dev_t[BLK_DEV_SIZE];
	char * buff;
	struct multipath * mpp;

	if (sysfs_get_dev(sysfs_path, devname, dev_t, BLK_DEV_SIZE))
		return 1;

	if (sscanf(dev_t, "%d:%d", &major, &minor) != 2)
		return 1;

	buff = dm_mapname(major, minor, "multipath");
		
	if (!buff)
		return 1;
	
	mpp = find_mp(allpaths->mpvec, buff);

	if (mpp) {
		/*
		 * devmap already in mpvec
		 * but remove DM uevent are somewhet unreliable
		 * so for now consider safer to remove and re-add the map
		 */
		condlog(2, "%s: remove dead config", mpp->alias);
		remove_map(mpp, allpaths);
		mpp = NULL;
	}
	if (!mpp) {
		mpp = alloc_multipath();

		if (!mpp)
			return 1;

		mpp->minor = minor;
		mpp->alias = MALLOC(strlen(buff) + 1);

		if (!mpp->alias)
			goto out;

		strncat(mpp->alias, buff, strlen(buff));

		dm_get_map(mpp->alias, &mpp->size, mpp->params);
		dm_get_status(mpp->alias, mpp->status);

		if (setup_multipath(allpaths, mpp))
			return 1; /* mpp freed in setup_multipath */

		if (!vector_alloc_slot(allpaths->mpvec))
			goto out;

		vector_set_slot(allpaths->mpvec, mpp);
		set_paths_owner(allpaths, mpp);

		if (start_waiter_thread(mpp, allpaths))
			goto out;
	}
	return 0;
out:
	free_multipath(mpp, KEEP_PATHS);
	return 1;
}

static int
uev_remove_map (char * devname, struct paths * allpaths)
{
	int minor;
	struct multipath * mpp;

	minor = atoi(devname + 3);
	mpp = find_mp_by_minor(allpaths->mpvec, minor);

	if (mpp)
		remove_map(mpp, allpaths);

	return 0;
}

static int
uev_add_path (char * devname, struct paths * allpaths)
{
	struct path * pp;

	pp = find_path_by_dev(allpaths->pathvec, devname);

	if (pp) {
		condlog(3, "%s: already in pathvec");
		return 0;
	}
	condlog(2, "add %s path checker", devname);
	pp = store_pathinfo(allpaths->pathvec, conf->hwtable,
		       devname, DI_SYSFS | DI_WWID);

	if (!pp)
		return 1;

	pp->mpp = find_mp_by_wwid(allpaths->mpvec, pp->wwid);
	condlog(4, "%s: ownership set to %s", pp->dev_t, pp->mpp->alias);

	return 0;
}

static int
uev_remove_path (char * devname, struct paths * allpaths)
{
	int i;
	struct path * pp;

	pp = find_path_by_dev(allpaths->pathvec, devname);

	if (!pp) {
		condlog(3, "%s: not in pathvec");
		return 0;
	}
	condlog(2, "remove %s path checker", devname);
	i = find_slot(allpaths->pathvec, (void *)pp);
	vector_del_slot(allpaths->pathvec, i);
	free_path(pp);

	return 0;
}

static char *
show_paths (struct paths * allpaths)
{
	int i, j, k;
	struct path * pp;
	char * c;
	char * reply;

	reply = MALLOC(MAX_REPLY_LEN);

	if (!reply)
		return NULL;

	c = reply;
	c += sprintf(c, "\n");

	vector_foreach_slot(allpaths->pathvec, pp, i) {
		c += sprintf(c, "%10s: ", pp->dev);
		c += sprintf(c, "state %i, ", pp->state);

		j = pp->tick;
		k = pp->checkint - pp->tick;
		c += sprintf(c, "%3i/%3i ", j, pp->checkint);

		while (j-- > 0)
			c += sprintf(c, "X");


		while (k-- > 0)
			c += sprintf(c, ".");

		c += sprintf(c, "\n");
	}

	return reply;
}

static char *
show_maps (struct paths * allpaths)
{
	int i, j, k;
	struct multipath * mpp;
	char * c;
	char * reply;

	reply = MALLOC(MAX_REPLY_LEN);

	if (!reply)
		return NULL;

	c = reply;
	c += sprintf(c, "\n");

	vector_foreach_slot(allpaths->mpvec, mpp, i) {
		c += sprintf(c, "%20s: ", mpp->alias);

		j = mpp->failback_tick;
		k = mpp->pgfailback - mpp->failback_tick;
		c += sprintf(c, "%3i/%3i ", j, mpp->pgfailback);

		while (j-- > 0)
			c += sprintf(c, "X");


		while (k-- > 0)
			c += sprintf(c, ".");

		c += sprintf(c, "\n");
	}

	return reply;
}

char *
uxsock_trigger (char * str, void * trigger_data)
{
	struct paths * allpaths;
	char * reply = NULL;

	allpaths = (struct paths *)trigger_data;

	lock(allpaths->lock);

	if (*str == 'l' && *(str + 1) == 'p')
		reply = show_paths(allpaths);

	else if (*str == 'l' && *(str + 1) == 'm')
		reply = show_maps(allpaths);

	else if (*str == 'r' && *(str + 1) == 'p')
		uev_remove_path(str + 3, allpaths);

	else if (*str == 'a' && *(str + 1) == 'p')
		uev_add_path(str + 3, allpaths);

	else if (*str == 'r' && *(str + 1) == 'm')
		uev_remove_map(str + 3, allpaths);

	else if (*str == 'a' && *(str + 1) == 'm')
		uev_add_map(str + 3, allpaths);

	if (!reply)
		asprintf(&reply, "ok\n");

	unlock(allpaths->lock);

	return reply;
}

int 
uev_trigger (struct uevent * uev, void * trigger_data)
{
	int r = 0;
	char devname[32];
	struct paths * allpaths;

	allpaths = (struct paths *)trigger_data;
	lock(allpaths->lock);

	if (strncmp(uev->devpath, "/block", 6))
		goto out;

	basename(uev->devpath, devname);

	/*
	 * device map add/remove event
	 */
	if (!strncmp(devname, "dm-", 3)) {
		condlog(2, "%s %s devmap", uev->action, devname);

		if (!strncmp(uev->action, "add", 3)) {
			r = uev_add_map(devname, allpaths);
			goto out;
		}
		if (!strncmp(uev->action, "remove", 6)) {
			r = uev_remove_map(devname, allpaths);
			goto out;
		}
		goto out;
	}
	
	/*
	 * path add/remove event
	 */
	if (blacklist(conf->blist, devname))
		goto out;

	if (!strncmp(uev->action, "add", 3)) {
		r = uev_add_path(devname, allpaths);
		goto out;
	}
	if (!strncmp(uev->action, "remove", 6)) {
		r = uev_remove_path(devname, allpaths);
		goto out;
	}

out:
	FREE(uev);
	unlock(allpaths->lock);
	return r;
}

static void *
ueventloop (void * ap)
{
	uevent_listen(&uev_trigger, ap);

	return NULL;
}

static void *
uxlsnrloop (void * ap)
{
	uxsock_listen(&uxsock_trigger, ap);

	return NULL;
}

static void
strvec_free (vector vec)
{
	int i;
	char * str;

	vector_foreach_slot (vec, str, i)
		if (str)
			FREE(str);

	vector_free(vec);
}

static int
exit_daemon (int status)
{
	if (status != 0)
		fprintf(stderr, "bad exit status. see daemon.log\n");

	condlog(3, "umount ramfs");
	umount(CALLOUT_DIR);

	condlog(3, "unlink pidfile");
	unlink(DEFAULT_PIDFILE);

	condlog(2, "--------shut down-------");
	
	if (logsink)
		log_thread_stop();

	exit(status);
}

/*
 * caller must have locked the path list before calling that function
 */
static int
get_dm_mpvec (struct paths * allpaths)
{
	int i;
	struct multipath * mpp;

	if (dm_get_maps(allpaths->mpvec, "multipath"))
		return 1;

	vector_foreach_slot (allpaths->mpvec, mpp, i) {
		setup_multipath(allpaths, mpp);
		mpp->minor = dm_get_minor(mpp->alias);
		start_waiter_thread(mpp, allpaths);
	}

	return 0;
}

static void
fail_path (struct path * pp)
{
	if (!pp->mpp)
		return;

	condlog(2, "checker failed path %s in map %s",
		 pp->dev_t, pp->mpp->alias);

	dm_fail_path(pp->mpp->alias, pp->dev_t);
}

/*
 * caller must have locked the path list before calling that function
 */
static void
reinstate_path (struct path * pp)
{
	if (pp->mpp) {
		if (dm_reinstate(pp->mpp->alias, pp->dev_t))
			condlog(0, "%s: reinstate failed", pp->dev_t);
		else
			condlog(2, "%s: reinstated", pp->dev_t);
	}
}

static void *
checkerloop (void *ap)
{
	struct paths *allpaths;
	struct path *pp;
	int i;
	int newstate;
	char checker_msg[MAX_CHECKER_MSG_SIZE];

	mlockall(MCL_CURRENT | MCL_FUTURE);

	memset(checker_msg, 0, MAX_CHECKER_MSG_SIZE);
	allpaths = (struct paths *)ap;

	condlog(2, "path checkers start up");

	while (1) {
		lock(allpaths->lock);
		condlog(4, "tick");

		vector_foreach_slot (allpaths->pathvec, pp, i) {
			if (pp->tick) {
				/*
				 * don't check this path yet
				 */
				pp->tick--;
				continue;
			}

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
				LOG_MSG(checker_msg, pp->dev_t);

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
					if (pp->mpp)
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
							 allpaths->pathvec);

				/*
				 * schedule defered failback
				 */
				if (pp->mpp && pp->mpp->pgfailback > 0)
					pp->mpp->failback_tick =
						pp->mpp->pgfailback;

				if (pp->mpp &&
				    pp->mpp->pgfailback == FAILBACK_IMMEDIATE)
					switch_pathgroup(pp->mpp);
			}
			else if (newstate == PATH_UP || newstate == PATH_GHOST) {
				/*
				 * PATH_UP for last two checks
				 * defered failback getting sooner
				 */
				if (pp->mpp && pp->mpp->pgfailback > 0) {
					if (pp->mpp->failback_tick > 0) {
						pp->mpp->failback_tick--;

						if (!pp->mpp->failback_tick)
							switch_pathgroup(pp->mpp);
					}
				}
				
				/*
				 * and double the next check delay.
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
		}
		unlock(allpaths->lock);
		sleep(1);
	}
	return NULL;
}

static struct paths *
init_paths (void)
{
	struct paths *allpaths;

	allpaths = MALLOC(sizeof(struct paths));

	if (!allpaths)
		return NULL;

	allpaths->lock = 
		(pthread_mutex_t *)MALLOC(sizeof(pthread_mutex_t));

	if (!allpaths->lock)
		goto out;

	allpaths->pathvec = vector_alloc();

	if (!allpaths->pathvec)
		goto out1;
		
	allpaths->mpvec = vector_alloc();

	if (!allpaths->mpvec)
		goto out2;
	
	pthread_mutex_init(allpaths->lock, NULL);

	return allpaths;

out2:
	vector_free(allpaths->pathvec);
out1:
	FREE(allpaths->lock);
out:
	FREE(allpaths);
	condlog(0, "failed to init paths");
	return NULL;
}

/*
 * this logic is all about keeping callouts working in case of
 * system disk outage (think system over SAN)
 * this needs the clone syscall, so don't bother if not present
 * (Debian Woody)
 */
#ifdef CLONE_NEWNS
static int
prepare_namespace(void)
{
	mode_t mode = S_IRWXU;
	struct stat *buf;
	char ramfs_args[64];
	int i;
	int fd;
	char * bin;
	size_t size = 10;
	struct stat statbuf;
	
	buf = MALLOC(sizeof(struct stat));

	/*
	 * create a temp mount point for ramfs
	 */
	if (stat(CALLOUT_DIR, buf) < 0) {
		if (mkdir(CALLOUT_DIR, mode) < 0) {
			condlog(0, "cannot create " CALLOUT_DIR);
			return -1;
		}
		condlog(4, "created " CALLOUT_DIR);
	}

	/*
	 * compute the optimal ramdisk size
	 */
	vector_foreach_slot (conf->binvec, bin,i) {
		if ((fd = open(bin, O_RDONLY)) < 0) {
			condlog(0, "cannot open %s", bin);
			return -1;
		}
		if (fstat(fd, &statbuf) < 0) {
			condlog(0, "cannot stat %s", bin);
			return -1;
		}
		size += statbuf.st_size;
		close(fd);
	}
	condlog(3, "ramfs maxsize is %u", (unsigned int) size);
	
	/*
	 * mount the ramfs
	 */
	if (safe_sprintf(ramfs_args, "maxsize=%u", (unsigned int) size)) {
		fprintf(stderr, "ramfs_args too small\n");
		return -1;
	}
	if (mount(NULL, CALLOUT_DIR, "ramfs", MS_SYNCHRONOUS, ramfs_args) < 0) {
		condlog(0, "cannot mount ramfs on " CALLOUT_DIR);
		return -1;
	}
	condlog(4, "mount ramfs on " CALLOUT_DIR);

	/*
	 * populate the ramfs with callout binaries
	 */
	vector_foreach_slot (conf->binvec, bin,i) {
		if (copytodir(bin, CALLOUT_DIR) < 0) {
			condlog(0, "cannot copy %s in ramfs", bin);
			exit_daemon(1);
		}
		condlog(4, "cp %s in ramfs", bin);
	}
	strvec_free(conf->binvec);

	/*
	 * bind the ramfs to :
	 * /sbin : default home of multipath ...
	 * /bin  : default home of scsi_id ...
	 * /tmp  : home of scsi_id temp files
	 */
	if (mount(CALLOUT_DIR, "/sbin", NULL, MS_BIND, NULL) < 0) {
		condlog(0, "cannot bind ramfs on /sbin");
		return -1;
	}
	condlog(4, "bind ramfs on /sbin");
	if (mount(CALLOUT_DIR, "/bin", NULL, MS_BIND, NULL) < 0) {
		condlog(0, "cannot bind ramfs on /bin");
		return -1;
	}
	condlog(4, "bind ramfs on /bin");
	if (mount(CALLOUT_DIR, "/tmp", NULL, MS_BIND, NULL) < 0) {
		condlog(0, "cannot bind ramfs on /tmp");
		return -1;
	}
	condlog(4, "bind ramfs on /tmp");

	return 0;
}
#endif

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
	condlog(2, "SIGHUP received");

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
	struct paths * allpaths;

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
	if (!conf->binvec) {
		conf->binvec = vector_alloc();
		push_callout("/sbin/scsi_id");
	}
	if (!conf->multipath) {
		conf->multipath = MULTIPATH;
		push_callout(conf->multipath);
	}
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
	allpaths = init_paths();

	if (!allpaths)
		exit(1);

	if (sysfs_get_mnt_path(sysfs_path, FILE_NAME_SIZE)) {
		condlog(0, "can not find sysfs mount point");
		exit(1);
	}

#ifdef CLONE_NEWNS
	if (prepare_namespace() < 0) {
		condlog(0, "cannot prepare namespace");
		exit_daemon(1);
	}
#endif

	/*
	 * fetch paths and multipaths lists
	 * no paths and/or no multipaths are valid scenarii
	 * vectors maintenance will be driven by events
	 */
	path_discovery(allpaths->pathvec, conf, DI_SYSFS | DI_WWID);
	get_dm_mpvec(allpaths);

	/*
	 * start threads
	 */
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 64 * 1024);
	
	pthread_create(&check_thr, &attr, checkerloop, allpaths);
	pthread_create(&uevent_thr, &attr, ueventloop, allpaths);
	pthread_create(&uxlsnr_thr, &attr, uxlsnrloop, allpaths);
	pthread_join(check_thr, NULL);
	pthread_join(uevent_thr, NULL);
	pthread_join(uxlsnr_thr, NULL);

	return 0;
}

int
main (int argc, char *argv[])
{
	extern char *optarg;
	extern int optind;
	int arg;
	int err;
	void * child_stack;
	
	logsink = 1;

	if (getuid() != 0) {
		fprintf(stderr, "need to be root\n");
		exit(1);
	}

	/* make sure we don't lock any path */
	chdir("/");
	umask(umask(077) | 022);

	child_stack = (void *)malloc(CHILD_STACK_SIZE);

	if (!child_stack)
		exit(1);

	conf = alloc_config();

	if (!conf)
		exit(1);

	while ((arg = getopt(argc, argv, ":dv:k::")) != EOF ) {
	switch(arg) {
		case 'd':
			logsink = 0;
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

#ifdef CLONE_NEWNS	/* recent systems have clone() */

#    if defined(__hppa__) || defined(__powerpc64__)
	err = clone(child, child_stack, CLONE_NEWNS, NULL);
#    elif defined(__ia64__)
	err = clone2(child, child_stack,
		     CHILD_STACK_SIZE, CLONE_NEWNS, NULL,
		     NULL, NULL, NULL);
#    else
	err = clone(child, child_stack + CHILD_STACK_SIZE, CLONE_NEWNS, NULL);
#    endif
	if (err < 0)
		exit (1);

	exit(0);
#else			/* older system fallback to fork() */
	err = fork();
	
	if (err < 0)
		exit (1);

	return (child(child_stack));
#endif

}
