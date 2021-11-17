#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libudev.h>

#include "util.h"
#include "checkers.h"
#include "vector.h"
#include "defaults.h"
#include "debug.h"
#include "config.h"
#include "structs.h"
#include "structs_vec.h"
#include "sysfs.h"
#include "devmapper.h"
#include "dmparser.h"
#include "propsel.h"
#include "discovery.h"
#include "prio.h"
#include "configure.h"
#include "libdevmapper.h"
#include "io_err_stat.h"
#include "switchgroup.h"

/*
 * creates or updates mpp->paths reading mpp->pg
 */
int update_mpp_paths(struct multipath *mpp, vector pathvec)
{
	struct pathgroup * pgp;
	struct path * pp;
	int i,j;
	bool store_failure = false;

	if (!mpp || !mpp->pg)
		return 0;

	if (!mpp->paths &&
	    !(mpp->paths = vector_alloc()))
		return 1;

	vector_foreach_slot (mpp->pg, pgp, i) {
		vector_foreach_slot (pgp->paths, pp, j) {
			if (!find_path_by_devt(mpp->paths, pp->dev_t)) {
				struct path *pp1;

				/*
				 * Avoid adding removed paths to the map again
				 * when we reload it. Such paths may exist in
				 * ev_remove_paths() or if it returns failure.
				 */
				pp1 = find_path_by_devt(pathvec, pp->dev_t);
				if (pp1 && pp->initialized != INIT_REMOVED &&
				    store_path(mpp->paths, pp))
					store_failure = true;
			}
		}
	}

	return store_failure;
}

static bool guess_mpp_wwid(struct multipath *mpp)
{
	int i, j;
	struct pathgroup *pgp;
	struct path *pp;

	if (strlen(mpp->wwid) || !mpp->pg)
		return true;

	vector_foreach_slot(mpp->pg, pgp, i) {
		if (!pgp->paths)
			continue;
		vector_foreach_slot(pgp->paths, pp, j) {
			if (pp->initialized == INIT_OK && strlen(pp->wwid)) {
				strlcpy(mpp->wwid, pp->wwid, sizeof(mpp->wwid));
				condlog(2, "%s: guessed WWID %s from path %s",
					mpp->alias, mpp->wwid, pp->dev);
				return true;
			}
		}
	}
	condlog(1, "%s: unable to guess WWID", mpp->alias);
	return false;
}

/*
 * update_pathvec_from_dm() - update pathvec after disassemble_map()
 *
 * disassemble_map() may return block devices that are members in
 * multipath maps but haven't been discovered. Check whether they
 * need to be added to pathvec or discarded.
 *
 * Returns: true if immediate map reload is desirable
 *
 * Side effects:
 * - may delete non-existing paths and empty pathgroups from mpp
 * - may set pp->wwid and / or mpp->wwid
 * - calls pathinfo() on existing paths is pathinfo_flags is not 0
 */
bool update_pathvec_from_dm(vector pathvec, struct multipath *mpp,
	int pathinfo_flags)
{
	int i, j;
	struct pathgroup *pgp;
	struct path *pp;
	struct config *conf;
	bool mpp_has_wwid;
	bool must_reload = false;

	if (!mpp->pg)
		return false;

	/*
	 * This will initialize mpp->wwid with an educated guess,
	 * either from the dm uuid or from a member path with properly
	 * determined WWID.
	 */
	mpp_has_wwid = guess_mpp_wwid(mpp);

	vector_foreach_slot(mpp->pg, pgp, i) {
		if (!pgp->paths)
			goto delete_pg;

		vector_foreach_slot(pgp->paths, pp, j) {

			if (pp->mpp && pp->mpp != mpp) {
				condlog(0, "BUG: %s: found path %s which is already in %s",
					mpp->alias, pp->dev, pp->mpp->alias);

				/*
				 * Either we added this path to the other mpp
				 * explicitly, or we came by here earlier and
				 * decided it belonged there. In both cases,
				 * the path should remain in the other map,
				 * and be deleted here.
				 */
				must_reload = true;
				dm_fail_path(mpp->alias, pp->dev_t);
				vector_del_slot(pgp->paths, j--);
				continue;
			}
			pp->mpp = mpp;

			/*
			 * The way disassemble_map() works: If it encounters a
			 * path device which isn't found in pathvec, it adds an
			 * uninitialized struct path to pgp->paths, with only
			 * pp->dev_t filled in. Thus if pp->udev is set here,
			 * we know that the path is in pathvec already.
			 * However, it's possible that the path in pathvec is
			 * different from the one the kernel still had in its
			 * map.
			 */
			if (pp->udev) {
				if (pathinfo_flags & ~DI_NOIO) {
					conf = get_multipath_config();
					pthread_cleanup_push(put_multipath_config,
							     conf);
					pathinfo(pp, conf, pathinfo_flags|DI_WWID);
					pthread_cleanup_pop(1);
				}
			} else {
				/* If this fails, the device is not in sysfs */
				pp->udev = get_udev_device(pp->dev_t, DEV_DEVT);

				if (!pp->udev) {
					condlog(2, "%s: discarding non-existing path %s",
						mpp->alias, pp->dev_t);
					vector_del_slot(pgp->paths, j--);
					free_path(pp);
					must_reload = true;
					continue;
				} else {
					int rc;

					strlcpy(pp->dev, udev_device_get_sysname(pp->udev),
						sizeof(pp->dev));
					conf = get_multipath_config();
					pthread_cleanup_push(put_multipath_config,
							     conf);
					pp->checkint = conf->checkint;
					rc = pathinfo(pp, conf,
						      DI_SYSFS|DI_WWID|DI_BLACKLIST|DI_NOFALLBACK|pathinfo_flags);
					pthread_cleanup_pop(1);
					if (rc != PATHINFO_OK) {
						condlog(1, "%s: error %d in pathinfo, discarding path",
							pp->dev, rc);
						vector_del_slot(pgp->paths, j--);
						free_path(pp);
						must_reload = true;
						continue;
					}
					condlog(2, "%s: adding new path %s",
						mpp->alias, pp->dev);
					pp->initialized = INIT_PARTIAL;
					pp->partial_retrigger_delay = 180;
					store_path(pathvec, pp);
					pp->tick = 1;
				}
			}

			/* We don't set the map WWID from paths here */
			if (!mpp_has_wwid)
				continue;

			/*
			 * At this point, pp->udev is valid and and pp->wwid
			 * is the best we could get
			 */
			if (*pp->wwid && strcmp(mpp->wwid, pp->wwid)) {
				condlog(0, "%s: path %s WWID %s doesn't match, removing from map",
					mpp->wwid, pp->dev_t, pp->wwid);
				/*
				 * This path exists, but in the wrong map.
				 * We can't reload the map from here.
				 * Make sure it isn't used in this map
				 * any more, and let the checker re-add
				 * it as it sees fit.
				 */
				dm_fail_path(mpp->alias, pp->dev_t);
				vector_del_slot(pgp->paths, j--);
				orphan_path(pp, "WWID mismatch");
				pp->tick = 1;
				must_reload = true;
			} else if (!*pp->wwid) {
				condlog(3, "%s: setting wwid from map: %s",
					pp->dev, mpp->wwid);
				strlcpy(pp->wwid, mpp->wwid,
					sizeof(pp->wwid));
			}
		}
		if (VECTOR_SIZE(pgp->paths) != 0)
			continue;
	delete_pg:
		condlog(2, "%s: removing empty pathgroup %d", mpp->alias, i);
		vector_del_slot(mpp->pg, i--);
		free_pathgroup(pgp, KEEP_PATHS);
		must_reload = true;
	}
	mpp->need_reload = mpp->need_reload || must_reload;
	return must_reload;
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
			if (pp->size != 0 && mpp->size != 0 &&
			    pp->size != mpp->size) {
				condlog(3, "%s: size mismatch for %s, not adding path",
					pp->dev, mpp->alias);
				continue;
			}
			if (pp->initialized == INIT_REMOVED)
				continue;
			if (!mpp->paths && !(mpp->paths = vector_alloc()))
				goto err;

			conf = get_multipath_config();
			pthread_cleanup_push(put_multipath_config, conf);
			ret = pathinfo(pp, conf,
				       DI_PRIO | DI_CHECKER);
			pthread_cleanup_pop(1);
			if (ret) {
				condlog(3, "%s: pathinfo failed for %s",
					__func__, pp->dev);
				continue;
			}

			if (!find_path_by_devt(mpp->paths, pp->dev_t) &&
			    store_path(mpp->paths, pp))
				goto err;

			pp->mpp = mpp;
			condlog(3, "%s: ownership set to %s",
				pp->dev, mpp->alias);
		}
	}
	return 0;
err:
	condlog(1, "error setting ownership of %s to %s", pp->dev, mpp->alias);
	return 1;
}

void orphan_path(struct path *pp, const char *reason)
{
	condlog(3, "%s: orphan path, %s", pp->dev, reason);
	pp->mpp = NULL;
	uninitialize_path(pp);
}

void orphan_paths(vector pathvec, struct multipath *mpp, const char *reason)
{
	int i;
	struct path * pp;

	vector_foreach_slot (pathvec, pp, i) {
		if (pp->mpp == mpp) {
			if (pp->initialized == INIT_REMOVED ||
			    pp->initialized == INIT_PARTIAL) {
				condlog(3, "%s: freeing path in %s state",
					pp->dev,
					pp->initialized == INIT_REMOVED ?
					"removed" : "partial");
				vector_del_slot(pathvec, i--);
				free_path(pp);
			} else
				orphan_path(pp, reason);
		}
	}
}

void set_path_removed(struct path *pp)
{
	struct multipath *mpp = pp->mpp;

	orphan_path(pp, "removed");
	/*
	 * Keep link to mpp. It will be removed when the path
	 * is successfully removed from the map.
	 */
	if (!mpp)
		condlog(0, "%s: internal error: mpp == NULL", pp->dev);
	pp->mpp = mpp;
	pp->initialized = INIT_REMOVED;
}

void
remove_map(struct multipath *mpp, vector pathvec, vector mpvec)
{
	int i;

	/*
	 * clear references to this map
	 */
	orphan_paths(pathvec, mpp, "map removed internally");

	if (mpvec &&
	    (i = find_slot(mpvec, (void *)mpp)) != -1)
		vector_del_slot(mpvec, i);

	/*
	 * final free
	 */
	free_multipath(mpp, KEEP_PATHS);
}

void
remove_map_by_alias(const char *alias, struct vectors * vecs)
{
	struct multipath * mpp = find_mp_by_alias(vecs->mpvec, alias);
	if (mpp) {
		condlog(2, "%s: removing map by alias", alias);
		remove_map(mpp, vecs->pathvec, vecs->mpvec);
	}
}

void
remove_maps(struct vectors * vecs)
{
	int i;
	struct multipath * mpp;

	if (!vecs)
		return;

	vector_foreach_slot (vecs->mpvec, mpp, i) {
		remove_map(mpp, vecs->pathvec, vecs->mpvec);
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

	condlog(4, "%s: searching paths for valid hwe", mpp->alias);
	/* doing this in two passes seems like paranoia to me */
	vector_foreach_slot(mpp->paths, pp, i) {
		if (pp->state == PATH_UP && pp->initialized != INIT_PARTIAL &&
		    pp->initialized != INIT_REMOVED && pp->hwe)
			goto done;
	}
	vector_foreach_slot(mpp->paths, pp, i) {
		if ((pp->state != PATH_UP || pp->initialized == INIT_PARTIAL) &&
		    pp->initialized != INIT_REMOVED && pp->hwe)
			goto done;
	}
done:
	if (i < VECTOR_SIZE(mpp->paths))
		(void)set_mpp_hwe(mpp, pp);

	if (mpp->hwe)
		condlog(3, "%s: got hwe from path %s", mpp->alias, pp->dev);
	else
		condlog(2, "%s: no hwe found", mpp->alias);
}

int
update_multipath_table (struct multipath *mpp, vector pathvec, int flags)
{
	int r = DMP_ERR;
	char *params = NULL;

	if (!mpp)
		return r;

	r = dm_get_map(mpp->alias, &mpp->size, &params);
	if (r != DMP_OK) {
		condlog(2, "%s: %s", mpp->alias, (r == DMP_ERR)? "error getting table" : "map not present");
		return r;
	}

	if (disassemble_map(pathvec, params, mpp)) {
		condlog(2, "%s: cannot disassemble map", mpp->alias);
		free(params);
		return DMP_ERR;
	}

	free(params);
	params = NULL;
	if (dm_get_status(mpp->alias, &params) != DMP_OK)
		condlog(2, "%s: %s", mpp->alias, (r == DMP_ERR)? "error getting status" : "map not present");
	else if (disassemble_status(params, mpp))
		condlog(2, "%s: cannot disassemble status", mpp->alias);
	free(params);

	/* FIXME: we should deal with the return value here */
	update_pathvec_from_dm(pathvec, mpp, flags);

	return DMP_OK;
}

static struct path *find_devt_in_pathgroups(const struct multipath *mpp,
					    const char *dev_t)
{
	struct pathgroup  *pgp;
	struct path *pp;
	int j;

	vector_foreach_slot(mpp->pg, pgp, j) {
		pp = find_path_by_devt(pgp->paths, dev_t);
		if (pp)
			return pp;
	}
	return NULL;
}

static void check_removed_paths(const struct multipath *mpp, vector pathvec)
{
	struct path *pp;
	int i;

	vector_foreach_slot(pathvec, pp, i) {
		if (pp->mpp == mpp &&
		    (pp->initialized == INIT_REMOVED ||
		     pp->initialized == INIT_PARTIAL) &&
		    !find_devt_in_pathgroups(mpp, pp->dev_t)) {
			condlog(2, "%s: %s: freeing path in %s state",
				__func__, pp->dev,
				pp->initialized == INIT_REMOVED ?
				"removed" : "partial");
			vector_del_slot(pathvec, i--);
			free_path(pp);
		}
	}
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
	check_removed_paths(mpp, pathvec);
	update_mpp_paths(mpp, pathvec);
	vector_foreach_slot (mpp->paths, pp, i)
		pp->mpp = mpp;
}

int
update_multipath_strings(struct multipath *mpp, vector pathvec)
{
	struct pathgroup *pgp;
	int i, r = DMP_ERR;

	if (!mpp)
		return r;

	update_mpp_paths(mpp, pathvec);
	condlog(4, "%s: %s", mpp->alias, __FUNCTION__);

	free_multipath_attributes(mpp);
	free_pgvec(mpp->pg, KEEP_PATHS);
	mpp->pg = NULL;

	r = update_multipath_table(mpp, pathvec, 0);
	if (r != DMP_OK)
		return r;

	sync_paths(mpp, pathvec);

	vector_foreach_slot(mpp->pg, pgp, i)
		if (pgp->paths)
			path_group_prio_update(pgp);

	return DMP_OK;
}

static void enter_recovery_mode(struct multipath *mpp)
{
	unsigned int checkint;
	struct config *conf;

	if (mpp->in_recovery || mpp->no_path_retry <= 0)
		return;

	conf = get_multipath_config();
	checkint = conf->checkint;
	put_multipath_config(conf);

	/*
	 * Enter retry mode.
	 * meaning of +1: retry_tick may be decremented in checkerloop before
	 * starting retry.
	 */
	mpp->in_recovery = true;
	mpp->stat_queueing_timeouts++;
	mpp->retry_tick = mpp->no_path_retry * checkint + 1;
	condlog(1, "%s: Entering recovery mode: max_retries=%d",
		mpp->alias, mpp->no_path_retry);
}

static void leave_recovery_mode(struct multipath *mpp)
{
	bool recovery = mpp->in_recovery;

	mpp->in_recovery = false;
	mpp->retry_tick = 0;

	/*
	 * in_recovery is only ever set if mpp->no_path_retry > 0
	 * (see enter_recovery_mode()). But no_path_retry may have been
	 * changed while the map was recovering, so test it here again.
	 */
	if (recovery && (mpp->no_path_retry == NO_PATH_RETRY_QUEUE ||
			 mpp->no_path_retry > 0)) {
		dm_queue_if_no_path(mpp->alias, 1);
		condlog(2, "%s: queue_if_no_path enabled", mpp->alias);
		condlog(1, "%s: Recovered to normal mode", mpp->alias);
	}
}

void __set_no_path_retry(struct multipath *mpp, bool check_features)
{
	bool is_queueing = false; /* assign a value to make gcc happy */

	check_features = check_features && mpp->features != NULL;
	if (check_features)
		is_queueing = strstr(mpp->features, "queue_if_no_path");

	switch (mpp->no_path_retry) {
	case NO_PATH_RETRY_UNDEF:
		break;
	case NO_PATH_RETRY_FAIL:
		if (!check_features || is_queueing)
			dm_queue_if_no_path(mpp->alias, 0);
		break;
	case NO_PATH_RETRY_QUEUE:
		if (!check_features || !is_queueing)
			dm_queue_if_no_path(mpp->alias, 1);
		break;
	default:
		if (count_active_paths(mpp) > 0) {
			/*
			 * If in_recovery is set, leave_recovery_mode() takes
			 * care of dm_queue_if_no_path. Otherwise, do it here.
			 */
			if ((!check_features || !is_queueing) &&
			    !mpp->in_recovery)
				dm_queue_if_no_path(mpp->alias, 1);
			leave_recovery_mode(mpp);
		} else
			enter_recovery_mode(mpp);
		break;
	}
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
				  pp->state == PATH_SHAKY)) {
				condlog(2, "sync_map_state: failing %s state %d dmstate %d",
					pp->dev, pp->state, pp->dmstate);
				dm_fail_path(mpp->alias, pp->dev_t);
			}
		}
	}
}

static void
find_existing_alias (struct multipath * mpp,
		     struct vectors *vecs)
{
	struct multipath * mp;
	int i;

	vector_foreach_slot (vecs->mpvec, mp, i)
		if (strncmp(mp->wwid, mpp->wwid, WWID_SIZE - 1) == 0) {
			strlcpy(mpp->alias_old, mp->alias, WWID_SIZE);
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
	put_multipath_config(conf);

	/*
	 * We need to call this before select_alias(),
	 * because that accesses hwe properties.
	 */
	if (pp->hwe && !set_mpp_hwe(mpp, pp))
		goto out;

	strcpy(mpp->wwid, pp->wwid);
	find_existing_alias(mpp, vecs);
	if (select_alias(conf, mpp))
		goto out;
	mpp->size = pp->size;

	if (adopt_paths(vecs->pathvec, mpp) || pp->mpp != mpp ||
	    find_slot(mpp->paths, pp) == -1)
		goto out;

	if (add_vec) {
		if (!vector_alloc_slot(vecs->mpvec))
			goto out;

		vector_set_slot(vecs->mpvec, mpp);
	}

	return mpp;

out:
	remove_map(mpp, vecs->pathvec, vecs->mpvec);
	return NULL;
}

int verify_paths(struct multipath *mpp)
{
	struct path * pp;
	int count = 0;
	int i;

	if (!mpp)
		return 0;

	vector_foreach_slot (mpp->paths, pp, i) {
		/*
		 * see if path is in sysfs
		 */
		if (!pp->udev || sysfs_attr_get_value(pp->udev, "dev",
					 pp->dev_t, BLK_DEV_SIZE) < 0) {
			if (pp->state != PATH_DOWN) {
				condlog(1, "%s: removing valid path %s in state %d",
					mpp->alias, pp->dev, pp->state);
			} else {
				condlog(2, "%s: failed to access path %s",
					mpp->alias, pp->dev);
			}
			count++;
			vector_del_slot(mpp->paths, i);
			i--;

			/*
			 * Don't delete path from pathvec yet. We'll do this
			 * after the path has been removed from the map, in
			 * sync_paths().
			 */
			set_path_removed(pp);
		} else {
			condlog(4, "%s: verified path %s dev_t %s",
				mpp->alias, pp->dev, pp->dev_t);
		}
	}
	return count;
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
	int active = count_active_paths(mpp);

	if (active == 0) {
		enter_recovery_mode(mpp);
		if (mpp->no_path_retry != NO_PATH_RETRY_QUEUE)
			mpp->stat_map_failures++;
	}
	condlog(2, "%s: remaining active paths: %d", mpp->alias, active);
}

void update_queue_mode_add_path(struct multipath *mpp)
{
	int active = count_active_paths(mpp);

	if (active > 0)
		leave_recovery_mode(mpp);
	condlog(2, "%s: remaining active paths: %d", mpp->alias, active);
}

vector get_used_hwes(const struct _vector *pathvec)
{
	int i, j;
	struct path *pp;
	struct hwentry *hwe;
	vector v = vector_alloc();

	if (v == NULL)
		return NULL;

	vector_foreach_slot(pathvec, pp, i) {
		vector_foreach_slot_backwards(pp->hwe, hwe, j) {
			vector_find_or_add_slot(v, hwe);
		}
	}

	return v;
}
