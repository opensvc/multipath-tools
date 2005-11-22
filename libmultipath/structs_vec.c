#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "vector.h"
#include "defaults.h"
#include "debug.h"
#include "structs.h"
#include "structs_vec.h"
#include "devmapper.h"
#include "dmparser.h"
#include "config.h"
#include "propsel.h"
#include "discovery.h"
#include "../libcheckers/path_state.h"


/*
 * creates or updates mpp->paths reading mpp->pg
 */
extern int
update_mpp_paths(struct multipath * mpp)
{
	struct pathgroup * pgp;
	struct path * pp;
	int i,j;

	if (!mpp->pg)
		return 0;

	if (!mpp->paths &&
	    !(mpp->paths = vector_alloc()))
		return 1;

	vector_foreach_slot (mpp->pg, pgp, i) {
		vector_foreach_slot (pgp->paths, pp, j) {
			if (!find_path_by_devt(mpp->paths, pp->dev_t) &&
			    store_path(mpp->paths, pp))
				return 1;
		}
	}
	return 0;
}

extern int
adopt_paths (vector pathvec, struct multipath * mpp)
{
	int i;
	struct path * pp;

	if (!mpp)
		return 0;

	if (update_mpp_paths(mpp))
		return 1;

	vector_foreach_slot (pathvec, pp, i) {
		if (!strncmp(mpp->wwid, pp->wwid, WWID_SIZE)) {
			condlog(3, "%s ownership set for %s", pp->dev_t, mpp->alias);
			pp->mpp = mpp;
			
			if (!mpp->paths && !(mpp->paths = vector_alloc()))
				return 1;

			if (!find_path_by_dev(mpp->paths, pp->dev) &&
			    store_path(mpp->paths, pp))
					return 1;
		}
	}
	return 0;
}

extern void
orphan_path (struct path * pp)
{
	pp->mpp = NULL;
	pp->checkfn = NULL;
	pp->dmstate = PSTATE_UNDEF;
	pp->checker_context = NULL;
	pp->getuid = NULL;
	pp->getprio = NULL;
	pp->getprio_selected = 0;

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
			condlog(4, "%s is orphaned", pp->dev_t);
			orphan_path(pp);
		}
	}
}

static void
set_multipath_wwid (struct multipath * mpp)
{
	if (mpp->wwid)
		return;

	dm_get_uuid(mpp->alias, mpp->wwid);
}

extern void
remove_map (struct multipath * mpp, struct vectors * vecs,
	    stop_waiter_thread_func *stop_waiter, int purge_vec)
{
	int i;

	/*
	 * stop the DM event waiter thread
	 */
	if (stop_waiter)
		stop_waiter(mpp, vecs);

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
remove_maps (struct vectors * vecs,
	     stop_waiter_thread_func *stop_waiter)
{
	int i;
	struct multipath * mpp;

	vector_foreach_slot (vecs->mpvec, mpp, i) {
		remove_map(mpp, vecs, stop_waiter, 1);
		i--;
	}

	vector_free(vecs->mpvec);
	vecs->mpvec = NULL;
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

extern int
update_multipath_strings (struct multipath *mpp, vector pathvec)
{
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

extern int
setup_multipath (struct vectors * vecs, struct multipath * mpp)
{
retry:
	if (dm_get_info(mpp->alias, &mpp->dmi))
		goto out;

	set_multipath_wwid(mpp);
	mpp->mpe = find_mpe(mpp->wwid);
	condlog(3, "discovered map %s", mpp->alias);

	if (update_multipath_strings(mpp, vecs->pathvec)) {
		char new_alias[WWID_SIZE];

		/*
	 	 * detect an external rename of the multipath device
		 */
		if (dm_get_name(mpp->wwid, DEFAULT_TARGET, new_alias)) {
			condlog(3, "%s multipath mapped device name has "
				"changed from %s to %s", mpp->wwid,
				mpp->alias, new_alias);
			strcpy(mpp->alias, new_alias);
#if DAEMON
			if (mpp->waiter) 
				strncpy(((struct event_thread *)mpp->waiter)->mapname,
					new_alias, WWID_SIZE);
#endif
			goto retry;
		}
		goto out;
	}

	//adopt_paths(vecs->pathvec, mpp);
	mpp->hwe = extract_hwe_from_path(mpp);
	select_rr_weight(mpp);
	select_pgfailback(mpp);
	set_no_path_retry(mpp);

	return 0;
out:
	condlog(0, "%s: failed to setup multipath", mpp->alias);
	remove_map(mpp, vecs, NULL, 1);
	return 1;
}

extern struct multipath *
add_map_without_path (struct vectors * vecs,
		      int minor, char * alias,
		      start_waiter_thread_func *start_waiter)
{
	struct multipath * mpp = alloc_multipath();

	if (!mpp)
		return NULL;

	mpp->alias = alias;

	if (setup_multipath(vecs, mpp))
		return NULL; /* mpp freed in setup_multipath */

	if (adopt_paths(vecs->pathvec, mpp))
		goto out;
	
	if (!vector_alloc_slot(vecs->mpvec))
		goto out;

	vector_set_slot(vecs->mpvec, mpp);

	if (start_waiter(mpp, vecs))
		goto out;

	return mpp;
out:
	remove_map(mpp, vecs, NULL, 1);
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
	select_alias(mpp);
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
	remove_map(mpp, vecs, NULL, add_vec);
	return NULL;
}

extern int
verify_paths(struct multipath * mpp, struct vectors * vecs, vector rpvec)
{
	struct path * pp;
	int count = 0;
	int i, j;

	vector_foreach_slot (mpp->paths, pp, i) {
		/*
		 * see if path is in sysfs
		 */
		if (!pp->dev || sysfs_get_dev(sysfs_path,
				  pp->dev, pp->dev_t, BLK_DEV_SIZE)) {
			condlog(0, "%s: failed to access path %s", mpp->alias,
				pp->dev ? pp->dev : pp->dev_t);
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
		}
	}
	return count;
}

