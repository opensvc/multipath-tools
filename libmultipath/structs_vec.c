#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "checkers.h"
#include "vector.h"
#include "defaults.h"
#include "debug.h"
#include "config.h"
#include "structs.h"
#include "structs_vec.h"
#include "sysfs.h"
#include "waiter.h"
#include "devmapper.h"
#include "dmparser.h"
#include "propsel.h"
#include "discovery.h"
#include "prio.h"
#include "configure.h"
#include "libdevmapper.h"
#include "io_err_stat.h"

/*
 * creates or updates mpp->paths reading mpp->pg
 */
int update_mpp_paths(struct multipath *mpp, vector pathvec)
{
	struct pathgroup * pgp;
	struct path * pp;
	int i,j;

	if (!mpp || !mpp->pg)
		return 0;

	if (!mpp->paths &&
	    !(mpp->paths = vector_alloc()))
		return 1;

	vector_foreach_slot (mpp->pg, pgp, i) {
		vector_foreach_slot (pgp->paths, pp, j) {
			if (!find_path_by_devt(mpp->paths, pp->dev_t) &&
			    (find_path_by_devt(pathvec, pp->dev_t)) &&
			    store_path(mpp->paths, pp))
				return 1;
		}
	}
	return 0;
}

int adopt_paths(vector pathvec, struct multipath *mpp)
{
	int i, ret;
	struct path * pp;
	struct config *conf;

	if (!mpp)
		return 0;

	if (update_mpp_paths(mpp, pathvec))
		return 1;

	vector_foreach_slot (pathvec, pp, i) {
		if (!strncmp(mpp->wwid, pp->wwid, WWID_SIZE)) {
			condlog(3, "%s: ownership set to %s",
				pp->dev, mpp->alias);
			pp->mpp = mpp;

			if (!mpp->paths && !(mpp->paths = vector_alloc()))
				return 1;

			if (!find_path_by_dev(mpp->paths, pp->dev) &&
			    store_path(mpp->paths, pp))
					return 1;
			conf = get_multipath_config();
			ret = pathinfo(pp, conf,
				       DI_PRIO | DI_CHECKER);
			put_multipath_config(conf);
			if (ret)
				return 1;
		}
	}
	return 0;
}

void orphan_path(struct path *pp, const char *reason)
{
	condlog(3, "%s: orphan path, %s", pp->dev, reason);
	pp->mpp = NULL;
	pp->dmstate = PSTATE_UNDEF;
	pp->uid_attribute = NULL;
	pp->getuid = NULL;
	prio_put(&pp->prio);
	checker_put(&pp->checker);
	if (pp->fd >= 0)
		close(pp->fd);
	pp->fd = -1;
}

void orphan_paths(vector pathvec, struct multipath *mpp)
{
	int i;
	struct path * pp;

	vector_foreach_slot (pathvec, pp, i) {
		if (pp->mpp == mpp) {
			orphan_path(pp, "map flushed");
		}
	}
}

static void
set_multipath_wwid (struct multipath * mpp)
{
	if (strlen(mpp->wwid))
		return;

	dm_get_uuid(mpp->alias, mpp->wwid);
}

#define PURGE_VEC 1

void
remove_map(struct multipath * mpp, struct vectors * vecs, int purge_vec)
{
	int i;

	condlog(4, "%s: remove multipath map", mpp->alias);

	/*
	 * clear references to this map
	 */
	orphan_paths(vecs->pathvec, mpp);

	if (purge_vec &&
	    (i = find_slot(vecs->mpvec, (void *)mpp)) != -1)
		vector_del_slot(vecs->mpvec, i);

	/*
	 * final free
	 */
	free_multipath(mpp, KEEP_PATHS);
}

void
remove_maps(struct vectors * vecs)
{
	int i;
	struct multipath * mpp;

	if (!vecs)
		return;

	vector_foreach_slot (vecs->mpvec, mpp, i) {
		remove_map(mpp, vecs, 1);
		i--;
	}

	vector_free(vecs->mpvec);
	vecs->mpvec = NULL;
}

void
extract_hwe_from_path(struct multipath * mpp)
{
	struct path * pp = NULL;
	int i;

	if (mpp->hwe || !mpp->paths)
		return;

	condlog(3, "%s: searching paths for valid hwe", mpp->alias);
	/* doing this in two passes seems like paranoia to me */
	vector_foreach_slot(mpp->paths, pp, i) {
		if (pp->state != PATH_UP)
			continue;
		if (pp->hwe) {
			mpp->hwe = pp->hwe;
			return;
		}
	}
	vector_foreach_slot(mpp->paths, pp, i) {
		if (pp->state == PATH_UP)
			continue;
		if (pp->hwe) {
			mpp->hwe = pp->hwe;
			return;
		}
	}
}

int
update_multipath_table (struct multipath *mpp, vector pathvec, int is_daemon)
{
	char params[PARAMS_SIZE] = {0};

	if (!mpp)
		return 1;

	if (dm_get_map(mpp->alias, &mpp->size, params)) {
		condlog(3, "%s: cannot get map", mpp->alias);
		return 1;
	}

	if (disassemble_map(pathvec, params, mpp, is_daemon)) {
		condlog(3, "%s: cannot disassemble map", mpp->alias);
		return 1;
	}

	return 0;
}

int
update_multipath_status (struct multipath *mpp)
{
	char status[PARAMS_SIZE] = {0};

	if (!mpp)
		return 1;

	if (dm_get_status(mpp->alias, status)) {
		condlog(3, "%s: cannot get status", mpp->alias);
		return 1;
	}

	if (disassemble_status(status, mpp)) {
		condlog(3, "%s: cannot disassemble status", mpp->alias);
		return 1;
	}

	return 0;
}

void sync_paths(struct multipath *mpp, vector pathvec)
{
	struct path *pp;
	struct pathgroup  *pgp;
	int found, i, j;

	vector_foreach_slot (mpp->paths, pp, i) {
		found = 0;
		vector_foreach_slot(mpp->pg, pgp, j) {
			if (find_slot(pgp->paths, (void *)pp) != -1) {
				found = 1;
				break;
			}
		}
		if (!found) {
			condlog(3, "%s dropped path %s", mpp->alias, pp->dev);
			vector_del_slot(mpp->paths, i--);
			orphan_path(pp, "path removed externally");
		}
	}
	update_mpp_paths(mpp, pathvec);
	vector_foreach_slot (mpp->paths, pp, i)
		pp->mpp = mpp;
}

int
update_multipath_strings(struct multipath *mpp, vector pathvec, int is_daemon)
{
	if (!mpp)
		return 1;

	update_mpp_paths(mpp, pathvec);
	condlog(4, "%s: %s", mpp->alias, __FUNCTION__);

	free_multipath_attributes(mpp);
	free_pgvec(mpp->pg, KEEP_PATHS);
	mpp->pg = NULL;

	if (update_multipath_table(mpp, pathvec, is_daemon))
		return 1;
	sync_paths(mpp, pathvec);

	if (update_multipath_status(mpp))
		return 1;

	return 0;
}

void enter_recovery_mode(struct multipath *mpp)
{
	struct config *conf = get_multipath_config();

	/*
	 * Enter retry mode.
	 * meaning of +1: retry_tick may be decremented in checkerloop before
	 * starting retry.
	 */
	mpp->stat_queueing_timeouts++;
	mpp->retry_tick = mpp->no_path_retry * conf->checkint + 1;
	condlog(1, "%s: Entering recovery mode: max_retries=%d",
		mpp->alias, mpp->no_path_retry);
	put_multipath_config(conf);
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
	remove_map(mpp, vecs, PURGE_VEC);
	return 1;
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
			    pp->state == PATH_WILD ||
			    pp->state == PATH_DELAYED)
				continue;
			if (mpp->ghost_delay_tick > 0)
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

int
update_map (struct multipath *mpp, struct vectors *vecs)
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

	extract_hwe_from_path(mpp);
	if (setup_map(mpp, params, PARAMS_SIZE, vecs)) {
		condlog(0, "%s: failed to setup new map in update", mpp->alias);
		retries = -1;
		goto fail;
	}
	if (domap(mpp, params, 1) <= 0 && retries-- > 0) {
		condlog(0, "%s: map_udate sleep", mpp->alias);
		sleep(1);
		goto retry;
	}
	dm_lib_release();

fail:
	if (setup_multipath(vecs, mpp))
		return 1;

	sync_map_state(mpp);

	if (retries < 0)
		condlog(0, "%s: failed reload in new map update", mpp->alias);
	return 0;
}

struct multipath *add_map_without_path (struct vectors *vecs, const char *alias)
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

	if (update_map(mpp, vecs) != 0) /* map removed */
		return NULL;

	if (start_waiter_thread(mpp, vecs))
		goto out;

	return mpp;
out:
	remove_map(mpp, vecs, PURGE_VEC);
	return NULL;
}

static void
find_existing_alias (struct multipath * mpp,
		     struct vectors *vecs)
{
	struct multipath * mp;
	int i;

	vector_foreach_slot (vecs->mpvec, mp, i)
		if (strncmp(mp->wwid, mpp->wwid, WWID_SIZE - 1) == 0) {
			strncpy(mpp->alias_old, mp->alias, WWID_SIZE - 1);
			return;
		}
}

struct multipath *add_map_with_path(struct vectors *vecs, struct path *pp,
				    int add_vec)
{
	struct multipath * mpp;
	struct config *conf = NULL;

	if (!strlen(pp->wwid))
		return NULL;

	if (!(mpp = alloc_multipath()))
		return NULL;

	conf = get_multipath_config();
	mpp->mpe = find_mpe(conf->mptable, pp->wwid);
	mpp->hwe = pp->hwe;
	put_multipath_config(conf);

	strcpy(mpp->wwid, pp->wwid);
	find_existing_alias(mpp, vecs);
	if (select_alias(conf, mpp))
		goto out;
	mpp->size = pp->size;

	if (adopt_paths(vecs->pathvec, mpp))
		goto out;

	if (add_vec) {
		if (!vector_alloc_slot(vecs->mpvec))
			goto out;

		vector_set_slot(vecs->mpvec, mpp);
	}

	return mpp;

out:
	remove_map(mpp, vecs, PURGE_VEC);
	return NULL;
}

int verify_paths(struct multipath *mpp, struct vectors *vecs)
{
	struct path * pp;
	int count = 0;
	int i, j;

	if (!mpp)
		return 0;

	vector_foreach_slot (mpp->paths, pp, i) {
		/*
		 * see if path is in sysfs
		 */
		if (sysfs_attr_get_value(pp->udev, "dev",
					 pp->dev_t, BLK_DEV_SIZE) < 0) {
			if (pp->state != PATH_DOWN) {
				condlog(1, "%s: removing valid path %s in state %d",
					mpp->alias, pp->dev, pp->state);
			} else {
				condlog(3, "%s: failed to access path %s",
					mpp->alias, pp->dev);
			}
			count++;
			vector_del_slot(mpp->paths, i);
			i--;

			if ((j = find_slot(vecs->pathvec,
					   (void *)pp)) != -1)
				vector_del_slot(vecs->pathvec, j);
			free_path(pp);
		} else {
			condlog(4, "%s: verified path %s dev_t %s",
				mpp->alias, pp->dev, pp->dev_t);
		}
	}
	return count;
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
				struct config *conf = get_multipath_config();
				int oldstate = pp->state;
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
				if (pp->tick > conf->checkint)
					pp->tick = conf->checkint;
				put_multipath_config(conf);
			}
		}
	}
	return 0;
}

/*
 * mpp->no_path_retry:
 *   -2 (QUEUE) : queue_if_no_path enabled, never turned off
 *   -1 (FAIL)  : fail_if_no_path
 *    0 (UNDEF) : nothing
 *   >0         : queue_if_no_path enabled, turned off after polling n times
 */
void update_queue_mode_del_path(struct multipath *mpp)
{
	if (--mpp->nr_active == 0) {
		if (mpp->no_path_retry > 0)
			enter_recovery_mode(mpp);
		else if (mpp->no_path_retry != NO_PATH_RETRY_QUEUE)
			mpp->stat_map_failures++;
	}
	condlog(2, "%s: remaining active paths: %d", mpp->alias, mpp->nr_active);
}

void update_queue_mode_add_path(struct multipath *mpp)
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
