#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
			if (pp->size != 0 && mpp->size != 0 &&
			    pp->size != mpp->size) {
				condlog(3, "%s: size mismatch for %s, not adding path",
					pp->dev, mpp->alias);
				continue;
			}
			condlog(3, "%s: ownership set to %s",
				pp->dev, mpp->alias);
			pp->mpp = mpp;

			if (!mpp->paths && !(mpp->paths = vector_alloc()))
				return 1;

			if (!find_path_by_dev(mpp->paths, pp->dev) &&
			    store_path(mpp->paths, pp))
					return 1;
			conf = get_multipath_config();
			pthread_cleanup_push(put_multipath_config, conf);
			ret = pathinfo(pp, conf,
				       DI_PRIO | DI_CHECKER);
			pthread_cleanup_pop(1);
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

void orphan_paths(vector pathvec, struct multipath *mpp, const char *reason)
{
	int i;
	struct path * pp;

	vector_foreach_slot (pathvec, pp, i) {
		if (pp->mpp == mpp) {
			orphan_path(pp, reason);
		}
	}
}

void
remove_map(struct multipath * mpp, struct vectors * vecs, int purge_vec)
{
	int i;

	/*
	 * clear references to this map
	 */
	orphan_paths(vecs->pathvec, mpp, "map removed internally");

	if (purge_vec &&
	    (i = find_slot(vecs->mpvec, (void *)mpp)) != -1)
		vector_del_slot(vecs->mpvec, i);

	/*
	 * final free
	 */
	free_multipath(mpp, KEEP_PATHS);
}

void
remove_map_by_alias(const char *alias, struct vectors * vecs, int purge_vec)
{
	struct multipath * mpp = find_mp_by_alias(vecs->mpvec, alias);
	if (mpp) {
		condlog(2, "%s: removing map by alias", alias);
		remove_map(mpp, vecs, purge_vec);
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
	struct pathgroup *pgp;
	int i;

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

	vector_foreach_slot(mpp->pg, pgp, i)
		if (pgp->paths)
			path_group_prio_update(pgp);

	return 0;
}

void enter_recovery_mode(struct multipath *mpp)
{
	int checkint;
	struct config *conf = get_multipath_config();
	checkint = conf->checkint;
	put_multipath_config(conf);

	/*
	 * Enter retry mode.
	 * meaning of +1: retry_tick may be decremented in checkerloop before
	 * starting retry.
	 */
	mpp->stat_queueing_timeouts++;
	mpp->retry_tick = mpp->no_path_retry * checkint + 1;
	condlog(1, "%s: Entering recovery mode: max_retries=%d",
		mpp->alias, mpp->no_path_retry);
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

			/* Make sure mpp->hwe doesn't point to freed memory.
			 * We call extract_hwe_from_path() below to restore
			 * mpp->hwe
			 */
			if (mpp->hwe == pp->hwe)
				mpp->hwe = NULL;
			if ((j = find_slot(vecs->pathvec,
					   (void *)pp)) != -1)
				vector_del_slot(vecs->pathvec, j);
			free_path(pp);
		} else {
			condlog(4, "%s: verified path %s dev_t %s",
				mpp->alias, pp->dev, pp->dev_t);
		}
	}
	extract_hwe_from_path(mpp);
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
