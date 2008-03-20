/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 * Copyright (c) 2005 Kiyoshi Ueda, NEC
 * Copyright (c) 2005 Benjamin Marzinski, Redhat
 * Copyright (c) 2005 Edward Goggin, EMC
 */
#include <unistd.h>
#include <sys/stat.h>
#include <libdevmapper.h>
#include <wait.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <limits.h>

/*
 * libcheckers
 */
#include <checkers.h>

/*
 * libmultipath
 */
#include <parser.h>
#include <vector.h>
#include <memory.h>
#include <config.h>
#include <util.h>
#include <hwtable.h>
#include <defaults.h>
#include <structs.h>
#include <callout.h>
#include <blacklist.h>
#include <structs_vec.h>
#include <dmparser.h>
#include <devmapper.h>
#include <sysfs.h>
#include <dict.h>
#include <discovery.h>
#include <debug.h>
#include <propsel.h>
#include <uevent.h>
#include <switchgroup.h>
#include <print.h>
#include <configure.h>
#include <prio.h>
#include <pgpolicies.h>

#include "main.h"
#include "pidfile.h"
#include "uxlsnr.h"
#include "uxclnt.h"
#include "cli.h"
#include "cli_handlers.h"
#include "lock.h"
#include "waiter.h"

#define FILE_NAME_SIZE 256
#define CMDSIZE 160

#define LOG_MSG(a,b) \
	if (strlen(b)) condlog(a, "%s: %s - %s", pp->mpp->alias, pp->dev, b);

pthread_cond_t exit_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t exit_mutex = PTHREAD_MUTEX_INITIALIZER;

int logsink;

/*
 * global copy of vecs for use in sig handlers
 */
struct vectors * gvecs;

static int
need_switch_pathgroup (struct multipath * mpp, int refresh)
{
	struct pathgroup * pgp;
	struct path * pp;
	unsigned int i, j;

	if (!mpp || mpp->pgfailback == -FAILBACK_MANUAL)
		return 0;

	/*
	 * Refresh path priority values
	 */
	if (refresh)
		vector_foreach_slot (mpp->pg, pgp, i)
			vector_foreach_slot (pgp->paths, pp, j)
				pathinfo(pp, conf->hwtable, DI_PRIO);

	mpp->bestpg = select_path_group(mpp);

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
coalesce_maps(struct vectors *vecs, vector nmpv)
{
	struct multipath * ompp;
	vector ompv = vecs->mpvec;
	unsigned int i;
	int j;

	vector_foreach_slot (ompv, ompp, i) {
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
				if (!vector_alloc_slot(nmpv))
					return 1;

				vector_set_slot(nmpv, ompp);
				setup_multipath(vecs, ompp);

				if ((j = find_slot(ompv, (void *)ompp)) != -1)
					vector_del_slot(ompv, j);

				continue;
			}
			else {
				dm_lib_release();
				condlog(2, "%s devmap removed", ompp->alias);
			}
		}
	}
	return 0;
}

void
sync_map_state(struct multipath *mpp)
{
	struct pathgroup *pgp;
	struct path *pp;
	unsigned int i, j;

	if (!mpp->pg)
		return;

	vector_foreach_slot (mpp->pg, pgp, i){
		vector_foreach_slot (pgp->paths, pp, j){
			if (pp->state == PATH_UNCHECKED || 
			    pp->state == PATH_WILD)
				continue;
			if ((pp->dmstate == PSTATE_FAILED ||
			     pp->dmstate == PSTATE_UNDEF) &&
			    (pp->state == PATH_UP || pp->state == PATH_GHOST))
				dm_reinstate_path(mpp->alias, pp->dev_t);
			else if ((pp->dmstate == PSTATE_ACTIVE ||
				  pp->dmstate == PSTATE_UNDEF) &&
				 (pp->state == PATH_DOWN ||
				  pp->state == PATH_SHAKY))
				dm_fail_path(mpp->alias, pp->dev_t);
		}
	}
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
flush_map(struct multipath * mpp, struct vectors * vecs)
{
	/*
	 * clear references to this map before flushing so we can ignore
	 * the spurious uevent we may generate with the dm_flush_map call below
	 */
	if (dm_flush_map(mpp->alias)) {
		/*
		 * May not really be an error -- if the map was already flushed
		 * from the device mapper by dmsetup(8) for instance.
		 */
		condlog(0, "%s: can't flush", mpp->alias);
		return 1;
	}
	else {
		dm_lib_release();
		condlog(2, "%s: devmap removed", mpp->alias);
	}

	orphan_paths(vecs->pathvec, mpp);
	remove_map_and_stop_waiter(mpp, vecs, 1);

	return 0;
}

static int
uev_add_map (struct sysfs_device * dev, struct vectors * vecs)
{
	condlog(2, "%s: add map (uevent)", dev->kernel);
	return ev_add_map(dev, vecs);
}

int
ev_add_map (struct sysfs_device * dev, struct vectors * vecs)
{
	char * alias;
	char *dev_t;
	int major, minor;
	char * refwwid;
	struct multipath * mpp;
	int map_present;
	int r = 1;

	dev_t = sysfs_attr_get_value(dev->devpath, "dev");

	if (!dev_t || sscanf(dev_t, "%d:%d", &major, &minor) != 2)
		return 1;

	alias = dm_mapname(major, minor);

	if (!alias)
		return 1;

	map_present = dm_map_present(alias);

	if (map_present && dm_type(alias, TGT_MPATH) <= 0) {
		condlog(4, "%s: not a multipath map", alias);
		FREE(alias);
		return 0;
	}

	mpp = find_mp_by_alias(vecs->mpvec, alias);

	if (mpp) {
		/*
		 * Not really an error -- we generate our own uevent
		 * if we create a multipath mapped device as a result
		 * of uev_add_path
		 */
		condlog(0, "%s: devmap already registered",
			dev->kernel);
		FREE(alias);
		return 0;
	}

	/*
	 * now we can register the map
	 */
	if (map_present && (mpp = add_map_without_path(vecs, minor, alias))) {
		sync_map_state(mpp);
		condlog(2, "%s: devmap %s added", alias, dev->kernel);
		return 0;
	}
	refwwid = get_refwwid(dev->kernel, DEV_DEVMAP, vecs->pathvec);

	if (refwwid) {
		r = coalesce_paths(vecs, NULL, refwwid, 0);
		dm_lib_release();
	}

	if (!r)
		condlog(2, "%s: devmap %s added", alias, dev->kernel);
	else
		condlog(0, "%s: uev_add_map %s failed", alias, dev->kernel);

	FREE(refwwid);
	FREE(alias);
	return r;
}

static int
uev_remove_map (struct sysfs_device * dev, struct vectors * vecs)
{
	condlog(2, "%s: remove map (uevent)", dev->kernel);
	return ev_remove_map(dev->kernel, vecs);
}

int
ev_remove_map (char * devname, struct vectors * vecs)
{
	struct multipath * mpp;

	mpp = find_mp_by_str(vecs->mpvec, devname);

	if (!mpp) {
		condlog(2, "%s: devmap not registered, can't remove",
			devname);
		return 0;
	}
	flush_map(mpp, vecs);

	return 0;
}

static int
uev_umount_map (struct sysfs_device * dev, struct vectors * vecs)
{
	struct multipath * mpp;

	condlog(2, "%s: umount map (uevent)", dev->kernel);

	mpp = find_mp_by_str(vecs->mpvec, dev->kernel);

	if (!mpp)
		return 0;

	update_mpp_paths(mpp, vecs->pathvec);
	verify_paths(mpp, vecs, NULL);

	if (!VECTOR_SIZE(mpp->paths))
		flush_map(mpp, vecs);

	return 0;
}

static int
uev_add_path (struct sysfs_device * dev, struct vectors * vecs)
{
	condlog(2, "%s: add path (uevent)", dev->kernel);
	return (ev_add_path(dev->kernel, vecs) != 1)? 0 : 1;
}


/*
 * returns:
 * 0: added
 * 1: error
 * 2: blacklisted
 */
int
ev_add_path (char * devname, struct vectors * vecs)
{
	struct multipath * mpp;
	struct path * pp;
	char empty_buff[WWID_SIZE] = {0};
	char params[PARAMS_SIZE] = {0};

	if (strstr(devname, "..") != NULL) {
		/*
		 * Don't allow relative device names in the pathvec
		 */
		condlog(0, "%s: path name is invalid", devname);
		return 1;
	}

	pp = find_path_by_dev(vecs->pathvec, devname);

	if (pp) {
		condlog(0, "%s: spurious uevent, path already in pathvec",
			devname);
		if (pp->mpp)
			return 0;
	}
	else {
		/*
		 * get path vital state
		 */
		if (!(pp = store_pathinfo(vecs->pathvec, conf->hwtable,
		      devname, DI_ALL))) {
			condlog(0, "%s: failed to store path info", devname);
			return 1;
		}
		pp->checkint = conf->checkint;
	}

	/*
	 * need path UID to go any further
	 */
	if (memcmp(empty_buff, pp->wwid, WWID_SIZE) == 0) {
		condlog(0, "%s: failed to get path uid", devname);
		goto fail; /* leave path added to pathvec */
	}
	if (filter_path(conf, pp) > 0){
		int i = find_slot(vecs->pathvec, (void *)pp);
		if (i != -1)
			vector_del_slot(vecs->pathvec, i);
		free_path(pp);
		return 2;
	}
	mpp = pp->mpp = find_mp_by_wwid(vecs->mpvec, pp->wwid);
rescan:
	if (mpp) {
		if ((!pp->size) || (mpp->size != pp->size)) {
			if (!pp->size)
				condlog(0, "%s: failed to add new path %s, "
					"device size is 0",
					devname, pp->dev);
			else
				condlog(0, "%s: failed to add new path %s, "
					"device size mismatch",
					devname, pp->dev);
			int i = find_slot(vecs->pathvec, (void *)pp);
			if (i != -1)
				vector_del_slot(vecs->pathvec, i);
			free_path(pp);
			return 1;
		}

		condlog(4,"%s: adopting all paths for path %s",
			mpp->alias, pp->dev);
		if (adopt_paths(vecs->pathvec, mpp, 1))
			goto fail; /* leave path added to pathvec */

		verify_paths(mpp, vecs, NULL);
		mpp->flush_on_last_del = FLUSH_UNDEF;
		mpp->action = ACT_RELOAD;
	}
	else {
		if (!pp->size) {
			condlog(0, "%s: failed to create new map,"
				" %s device size is 0 ", devname, pp->dev);
			int i = find_slot(vecs->pathvec, (void *)pp);
			if (i != -1)
				vector_del_slot(vecs->pathvec, i);
			free_path(pp);
			return 1;
		}

		condlog(4,"%s: creating new map", pp->dev);
		if ((mpp = add_map_with_path(vecs, pp, 1)))
			mpp->action = ACT_CREATE;
		else
			goto fail; /* leave path added to pathvec */
	}

	/*
	 * push the map to the device-mapper
	 */
	if (setup_map(mpp, params, PARAMS_SIZE)) {
		condlog(0, "%s: failed to setup map for addition of new "
			"path %s", mpp->alias, devname);
		goto fail_map;
	}
	/*
	 * reload the map for the multipath mapped device
	 */
	if (domap(mpp, params) <= 0) {
		condlog(0, "%s: failed in domap for addition of new "
			"path %s", mpp->alias, devname);
		/*
		 * deal with asynchronous uevents :((
		 */
		if (mpp->action == ACT_RELOAD) {
			condlog(0, "%s: uev_add_path sleep", mpp->alias);
			sleep(1);
			update_mpp_paths(mpp, vecs->pathvec);
			goto rescan;
		}
		else
			goto fail_map;
	}
	dm_lib_release();

	/*
	 * update our state from kernel regardless of create or reload
	 */
	if (setup_multipath(vecs, mpp))
		goto fail_map;

	sync_map_state(mpp);

	if (mpp->action == ACT_CREATE &&
	    start_waiter_thread(mpp, vecs))
			goto fail_map;

	condlog(2, "%s path added to devmap %s", devname, mpp->alias);
	return 0;

fail_map:
	remove_map(mpp, vecs, 1);
fail:
	orphan_path(pp);
	return 1;
}

static int
uev_remove_path (struct sysfs_device * dev, struct vectors * vecs)
{
	int retval;

	condlog(2, "%s: remove path (uevent)", dev->kernel);
	retval = ev_remove_path(dev->kernel, vecs);
	if (!retval)
		sysfs_device_put(dev);

	return retval;
}

int
ev_remove_path (char * devname, struct vectors * vecs)
{
	struct multipath * mpp;
	struct path * pp;
	int i, retval = 0;
	char params[PARAMS_SIZE] = {0};

	pp = find_path_by_dev(vecs->pathvec, devname);

	if (!pp) {
		/* Not an error; path might have been purged earlier */
		condlog(0, "%s: path already removed", devname);
		return 0;
	}

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
		if ((i = find_slot(mpp->paths, (void *)pp)) != -1)
			vector_del_slot(mpp->paths, i);

		/*
		 * remove the map IFF removing the last path
		 */
		if (VECTOR_SIZE(mpp->paths) == 0) {
			char alias[WWID_SIZE];

			/*
			 * flush_map will fail if the device is open
			 */
			strncpy(alias, mpp->alias, WWID_SIZE);
			if (mpp->flush_on_last_del == FLUSH_ENABLED) {
				condlog(2, "%s Last path deleted, disabling queueing", mpp->alias);
				mpp->retry_tick = 0;
				mpp->no_path_retry = NO_PATH_RETRY_FAIL;
				mpp->flush_on_last_del = FLUSH_IN_PROGRESS;
				dm_queue_if_no_path(mpp->alias, 0);
			}
			if (!flush_map(mpp, vecs)) {
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

		if (setup_map(mpp, params, PARAMS_SIZE)) {
			condlog(0, "%s: failed to setup map for"
				" removal of path %s", mpp->alias,
				devname);
			goto fail;
		}
		/*
		 * reload the map
		 */
		mpp->action = ACT_RELOAD;
		if (domap(mpp, params) <= 0) {
			condlog(0, "%s: failed in domap for "
				"removal of path %s",
				mpp->alias, devname);
			retval = 1;
		} else {
			/*
			 * update our state from kernel
			 */
			if (setup_multipath(vecs, mpp)) {
				goto fail;
			}
			sync_map_state(mpp);

			condlog(2, "%s: path removed from map %s",
				devname, mpp->alias);
		}
	}

out:
	if ((i = find_slot(vecs->pathvec, (void *)pp)) != -1)
		vector_del_slot(vecs->pathvec, i);

	free_path(pp);

	return retval;

fail:
	remove_map_and_stop_waiter(mpp, vecs, 1);
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
		if (setup_multipath(vecs, mpp))
			return 1;

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

	pthread_cleanup_push(cleanup_lock, &vecs->lock);
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
	char *tmp;
	char a[11], b[11];

	/*
	 * keep only block devices, discard partitions
	 */
	tmp = strstr(devpath, "/block/");
	if (tmp == NULL){
		condlog(4, "no /block/ in '%s'", devpath);
		return 1;
	}
	if (sscanf(tmp, "/block/%10s", a) != 1 ||
	    sscanf(tmp, "/block/%10[^/]/%10s", a, b) == 2) {
		condlog(4, "discard event on %s", devpath);
		return 1;
	}
	return 0;
}

int
uev_trigger (struct uevent * uev, void * trigger_data)
{
	int r = 0;
	struct sysfs_device *sysdev;
	struct vectors * vecs;

	vecs = (struct vectors *)trigger_data;

	if (uev_discard(uev->devpath))
		return 0;

	sysdev = sysfs_device_get(uev->devpath);
	if(!sysdev)
		return 0;

	lock(vecs->lock);

	/*
	 * device map event
	 * Add events are ignored here as the tables
	 * are not fully initialised then.
	 */
	if (!strncmp(sysdev->kernel, "dm-", 3)) {
		if (!strncmp(uev->action, "change", 6)) {
			r = uev_add_map(sysdev, vecs);
			goto out;
		}
		if (!strncmp(uev->action, "remove", 6)) {
			r = uev_remove_map(sysdev, vecs);
			goto out;
		}
		if (!strncmp(uev->action, "umount", 6)) {
			r = uev_umount_map(sysdev, vecs);
			goto out;
		}
		goto out;
	}

	/*
	 * path add/remove event
	 */
	if (filter_devnode(conf->blist_devnode, conf->elist_devnode,
			   sysdev->kernel) > 0)
		goto out;

	if (!strncmp(uev->action, "add", 3)) {
		r = uev_add_path(sysdev, vecs);
		goto out;
	}
	if (!strncmp(uev->action, "remove", 6)) {
		r = uev_remove_path(sysdev, vecs);
		goto out;
	}

out:
	unlock(vecs->lock);
	return r;
}

static void *
ueventloop (void * ap)
{
	block_signal(SIGUSR1, NULL);
	block_signal(SIGHUP, NULL);

	if (uevent_listen(&uev_trigger, ap))
		fprintf(stderr, "error starting uevent listener");

	return NULL;
}

static void *
uxlsnrloop (void * ap)
{
	block_signal(SIGUSR1, NULL);
	block_signal(SIGHUP, NULL);

	if (cli_init())
		return NULL;

	set_handler_callback(LIST+PATHS, cli_list_paths);
	set_handler_callback(LIST+PATHS+FMT, cli_list_paths_fmt);
	set_handler_callback(LIST+MAPS, cli_list_maps);
	set_handler_callback(LIST+STATUS, cli_list_status);
	set_handler_callback(LIST+MAPS+STATUS, cli_list_maps_status);
	set_handler_callback(LIST+MAPS+STATS, cli_list_maps_stats);
	set_handler_callback(LIST+MAPS+FMT, cli_list_maps_fmt);
	set_handler_callback(LIST+MAPS+TOPOLOGY, cli_list_maps_topology);
	set_handler_callback(LIST+TOPOLOGY, cli_list_maps_topology);
	set_handler_callback(LIST+MAP+TOPOLOGY, cli_list_map_topology);
	set_handler_callback(LIST+CONFIG, cli_list_config);
	set_handler_callback(LIST+BLACKLIST, cli_list_blacklist);
	set_handler_callback(LIST+DEVICES, cli_list_devices);
	set_handler_callback(LIST+WILDCARDS, cli_list_wildcards);
	set_handler_callback(ADD+PATH, cli_add_path);
	set_handler_callback(DEL+PATH, cli_del_path);
	set_handler_callback(ADD+MAP, cli_add_map);
	set_handler_callback(DEL+MAP, cli_del_map);
	set_handler_callback(SWITCH+MAP+GROUP, cli_switch_group);
	set_handler_callback(RECONFIGURE, cli_reconfigure);
	set_handler_callback(SUSPEND+MAP, cli_suspend);
	set_handler_callback(RESUME+MAP, cli_resume);
	set_handler_callback(RESIZE+MAP, cli_resize);
	set_handler_callback(REINSTATE+PATH, cli_reinstate);
	set_handler_callback(FAIL+PATH, cli_fail);
	set_handler_callback(DISABLEQ+MAP, cli_disable_queueing);
	set_handler_callback(RESTOREQ+MAP, cli_restore_queueing);
	set_handler_callback(DISABLEQ+MAPS, cli_disable_all_queueing);
	set_handler_callback(RESTOREQ+MAPS, cli_restore_all_queueing);
	set_handler_callback(QUIT, cli_quit);

	umask(077);
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

	pthread_mutex_lock(&exit_mutex);
	pthread_cond_signal(&exit_cond);
	pthread_mutex_unlock(&exit_mutex);

	return status;
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
reinstate_path (struct path * pp, int add_active)
{
	if (!pp->mpp)
		return;

	if (dm_reinstate_path(pp->mpp->alias, pp->dev_t))
		condlog(0, "%s: reinstate failed", pp->dev_t);
	else {
		condlog(2, "%s: reinstated", pp->dev_t);
		if (add_active)
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
	unsigned int i;

	if (!vecs->mpvec)
		return;

	vector_foreach_slot (vecs->mpvec, mpp, i) {
		if (mpp && mpp->alias && !dm_map_present(mpp->alias)) {
			condlog(2, "%s: remove dead map", mpp->alias);
			remove_map_and_stop_waiter(mpp, vecs, 1);
			i--;
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
	unsigned int i;

	vector_foreach_slot (mpvec, mpp, i) {
		if (mpp->retry_tick) {
			mpp->stat_total_queueing_time++;
			condlog(4, "%s: Retrying.. No active path", mpp->alias);
			if(--mpp->retry_tick == 0) {
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

	if (refresh_all) {
		vector_foreach_slot (pp->mpp->pg, pgp, i) {
			vector_foreach_slot (pgp->paths, pp1, j) {
				oldpriority = pp1->priority;
				pathinfo(pp1, conf->hwtable, DI_PRIO);
				if (pp1->priority != oldpriority)
					changed = 1;
			}
		}
		return changed;
	}
	oldpriority = pp->priority;
	pathinfo(pp, conf->hwtable, DI_PRIO);

	if (pp->priority == oldpriority)
		return 0;
	return 1;
}

int update_path_groups(struct multipath *mpp, struct vectors *vecs, int refresh)
{
	int i;
	struct path * pp;
	char params[PARAMS_SIZE];

	update_mpp_paths(mpp, vecs->pathvec);
	if (refresh) {
		vector_foreach_slot (mpp->paths, pp, i)
			pathinfo(pp, conf->hwtable, DI_PRIO);
	}
	params[0] = '\0';
	if (setup_map(mpp, params, PARAMS_SIZE))
		return 1;

	mpp->action = ACT_RELOAD;
	if (domap(mpp, params) <= 0) {
		condlog(0, "%s: failed to update map : %s", mpp->alias,
			strerror(errno));
		return 1;
	}
	dm_lib_release();
	setup_multipath(vecs, mpp);
	sync_map_state(mpp);

	return 0;
}

void
check_path (struct vectors * vecs, struct path * pp)
{
	int newstate;
	int new_path_up = 0;

	if (!pp->mpp)
		return;

	if (pp->tick && --pp->tick)
		return; /* don't check this path yet */

	/*
	 * provision a next check soonest,
	 * in case we exit abnormaly from here
	 */
	pp->tick = conf->checkint;

	newstate = get_state(pp, 1);

	if (newstate == PATH_WILD || newstate == PATH_UNCHECKED) {
		condlog(2, "%s: unusable path", pp->dev);
		pathinfo(pp, conf->hwtable, 0);
		return;
	}
	/*
	 * Async IO in flight. Keep the previous path state
	 * and reschedule as soon as possible
	 */
	if (newstate == PATH_PENDING) {
		pp->tick = 1;
		return;
	}
	if (newstate != pp->state) {
		int oldstate = pp->state;
		pp->state = newstate;
		LOG_MSG(1, checker_message(&pp->checker));

		/*
		 * upon state change, reset the checkint
		 * to the shortest delay
		 */
		pp->checkint = conf->checkint;

		if (newstate == PATH_DOWN || newstate == PATH_SHAKY ||
		    update_multipath_strings(pp->mpp, vecs->pathvec)) {
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
			return;
		}

		/*
		 * reinstate this path
		 */
		if (oldstate != PATH_UP &&
		    oldstate != PATH_GHOST)
			reinstate_path(pp, 1);
		else
			reinstate_path(pp, 0);

		new_path_up = 1;

		/*
		 * if at least one path is up in a group, and
		 * the group is disabled, re-enable it
		 */
		if (newstate == PATH_UP)
			enable_group(pp);
	}
	else if (newstate == PATH_UP || newstate == PATH_GHOST) {
		if (pp->dmstate == PSTATE_FAILED ||
		    pp->dmstate == PSTATE_UNDEF) {
			/* Clear IO errors */
			reinstate_path(pp, 0);
		} else {
			LOG_MSG(4, checker_message(&pp->checker));
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
	}
	else if (newstate == PATH_DOWN)
		LOG_MSG(2, checker_message(&pp->checker));

	pp->state = newstate;

	/*
	 * path prio refreshing
	 */
	condlog(4, "path prio refresh");

	if (update_prio(pp, new_path_up) &&
	    pp->mpp->pgpolicyfn == (pgpolicyfn *)group_by_prio)
		update_path_groups(pp->mpp, vecs, !new_path_up);
	else if (need_switch_pathgroup(pp->mpp, 0)) {
		if (pp->mpp->pgfailback > 0 &&
		    (new_path_up || pp->mpp->failback_tick <= 0))
			pp->mpp->failback_tick =
				pp->mpp->pgfailback + 1;
		else if (pp->mpp->pgfailback == -FAILBACK_IMMEDIATE)
			switch_pathgroup(pp->mpp);
	}
}

static void *
checkerloop (void *ap)
{
	struct vectors *vecs;
	struct path *pp;
	int count = 0;
	unsigned int i;
	sigset_t old;

	mlockall(MCL_CURRENT | MCL_FUTURE);
	vecs = (struct vectors *)ap;
	condlog(2, "path checkers start up");

	/*
	 * init the path check interval
	 */
	vector_foreach_slot (vecs->pathvec, pp, i) {
		pp->checkint = conf->checkint;
	}

	while (1) {
		block_signal(SIGHUP, &old);
		pthread_cleanup_push(cleanup_lock, &vecs->lock);
		lock(vecs->lock);
		condlog(4, "tick");

		if (vecs->pathvec) {
			vector_foreach_slot (vecs->pathvec, pp, i) {
				check_path(vecs, pp);
			}
		}
		if (vecs->mpvec) {
			defered_failback_tick(vecs->mpvec);
			retry_count_tick(vecs->mpvec);
		}
		if (count)
			count--;
		else {
			condlog(4, "map garbage collection");
			mpvec_garbage_collector(vecs);
			count = MAPGCINT;
		}

		lock_cleanup_pop(vecs->lock);
		pthread_sigmask(SIG_SETMASK, &old, NULL);
		sleep(1);
	}
	return NULL;
}

int
configure (struct vectors * vecs, int start_waiters)
{
	struct multipath * mpp;
	struct path * pp;
	vector mpvec;
	int i;

	if (!vecs->pathvec && !(vecs->pathvec = vector_alloc()))
		return 1;

	if (!vecs->mpvec && !(vecs->mpvec = vector_alloc()))
		return 1;

	if (!(mpvec = vector_alloc()))
		return 1;

	/*
	 * probe for current path (from sysfs) and map (from dm) sets
	 */
	path_discovery(vecs->pathvec, conf, DI_ALL);

	vector_foreach_slot (vecs->pathvec, pp, i){
		if (filter_path(conf, pp) > 0){
			vector_del_slot(vecs->pathvec, i);
			free_path(pp);
			i--;
		}
		else
			pp->checkint = conf->checkint;
	}
	if (map_discovery(vecs))
		return 1;

	/*
	 * create new set of maps & push changed ones into dm
	 */
	if (coalesce_paths(vecs, mpvec, NULL, 1))
		return 1;

	/*
	 * may need to remove some maps which are no longer relevant
	 * e.g., due to blacklist changes in conf file
	 */
	if (coalesce_maps(vecs, mpvec))
		return 1;

	dm_lib_release();

	sync_maps_state(mpvec);

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
		if (setup_multipath(vecs, mpp))
			return 1;
		if (start_waiters)
			if (start_waiter_thread(mpp, vecs))
				return 1;
	}
	return 0;
}

int
reconfigure (struct vectors * vecs)
{
	struct config * old = conf;

	/*
	 * free old map and path vectors ... they use old conf state
	 */
	if (VECTOR_SIZE(vecs->mpvec))
		remove_maps_and_stop_waiters(vecs);

	if (VECTOR_SIZE(vecs->pathvec))
		free_pathvec(vecs->pathvec, FREE_PATHS);

	vecs->pathvec = NULL;
	conf = NULL;

	if (load_config(DEFAULT_CONFIGFILE))
		return 1;

	conf->verbosity = old->verbosity;

	if (!conf->checkint) {
		conf->checkint = DEFAULT_CHECKINT;
		conf->max_checkint = MAX_CHECKINT(conf->checkint);
	}
	conf->daemon = 1;
	configure(vecs, 1);
	free_config(old);
	return 0;
}

static struct vectors *
init_vecs (void)
{
	struct vectors * vecs;

	vecs = (struct vectors *)MALLOC(sizeof(struct vectors));

	if (!vecs)
		return NULL;

	vecs->lock.mutex =
		(pthread_mutex_t *)MALLOC(sizeof(pthread_mutex_t));

	if (!vecs->lock.mutex)
		goto out;

	pthread_mutex_init(vecs->lock.mutex, NULL);
	vecs->lock.depth = 0;

	return vecs;

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
	condlog(2, "reconfigure (SIGHUP)");

	lock(gvecs->lock);
	reconfigure(gvecs);
	unlock(gvecs->lock);

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
sigusr1 (int sig)
{
	condlog(3, "SIGUSR1 received");
}

static void
signal_init(void)
{
	signal_set(SIGHUP, sighup);
	signal_set(SIGUSR1, sigusr1);
	signal_set(SIGINT, sigend);
	signal_set(SIGTERM, sigend);
	signal(SIGPIPE, SIG_IGN);
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
	pthread_attr_t log_attr, misc_attr;
	struct vectors * vecs;
	struct multipath * mpp;
	int i;

	mlockall(MCL_CURRENT | MCL_FUTURE);

	setup_thread_attr(&misc_attr, 64 * 1024, 1);
	setup_thread_attr(&waiter_attr, 32 * 1024, 1);

	if (logsink) {
		setup_thread_attr(&log_attr, 64 * 1024, 0);
		log_thread_start(&log_attr);
		pthread_attr_destroy(&log_attr);
	}

	condlog(2, "--------start up--------");
	condlog(2, "read " DEFAULT_CONFIGFILE);

	if (load_config(DEFAULT_CONFIGFILE))
		exit(1);

	if (init_checkers()) {
		condlog(0, "failed to initialize checkers");
		exit(1);
	}
	if (init_prio()) {
		condlog(0, "failed to initialize prioritizers");
		exit(1);
	}

	setlogmask(LOG_UPTO(conf->verbosity + 3));

	/*
	 * fill the voids left in the config file
	 */
	if (!conf->checkint) {
		conf->checkint = DEFAULT_CHECKINT;
		conf->max_checkint = MAX_CHECKINT(conf->checkint);
	}

	if (conf->max_fds) {
		struct rlimit fd_limit;

		fd_limit.rlim_cur = conf->max_fds;
		fd_limit.rlim_max = conf->max_fds;
		if (setrlimit(RLIMIT_NOFILE, &fd_limit) < 0)
			condlog(0, "can't set open fds limit to %d : %s\n",
				conf->max_fds, strerror(errno));
	}

	if (pidfile_create(DEFAULT_PIDFILE, getpid())) {
		if (logsink)
			log_thread_stop();

		exit(1);
	}
	signal_init();
	setscheduler();
	set_oom_adj(-16);
	vecs = gvecs = init_vecs();

	if (!vecs)
		exit(1);

	if (sysfs_init(conf->sysfs_dir, FILE_NAME_SIZE)) {
		condlog(0, "can not find sysfs mount point");
		exit(1);
	}
	conf->daemon = 1;
	udev_set_sync_support(0);
	/*
	 * fetch and configure both paths and multipaths
	 */
	if (configure(vecs, 1)) {
		condlog(0, "failure during configuration");
		exit(1);
	}
	/*
	 * start threads
	 */
	pthread_create(&check_thr, &misc_attr, checkerloop, vecs);
	pthread_create(&uevent_thr, &misc_attr, ueventloop, vecs);
	pthread_create(&uxlsnr_thr, &misc_attr, uxlsnrloop, vecs);
	pthread_attr_destroy(&misc_attr);

	pthread_mutex_lock(&exit_mutex);
	pthread_cond_wait(&exit_cond, &exit_mutex);

	/*
	 * exit path
	 */
	block_signal(SIGHUP, NULL);
	lock(vecs->lock);
	if (conf->queue_without_daemon == QUE_NO_DAEMON_OFF)
		vector_foreach_slot(vecs->mpvec, mpp, i)
			dm_queue_if_no_path(mpp->alias, 0);
	remove_maps_and_stop_waiters(vecs);
	free_pathvec(vecs->pathvec, FREE_PATHS);

	pthread_cancel(check_thr);
	pthread_cancel(uevent_thr);
	pthread_cancel(uxlsnr_thr);

	sysfs_cleanup();

	free_keys(keys);
	keys = NULL;
	free_handlers(handlers);
	handlers = NULL;
	free_polls();

	unlock(vecs->lock);
	/* Now all the waitevent threads will start rushing in. */
	while (vecs->lock.depth > 0) {
		sleep (1); /* This is weak. */
		condlog(3,"Have %d wait event checkers threads to de-alloc, waiting..\n", vecs->lock.depth);
	}
	pthread_mutex_destroy(vecs->lock.mutex);
	FREE(vecs->lock.mutex);
	vecs->lock.depth = 0;
	vecs->lock.mutex = NULL;
	FREE(vecs);
	vecs = NULL;

	condlog(2, "--------shut down-------");

	if (logsink)
		log_thread_stop();

	dm_lib_release();
	dm_lib_exit();

	cleanup_prio();
	cleanup_checkers();
	/*
	 * Freeing config must be done after condlog() and dm_lib_exit(),
	 * because logging functions like dlog() and dm_write_log()
	 * reference the config.
	 */
	free_config(conf);
	conf = NULL;

#ifdef _DEBUG_
	dbg_free_final(NULL);
#endif

	exit(0);
}

static int
daemonize(void)
{
	int pid;
	int in_fd, out_fd;

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

	in_fd = open("/dev/null", O_RDONLY);
	if (in_fd < 0){
		fprintf(stderr, "cannot open /dev/null for input : %s\n",
			strerror(errno));
		_exit(0);
	}
	out_fd = open("/dev/console", O_WRONLY);
	if (out_fd < 0){
		fprintf(stderr, "cannot open /dev/console for output : %s\n",
			strerror(errno));
		_exit(0);
	}

	close(STDIN_FILENO);
	dup(in_fd);
	close(STDOUT_FILENO);
	dup(out_fd);
	close(STDERR_FILENO);
	dup(out_fd);

	close(in_fd);
	close(out_fd);
	if (chdir("/") < 0)
		fprintf(stderr, "cannot chdir to '/', continuing\n");

	return 0;
}

int
main (int argc, char *argv[])
{
	extern char *optarg;
	extern int optind;
	int arg;
	int err;

	logsink = 1;
	dm_init();

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
	if (optind < argc) {
		char cmd[CMDSIZE];
		char * s = cmd;
		char * c = s;

		while (optind < argc) {
			if (strchr(argv[optind], ' '))
				c += snprintf(c, s + CMDSIZE - c, "\"%s\" ", argv[optind]);
			else
				c += snprintf(c, s + CMDSIZE - c, "%s ", argv[optind]);
			optind++;
		}
		c += snprintf(c, s + CMDSIZE - c, "\n");
		uxclnt(s);
		exit(0);
	}

	if (!logsink)
		err = 0;
	else
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

