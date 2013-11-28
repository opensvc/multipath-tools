#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "checkers.h"
#include "vector.h"
#include "defaults.h"
#include "debug.h"
#include "structs.h"
#include "structs_vec.h"
#include "waiter.h"
#include "devmapper.h"
#include "dmparser.h"
#include "config.h"
#include "propsel.h"
#include "discovery.h"
#include "prio.h"

/*
 * creates or updates mpp->paths reading mpp->pg
 */
extern int
update_mpp_paths(struct multipath * mpp, vector pathvec)
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

extern int
adopt_paths (vector pathvec, struct multipath * mpp, int get_info)
{
	int i;
	struct path * pp;

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
			if (get_info && pathinfo(pp, conf->hwtable,
						 DI_PRIO | DI_CHECKER))
				return 1;
		}
	}
	return 0;
}

extern void
orphan_path (struct path * pp, const char *reason)
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

extern void
orphan_paths (vector pathvec, struct multipath * mpp)
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

#define KEEP_WAITER 0
#define STOP_WAITER 1
#define PURGE_VEC 1

static void
_remove_map (struct multipath * mpp, struct vectors * vecs,
	    int stop_waiter, int purge_vec)
{
	int i;

	condlog(4, "%s: remove multipath map", mpp->alias);

	/*
	 * stop the DM event waiter thread
	 */
	if (stop_waiter)
		stop_waiter_thread(mpp, vecs);

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

extern void
remove_map (struct multipath * mpp, struct vectors * vecs, int purge_vec)
{
	_remove_map(mpp, vecs, KEEP_WAITER, purge_vec);
}

extern void
remove_map_and_stop_waiter (struct multipath * mpp, struct vectors * vecs,
			    int purge_vec)
{
	_remove_map(mpp, vecs, STOP_WAITER, purge_vec);
}

static void
_remove_maps (struct vectors * vecs, int stop_waiter)
{
	int i;
	struct multipath * mpp;

	if (!vecs)
		return;

	vector_foreach_slot (vecs->mpvec, mpp, i) {
		_remove_map(mpp, vecs, stop_waiter, 1);
		i--;
	}

	vector_free(vecs->mpvec);
	vecs->mpvec = NULL;
}

extern void
remove_maps (struct vectors * vecs)
{
	_remove_maps(vecs, KEEP_WAITER);
}

extern void
remove_maps_and_stop_waiters (struct vectors * vecs)
{
	_remove_maps(vecs, STOP_WAITER);
}

static struct hwentry *
extract_hwe_from_path(struct multipath * mpp)
{
	struct path * pp = NULL;
	int pg_num = -1, p_num = -1, i;
	struct pathgroup * pgp = NULL;

	condlog(3, "%s: searching paths for valid hwe", mpp->alias);

	if (mpp && mpp->pg) {
		vector_foreach_slot(mpp->pg, pgp, i) {
			if (pgp->status == PGSTATE_ACTIVE ||
			    pgp->status == PGSTATE_ENABLED) {
				pg_num = i;
				break;
			}
		}
		if (pg_num >= 0)
			pgp = VECTOR_SLOT(mpp->pg, pg_num);
	}

	if (pgp && pgp->paths) {
		vector_foreach_slot(pgp->paths, pp, i) {
			if (pp->dmstate == PSTATE_FAILED)
				continue;
			if (strlen(pp->vendor_id) > 0 &&
			    strlen(pp->product_id) > 0 &&
			    strlen(pp->rev) > 0) {
				p_num = i;
				break;
			}
		}
		if (p_num >= 0)
			pp = VECTOR_SLOT(pgp->paths, i);
	}

	if (pp) {
		condlog(3, "%s: vendor = %s", pp->dev, pp->vendor_id);
		condlog(3, "%s: product = %s", pp->dev, pp->product_id);
		condlog(3, "%s: rev = %s", pp->dev, pp->rev);
		if (!pp->hwe) {
			condlog(3, "searching hwtable");
			pp->hwe = find_hwe(conf->hwtable, pp->vendor_id,
					   pp->product_id, pp->rev);
		}
	}

	return pp?pp->hwe:NULL;
}

static int
update_multipath_table (struct multipath *mpp, vector pathvec)
{
	char params[PARAMS_SIZE] = {0};

	if (!mpp)
		return 1;

	if (dm_get_map(mpp->alias, &mpp->size, params)) {
		condlog(3, "%s: cannot get map", mpp->alias);
		return 1;
	}

	if (disassemble_map(pathvec, params, mpp)) {
		condlog(3, "%s: cannot disassemble map", mpp->alias);
		return 1;
	}

	return 0;
}

static int
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

extern int
update_multipath_strings (struct multipath *mpp, vector pathvec)
{
	if (!mpp)
		return 1;

	condlog(4, "%s: %s", mpp->alias, __FUNCTION__);

	free_multipath_attributes(mpp);
	free_pgvec(mpp->pg, KEEP_PATHS);
	mpp->pg = NULL;

	if (update_multipath_table(mpp, pathvec))
		return 1;

	if (update_multipath_status(mpp))
		return 1;

	return 0;
}

extern void
set_no_path_retry(struct multipath *mpp)
{
	mpp->retry_tick = 0;
	mpp->nr_active = pathcount(mpp, PATH_UP) + pathcount(mpp, PATH_GHOST);
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

extern int
__setup_multipath (struct vectors * vecs, struct multipath * mpp, int reset)
{
	if (dm_get_info(mpp->alias, &mpp->dmi)) {
		/* Error accessing table */
		condlog(3, "%s: cannot access table", mpp->alias);
		goto out;
	}

	if (!dm_map_present(mpp->alias)) {
		/* Table has been removed */
		condlog(3, "%s: table does not exist", mpp->alias);
		goto out;
	}

	if (update_multipath_strings(mpp, vecs->pathvec)) {
		condlog(0, "%s: failed to setup multipath", mpp->alias);
		goto out;
	}

	set_multipath_wwid(mpp);
	mpp->mpe = find_mpe(mpp->wwid);
	condlog(3, "%s: discover", mpp->alias);

	if (!mpp->hwe)
		mpp->hwe = extract_hwe_from_path(mpp);
	if (!mpp->hwe) {
		condlog(3, "%s: no hardware entry found, using defaults",
			mpp->alias);
	}
	if (reset) {
		select_rr_weight(mpp);
		select_pgfailback(mpp);
		set_no_path_retry(mpp);
		select_flush_on_last_del(mpp);
	}

	return 0;
out:
	remove_map(mpp, vecs, PURGE_VEC);
	return 1;
}

extern struct multipath *
add_map_without_path (struct vectors * vecs, char * alias)
{
	struct multipath * mpp = alloc_multipath();

	if (!mpp || !alias)
		return NULL;

	mpp->alias = STRDUP(alias);

	if (setup_multipath(vecs, mpp))
		return NULL; /* mpp freed in setup_multipath */

	if (adopt_paths(vecs->pathvec, mpp, 1))
		goto out;

	if (!vector_alloc_slot(vecs->mpvec))
		goto out;

	vector_set_slot(vecs->mpvec, mpp);

	if (start_waiter_thread(mpp, vecs))
		goto out;

	return mpp;
out:
	remove_map(mpp, vecs, PURGE_VEC);
	return NULL;
}

extern struct multipath *
add_map_with_path (struct vectors * vecs,
		   struct path * pp, int add_vec)
{
	struct multipath * mpp;

	if (!(mpp = alloc_multipath()))
		return NULL;

	mpp->mpe = find_mpe(pp->wwid);
	mpp->hwe = pp->hwe;

	strcpy(mpp->wwid, pp->wwid);
	if (select_alias(mpp))
		goto out;
	mpp->size = pp->size;

	if (adopt_paths(vecs->pathvec, mpp, 1))
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

extern int
verify_paths(struct multipath * mpp, struct vectors * vecs, vector rpvec)
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
		if (sysfs_get_dev(pp->udev, pp->dev_t, BLK_DEV_SIZE) <= 0) {
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

			if (rpvec)
				store_path(rpvec, pp);
			else {
				if ((j = find_slot(vecs->pathvec,
						   (void *)pp)) != -1)
					vector_del_slot(vecs->pathvec, j);
				free_path(pp);
			}
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

	free_pgvec(mpp->pg, KEEP_PATHS);
	mpp->pg = NULL;

	if (__setup_multipath(vecs, mpp, reset))
		return 1; /* mpp freed in setup_multipath */

	adopt_paths(vecs->pathvec, mpp, 0);
	/*
	 * compare checkers states with DM states
	 */
	vector_foreach_slot (mpp->pg, pgp, i) {
		vector_foreach_slot (pgp->paths, pp, j) {
			if (pp->dmstate != PSTATE_FAILED)
				continue;

			if (pp->state != PATH_DOWN) {
				int oldstate = pp->state;
				condlog(2, "%s: mark as failed", pp->dev_t);
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
	if (--mpp->nr_active == 0 && mpp->no_path_retry > 0) {
		/*
		 * Enter retry mode.
		 * meaning of +1: retry_tick may be decremented in
		 *                checkerloop before starting retry.
		 */
		mpp->stat_queueing_timeouts++;
		mpp->retry_tick = mpp->no_path_retry * conf->checkint + 1;
		condlog(1, "%s: Entering recovery mode: max_retries=%d",
			mpp->alias, mpp->no_path_retry);
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

