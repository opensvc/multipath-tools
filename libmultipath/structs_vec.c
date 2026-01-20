#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "mt-udev-wrap.h"

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
 * Side effects:
 * - may delete non-existing paths and empty pathgroups from mpp
 * - may set pp->wwid and / or mpp->wwid
 * - calls pathinfo() on existing paths is pathinfo_flags is not 0
 */
static void update_pathvec_from_dm(vector pathvec, struct multipath *mpp,
	int pathinfo_flags)
{
	int i, j;
	struct pathgroup *pgp;
	struct path *pp;
	struct config *conf;
	bool mpp_has_wwid;
	bool must_reload = false;
	bool pg_deleted = false;
	bool map_discovery = !!(pathinfo_flags & DI_DISCOVERY);

	pathinfo_flags &= ~DI_DISCOVERY;

	if (!mpp->pg)
		return;

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

			/* A pathgroup has been deleted before. Invalidate pgindex */
			if (pg_deleted)
				pp->pgindex = 0;

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
				/*
				 * pp->pgindex has been set in disassemble_map(),
				 * which has probably been called just before for
				 * mpp. So he pgindex relates to mpp and may be
				 * wrong for pp->mpp. Invalidate it.
				 */
				pp->pgindex = 0;
				continue;
			}
			pp->mpp = mpp;

			/*
			 * The way disassemble_map() works: If it encounters a
			 * path device which isn't found in pathvec, it adds an
			 * uninitialized struct path to pgp->paths, with only
			 * pp->dev_t filled in. Thus if pp->udev is set here,
			 * we know that the path is in pathvec already.
			 */
			if (pp->udev) {
				if (pathinfo_flags & ~DI_NOIO) {
					conf = get_multipath_config();
					pthread_cleanup_push(put_multipath_config,
							     conf);
					if (pathinfo(pp, conf, pathinfo_flags) != PATHINFO_OK)
						condlog(2, "%s: pathinfo failed for existing path %s (flags=0x%x)",
							__func__, pp->dev, pathinfo_flags);
					pthread_cleanup_pop(1);
				}
			} else {
				/* If this fails, the device is not in sysfs */
				pp->udev = get_udev_device(pp->dev_t, DEV_DEVT);

				if (!pp->udev) {
					condlog(2, "%s: discarding non-existing path %s",
						mpp->alias, pp->dev_t);
					vector_del_slot(pgp->paths, j--);
					pp->mpp = NULL;
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
					if (rc == PATHINFO_FAILED ||
					    (rc == PATHINFO_SKIPPED && !map_discovery)) {
						condlog(1, "%s: error %d in pathinfo, discarding path",
							pp->dev, rc);
						vector_del_slot(pgp->paths, j--);
						pp->mpp = NULL;
						free_path(pp);
						must_reload = true;
						continue;
					}
					if (rc == PATHINFO_SKIPPED) {
						condlog(1, "%s: blacklisted path in %s", pp->dev, mpp->alias);
						set_path_removed(pp);
						must_reload = true;
					} else {
						condlog(2, "%s: adding new path %s", mpp->alias, pp->dev);
						pp->initialized = INIT_PARTIAL;
						pp->partial_retrigger_delay = 180;
					}
					store_path(pathvec, pp);
					pp->tick = 1;
				}
			}

			/* We don't set the map WWID from paths here */
			if (!mpp_has_wwid)
				continue;

			/*
			 * At this point, pp->udev is valid and pp->wwid
			 * is the best we could get
			 */
			if (*pp->wwid && strcmp(mpp->wwid, pp->wwid)) {
				condlog(0, "%s: path %s WWID %s doesn't match, removing from map",
					mpp->wwid, pp->dev_t, pp->wwid);
				/*
				 * This path exists, but in the wrong map.
				 * We can't reload the map from here.
				 * Make sure it isn't used in this map
				 * anymore, and let the checker re-add
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
		free_pathgroup(pgp);
		must_reload = true;
		/* Invalidate pgindex for all other pathgroups */
		pg_deleted = true;
	}
	mpp->need_reload = mpp->need_reload || must_reload;
}

static bool set_path_max_sectors_kb(const struct path *pp, int max_sectors_kb)
{
	char buf[11];
	ssize_t len;
	int ret, current;
	bool rc = false;

	if (max_sectors_kb == MAX_SECTORS_KB_UNDEF)
		return rc;

	if (sysfs_attr_get_value(pp->udev, "queue/max_sectors_kb", buf, sizeof(buf)) <= 0
	    || sscanf(buf, "%d\n", &current) != 1)
		current = MAX_SECTORS_KB_UNDEF;
	if (current == max_sectors_kb)
		return rc;

	len = snprintf(buf, sizeof(buf), "%d", max_sectors_kb);
	ret = sysfs_attr_set_value(pp->udev, "queue/max_sectors_kb", buf, len);
	if (ret != len)
		log_sysfs_attr_set_value(3, ret,
					 "failed setting max_sectors_kb on %s",
					 pp->dev);
	else {
		condlog(3, "%s: set max_sectors_kb to %d for %s", __func__,
			max_sectors_kb, pp->dev);
		rc = true;
	}
	return rc;
}

int adopt_paths(vector pathvec, struct multipath *mpp,
		const struct multipath *current_mpp)
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
			if (mpp->queue_mode == QUEUE_MODE_RQ &&
			    pp->bus == SYSFS_BUS_NVME &&
			    pp->sg_id.proto_id == NVME_PROTOCOL_TCP) {
				condlog(2, "%s: multipath device %s created with request queue_mode. Unable to add nvme:tcp paths",
					pp->dev, mpp->alias);
				continue;
			}
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

			if (!find_path_by_devt(mpp->paths, pp->dev_t)) {

				if (store_path(mpp->paths, pp))
					goto err;
				/*
				 * Setting max_sectors_kb on live paths is dangerous.
				 * But we can do it here on a path that isn't yet part
				 * of the map. If this value is lower than the current
				 * max_sectors_kb and the map is reloaded, the map's
				 * max_sectors_kb will be safely adjusted by the kernel.
				 *
				 * We must make sure that the path is not part of the
				 * map yet. Normally we can check this in mpp->paths.
				 * But if adopt_paths is called from coalesce_paths,
				 * we need to check the separate struct multipath that
				 * has been obtained from map_discovery().
				 */
				if (!current_mpp ||
				    !mp_find_path_by_devt(current_mpp, pp->dev_t))
					set_path_max_sectors_kb(pp, mpp->max_sectors_kb);
			}

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

static void orphan_path__(struct path *pp, const char *reason)
{
	condlog(3, "%s: orphan path, %s", pp->dev, reason);
	uninitialize_path(pp);
}

void orphan_path(struct path *pp, const char *reason)
{
	pp->mpp = NULL;
	orphan_path__(pp, reason);
}

static void orphan_paths(vector pathvec, struct multipath *mpp, const char *reason)
{
	int i;
	struct path * pp;

	vector_foreach_slot (pathvec, pp, i) {
		if (pp->mpp == mpp)
			orphan_path(pp, reason);
		else if (pp->add_when_online &&
			 strncmp(mpp->wwid, pp->wwid, WWID_SIZE) == 0) {
			pp->add_when_online = false;
		}
	}
}

void set_path_removed(struct path *pp)
{
	/*
	 * Keep link to mpp. It will be removed when the path
	 * is successfully removed from the map.
	 */
	if (!pp->mpp)
		condlog(0, "%s: internal error: mpp == NULL", pp->dev);
	orphan_path__(pp, "removed");
	pp->initialized = INIT_REMOVED;
}

void remove_map_callback(struct multipath *mpp __attribute__((unused)))
{
}

void remove_map_from_mpvec(const struct multipath *mpp, vector mpvec)
{
	int i = find_slot(mpvec, mpp);

	if (i != -1)
		vector_del_slot(mpvec, i);
}

void remove_map(struct multipath *mpp, vector pathvec)
{
	remove_map_callback(mpp);

	/*
	 * clear references to this map.
	 * This needs to be called before free_multipath(),
	 * because of the add_when_online logic.
	 */
	orphan_paths(pathvec, mpp, "map removed internally");

	free_multipath(mpp);
}

void
remove_map_by_alias(const char *alias, struct vectors * vecs)
{
	struct multipath * mpp = find_mp_by_alias(vecs->mpvec, alias);
	if (mpp) {
		condlog(2, "%s: removing map by alias", alias);
		remove_map_from_mpvec(mpp, vecs->mpvec);
		remove_map(mpp, vecs->pathvec);
	}
}

void
remove_maps(struct vectors * vecs)
{
	int i;
	struct multipath * mpp;

	if (!vecs)
		return;

	vector_foreach_slot (vecs->mpvec, mpp, i)
		remove_map(mpp, vecs->pathvec);

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
update_multipath_table__ (struct multipath *mpp, vector pathvec, int flags,
			  const char *params, const char *status)
{
	if (disassemble_map(pathvec, params, mpp)) {
		condlog(2, "%s: cannot disassemble map", mpp->alias);
		return DMP_ERR;
	}

	if (disassemble_status(status, mpp))
		condlog(2, "%s: cannot disassemble status", mpp->alias);

	update_pathvec_from_dm(pathvec, mpp, flags);

	return DMP_OK;
}

int
update_multipath_table (struct multipath *mpp, vector pathvec, int flags)
{
	int r = DMP_ERR;
	char __attribute__((cleanup(cleanup_charp))) *params = NULL;
	char __attribute__((cleanup(cleanup_charp))) *status = NULL;
	/* only set the actual mpp->dmi if libmp_mapinfo returns DMP_OK */
	struct dm_info dmi;
	unsigned long long size;
	struct config *conf;

	if (!mpp)
		return r;

	size = mpp->size;
	conf = get_multipath_config();
	mpp->sync_tick = conf->max_checkint;
	put_multipath_config(conf);

	r = libmp_mapinfo(DM_MAP_BY_NAME | MAPINFO_MPATH_ONLY,
			  (mapid_t) { .str = mpp->alias },
			  (mapinfo_t) {
				  .target = &params,
				  .status = &status,
				  .size = &mpp->size,
				  .dmi = &dmi,
			  });

	if (r != DMP_OK) {
		condlog(2, "%s: %s", mpp->alias, dmp_errstr(r));
		return r;
	} else if (size != mpp->size)
		condlog(0, "%s: size changed from %llu to %llu", mpp->alias, size, mpp->size);

	mpp->dmi = dmi;
	return update_multipath_table__(mpp, pathvec, flags, params, status);
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

/*
 * check_removed_paths()
 *
 * This function removes paths from the pathvec and frees them if they have
 * been marked for removal (INIT_REMOVED, INIT_PARTIAL).
 * This is important because some callers (e.g. uev_add_path->ev_remove_path())
 * rely on the paths being actually gone when the call stack returns.
 * Be sure not to call it while these paths are still referenced elsewhere
 * (e.g. from coalesce_paths(), where curmp may still reference them).
 *
 * Most important call stacks in multipath-tools 0.13.0:
 *
 * checker_finished()
 *   sync_mpp()
 *     do_sync_mpp()
 *       update_multipath_strings()
 *         sync_paths()
 *           check_removed_paths()
 *
 * [multiple callers including update_map(), ev_remove_path(), ...]
 *   setup_multipath()
 *     refresh_multipath()
 *       update_multipath_strings()
 *         sync_paths()
 *           check_removed_paths()
 *
 * refresh_multipath() is also called from a couple of CLI handlers.
 */
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
			pp->mpp = NULL;
			free_path(pp);
		}
	}
}

/* This function may free paths. See check_removed_paths(). */
void sync_paths(struct multipath *mpp, vector pathvec)
{
	struct path *pp;
	struct pathgroup  *pgp;
	int found, i, j;

	vector_foreach_slot (mpp->paths, pp, i) {
		found = 0;
		vector_foreach_slot(mpp->pg, pgp, j) {
			if (find_slot(pgp->paths, (void *)pp) != -1) {
				if (pp->add_when_online)
					pp->add_when_online = false;
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
		if (pp->mpp != mpp) {
			condlog(2, "%s: %s: mpp of %s was %p, fixing to %p",
				__func__, mpp->alias, pp->dev_t, pp->mpp, mpp);
			pp->mpp = mpp;
		};
}

/* This function may free paths. See check_removed_paths(). */
int update_multipath_strings(struct multipath *mpp, vector pathvec)
{
	struct pathgroup *pgp;
	int i, r = DMP_ERR;

	if (!mpp)
		return r;

	update_mpp_paths(mpp, pathvec);
	condlog(4, "%s: %s", mpp->alias, __FUNCTION__);

	free_multipath_attributes(mpp);
	free_pgvec(mpp->pg);
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
		dm_queue_if_no_path(mpp, 1);
		condlog(2, "%s: queue_if_no_path enabled", mpp->alias);
		condlog(1, "%s: Recovered to normal mode", mpp->alias);
	}
}

void set_no_path_retry(struct multipath *mpp)
{
	bool is_queueing = false; /* assign a value to make gcc happy */

	if (mpp->features)
		is_queueing = strstr(mpp->features, "queue_if_no_path");

	switch (mpp->no_path_retry) {
	case NO_PATH_RETRY_UNDEF:
		break;
	case NO_PATH_RETRY_FAIL:
		if (!mpp->features || is_queueing)
			dm_queue_if_no_path(mpp, 0);
		break;
	case NO_PATH_RETRY_QUEUE:
		if (!mpp->features || !is_queueing)
			dm_queue_if_no_path(mpp, 1);
		break;
	default:
		if (count_active_paths(mpp) > 0) {
			/*
			 * If in_recovery is set, leave_recovery_mode() takes
			 * care of dm_queue_if_no_path. Otherwise, do it here.
			 */
			if ((!mpp->features || !is_queueing) &&
			    !mpp->in_recovery)
				dm_queue_if_no_path(mpp, 1);
			leave_recovery_mode(mpp);
		} else {
			/*
			 * If in_recovery is set, enter_recovery_mode does
			 * nothing. If the device is already in recovery
			 * mode and has already timed out, manually call
			 * dm_queue_if_no_path to stop it from queueing.
			 */
			if ((!mpp->features || is_queueing) &&
			    mpp->in_recovery && mpp->retry_tick == 0)
				dm_queue_if_no_path(mpp, 0);
			if (pathcount(mpp, PATH_PENDING) == 0)
				enter_recovery_mode(mpp);
		}
		break;
	}
}

void
sync_map_state(struct multipath *mpp, bool reinstate_only)
{
	struct pathgroup *pgp;
	struct path *pp;
	unsigned int i, j;

	if (!mpp->pg)
		return;

	vector_foreach_slot (mpp->pg, pgp, i){
		vector_foreach_slot (pgp->paths, pp, j){
			if (pp->initialized == INIT_REMOVED ||
			    pp->state == PATH_UNCHECKED ||
			    pp->state == PATH_WILD ||
			    pp->state == PATH_DELAYED)
				continue;
			if (mpp->ghost_delay_tick > 0)
				continue;
			if ((pp->dmstate == PSTATE_FAILED ||
			     pp->dmstate == PSTATE_UNDEF) &&
			    (pp->state == PATH_UP || pp->state == PATH_GHOST))
				dm_reinstate_path(mpp->alias, pp->dev_t);
			else if (!reinstate_only &&
				 (pp->dmstate == PSTATE_ACTIVE ||
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
				    int add_vec, const struct multipath *current_mpp)
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

	if (adopt_paths(vecs->pathvec, mpp, current_mpp) || pp->mpp != mpp ||
	    find_slot(mpp->paths, pp) == -1)
		goto out;

	if (add_vec) {
		if (!vector_alloc_slot(vecs->mpvec))
			goto out;

		vector_set_slot(vecs->mpvec, mpp);
	}

	return mpp;

out:
	remove_map_from_mpvec(mpp, vecs->mpvec);
	remove_map(mpp, vecs->pathvec);
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
 *
 * Since this will only be called when fail_path(), update_multipath(), or
 * io_err_stat_handle_pathfail() are failing a previously active path, the
 * device cannot already be in recovery mode, so there will never be a need
 * to disable queueing here.
 */
void update_queue_mode_del_path(struct multipath *mpp)
{
	int active = count_active_paths(mpp);
	bool is_queueing = mpp->features &&
			   strstr(mpp->features, "queue_if_no_path");

	if (active == 0) {
		enter_recovery_mode(mpp);
		if (mpp->no_path_retry == NO_PATH_RETRY_FAIL ||
		    (mpp->no_path_retry == NO_PATH_RETRY_UNDEF && !is_queueing))
			mpp->stat_map_failures++;
	}
	condlog(2, "%s: remaining active paths: %d", mpp->alias, active);
}

/*
 * Since this will only be called from check_path() -> reinstate_path() after
 * the queueing state has been updated in set_no_path_retry, this does not
 * need to worry about modifying the queueing state except when actually
 * leaving recovery mode.
 */
void update_queue_mode_add_path(struct multipath *mpp)
{
	int active = count_active_paths(mpp);

	if (active > 0)
		leave_recovery_mode(mpp);
	condlog(2, "%s: remaining active paths: %d", mpp->alias, active);
}

vector get_used_hwes(const struct vector_s *pathvec)
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
