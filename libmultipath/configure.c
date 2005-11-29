/*
 * Copyright (c) 2003, 2004, 2005 Christophe Varoqui
 * Copyright (c) 2005 Benjamin Marzinski, Redhat
 * Copyright (c) 2005 Kiyoshi Ueda, NEC
 * Copyright (c) 2005 Patrick Caulfield, Redhat
 * Copyright (c) 2005 Edward Goggin, EMC
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/file.h>
#include <errno.h>
#include <libdevmapper.h>

#include "../libcheckers/path_state.h"
#include "vector.h"
#include "memory.h"
#include "devmapper.h"
#include "blacklist.h"
#include "defaults.h"
#include "structs.h"
#include "structs_vec.h"
#include "dmparser.h"
#include "config.h"
#include "propsel.h"
#include "discovery.h"
#include "debug.h"
#include "switchgroup.h"
#include "print.h"
#include "configure.h"
#include "pgpolicies.h"
#include "dict.h"
#include "alias.h"

extern int
setup_map (struct multipath * mpp)
{
	struct pathgroup * pgp;
	int i;
	
	/*
	 * don't bother if devmap size is unknown
	 */
	if (mpp->size <= 0) {
		condlog(3, "%s: devmap size is unknown", mpp->alias);
		return 1;
	}

	/*
	 * properties selectors
	 */
	select_pgfailback(mpp);
	select_pgpolicy(mpp);
	select_selector(mpp);
	select_features(mpp);
	select_hwhandler(mpp);
	select_rr_weight(mpp);
	select_minio(mpp);
	select_no_path_retry(mpp);

	/*
	 * assign paths to path groups -- start with no groups and all paths
	 * in mpp->paths
	 */
	if (mpp->pg) {
		vector_foreach_slot (mpp->pg, pgp, i)
			free_pathgroup(pgp, KEEP_PATHS);

		vector_free(mpp->pg);
		mpp->pg = NULL;
	}
	if (mpp->pgpolicyfn && mpp->pgpolicyfn(mpp))
		return 1;

	mpp->nr_active = pathcount(mpp, PATH_UP);

	/*
	 * ponders each path group and determine highest prio pg
	 * to switch over (default to first)
	 */
	mpp->bestpg = select_path_group(mpp);

	/*
	 * transform the mp->pg vector of vectors of paths
	 * into a mp->params strings to feed the device-mapper
	 */
	if (assemble_map(mpp)) {
		condlog(0, "%s: problem assembing map", mpp->alias);
		return 1;
	}
	return 0;
}

static void
compute_pgid(struct pathgroup * pgp)
{
	struct path * pp;
	int i;

	vector_foreach_slot (pgp->paths, pp, i)
		pgp->id ^= (long)pp;
}

static int
pgcmp (struct multipath * mpp, struct multipath * cmpp)
{
	int i, j;
	struct pathgroup * pgp;
	struct pathgroup * cpgp;
	int r = 0;

	vector_foreach_slot (mpp->pg, pgp, i) {
		compute_pgid(pgp);

		vector_foreach_slot (cmpp->pg, cpgp, j) {
			if (pgp->id == cpgp->id) {
				r = 0;
				break;
			}
			r++;
		}
		if (r)
			return r;
	}
	return r;
}

static void
select_action (struct multipath * mpp, vector curmp)
{
	struct multipath * cmpp;

	cmpp = find_mp_by_alias(curmp, mpp->alias);

	if (!cmpp) {
		cmpp = find_mp_by_wwid(curmp, mpp->wwid);

		if (cmpp && !conf->dry_run) {
			condlog(2, "%s: rename: %s to %s", mpp->wwid,
				cmpp->alias, mpp->alias);
			strncpy(mpp->alias_old, cmpp->alias, WWID_SIZE);
			mpp->action = ACT_RENAME;
			return;
		}
		else {
			condlog(3, "set ACT_CREATE: map does not exist");
			mpp->action = ACT_CREATE;
		}
		mpp->action = ACT_CREATE;
		condlog(3, "set ACT_CREATE: map does not exist");
		return;
	}

	if (!find_mp_by_wwid(curmp, mpp->wwid)) {
		condlog(2, "remove: %s (wwid changed)", cmpp->alias);
		dm_flush_map(mpp->alias, DEFAULT_TARGET);
		strncat(cmpp->wwid, mpp->wwid, WWID_SIZE);
		drop_multipath(curmp, cmpp->wwid, KEEP_PATHS);
		mpp->action = ACT_CREATE;
		condlog(3, "set ACT_CREATE: map wwid change");
		return;
	}
		
	if (pathcount(mpp, PATH_UP) == 0) {
		mpp->action = ACT_NOTHING;
		condlog(3, "set ACT_NOTHING: no usable path");
		return;
	}
	if (cmpp->size != mpp->size) {
		mpp->action = ACT_RELOAD;
		condlog(3, "set ACT_RELOAD: size change");
		return;
	}
	if (!mpp->no_path_retry && /* let features be handled by the daemon */
	    strncmp(cmpp->features, mpp->features, strlen(mpp->features))) {
		mpp->action =  ACT_RELOAD;
		condlog(3, "set ACT_RELOAD: features change");
		return;
	}
	if (strncmp(cmpp->hwhandler, mpp->hwhandler,
		    strlen(mpp->hwhandler))) {
		mpp->action = ACT_RELOAD;
		condlog(3, "set ACT_RELOAD: hwhandler change");
		return;
	}
	if (strncmp(cmpp->selector, mpp->selector,
		    strlen(mpp->selector))) {
		mpp->action = ACT_RELOAD;
		condlog(3, "set ACT_RELOAD: selector change");
		return;
	}
	if (cmpp->minio != mpp->minio) {
		mpp->action = ACT_RELOAD;
		condlog(3, "set ACT_RELOAD: minio change (%u->%u)",
			cmpp->minio, mpp->minio);
		return;
	}
	if (VECTOR_SIZE(cmpp->pg) != VECTOR_SIZE(mpp->pg)) {
		mpp->action = ACT_RELOAD;
		condlog(3, "set ACT_RELOAD: number of path group change");
		return;
	}
	if (pgcmp(mpp, cmpp)) {
		mpp->action = ACT_RELOAD;
		condlog(3, "set ACT_RELOAD: path group topology change");
		return;
	}
	if (cmpp->nextpg != mpp->bestpg) {
		mpp->action = ACT_SWITCHPG;
		condlog(3, "set ACT_SWITCHPG: next path group change");
		return;
	}
	mpp->action = ACT_NOTHING;
	condlog(3, "set ACT_NOTHING: map unchanged");
	return;
}

extern int
reinstate_paths (struct multipath * mpp)
{
	int i, j;
	struct pathgroup * pgp;
	struct path * pp;

	if (!mpp->pg)
		return 0;

	vector_foreach_slot (mpp->pg, pgp, i) {
		if (!pgp->paths)
			continue;

		vector_foreach_slot (pgp->paths, pp, j) {
			if (pp->state != PATH_UP &&
			    (pgp->status == PGSTATE_DISABLED ||
			     pgp->status == PGSTATE_ACTIVE))
				continue;

			if (pp->dmstate == PSTATE_FAILED) {
				if (dm_reinstate_path(mpp->alias, pp->dev_t))
					condlog(0, "error reinstating %s",
						pp->dev);
			}
		}
	}
	return 0;
}

static int
lock_multipath (struct multipath * mpp, int lock)
{
	struct pathgroup * pgp;
	struct path * pp;
	int i, j;

	if (!mpp || !mpp->pg)
		return 0;
	
	vector_foreach_slot (mpp->pg, pgp, i) {
		if (!pgp->paths)
			continue;
		vector_foreach_slot(pgp->paths, pp, j) {
			if (lock && flock(pp->fd, LOCK_EX | LOCK_NB) &&
			    errno == EWOULDBLOCK)
				return 1;
			else if (!lock)
				flock(pp->fd, LOCK_UN);
		}
	}
	return 0;
}

/*
 * Return value:
 *  -1: Retry
 *   0: DM_DEVICE_CREATE or DM_DEVICE_RELOAD failed, or dry_run mode.
 *   1: DM_DEVICE_CREATE or DM_DEVICE_RELOAD succeeded.
 *   2: Map is already existing.
 */
extern int
domap (struct multipath * mpp)
{
	int r = 0;

	/*
	 * last chance to quit before touching the devmaps
	 */
	if (conf->dry_run) {
		print_mp(mpp, conf->verbosity);
		return 0;
	}

	switch (mpp->action) {
	case ACT_REJECT:
	case ACT_NOTHING:
		return 2;

	case ACT_SWITCHPG:
		dm_switchgroup(mpp->alias, mpp->bestpg);
		/*
		 * we may have avoided reinstating paths because there where in
		 * active or disabled PG. Now that the topology has changed,
		 * retry.
		 */
		reinstate_paths(mpp);
		return 2;

	case ACT_CREATE:
		if (lock_multipath(mpp, 1)) {
			condlog(3, "%s: in use", mpp->alias);
			return -1;
		}
		dm_shut_log();

		if (dm_map_present(mpp->alias))
			break;

		r = dm_addmap(DM_DEVICE_CREATE, mpp->alias, DEFAULT_TARGET,
			      mpp->params, mpp->size, mpp->wwid);

		/*
		 * DM_DEVICE_CREATE is actually DM_DEV_CREATE plus
		 * DM_TABLE_LOAD. Failing the second part leaves an
		 * empty map. Clean it up.
		 */
		if (!r && dm_map_present(mpp->alias)) {
			condlog(3, "%s: failed to load map "
				   "(a path might be in use)",
				   mpp->alias);
			dm_flush_map(mpp->alias, DEFAULT_TARGET);
		}

		lock_multipath(mpp, 0);
		dm_restore_log();
		break;

	case ACT_RELOAD:
		r = (dm_addmap(DM_DEVICE_RELOAD, mpp->alias, DEFAULT_TARGET,
			      mpp->params, mpp->size, NULL) &&
		     dm_simplecmd(DM_DEVICE_RESUME, mpp->alias));
		break;

	case ACT_RENAME:
		r = dm_rename(mpp->alias_old, mpp->alias);
		break;

	default:
		break;
	}

	if (r) {
		/*
		 * DM_DEVICE_CREATE, DM_DEIVCE_RENAME, or DM_DEVICE_RELOAD
		 * succeeded
		 */
#ifndef DAEMON
		dm_switchgroup(mpp->alias, mpp->bestpg);
		if (mpp->action != ACT_NOTHING)
			print_mp(mpp, conf->verbosity);
#else
		mpp->stat_map_loads++;
		condlog(2, "%s: load table [0 %llu %s %s]", mpp->alias,
                        mpp->size, DEFAULT_TARGET, mpp->params);
#endif
	}

	return r;
}

static int
deadmap (struct multipath * mpp)
{
	int i, j;
	struct pathgroup * pgp;
	struct path * pp;

	if (!mpp->pg)
		return 1;

	vector_foreach_slot (mpp->pg, pgp, i) {
		if (!pgp->paths)
			continue;

		vector_foreach_slot (pgp->paths, pp, j)
			if (strlen(pp->dev))
				return 0; /* alive */
	}
	
	return 1; /* dead */
}

extern int
coalesce_paths (struct vectors * vecs, vector newmp, char * refwwid)
{
	int r = 1;
	int k, i;
	char empty_buff[WWID_SIZE];
	struct multipath * mpp;
	struct path * pp1;
	struct path * pp2;
	vector curmp = vecs->mpvec;
	vector pathvec = vecs->pathvec;

	memset(empty_buff, 0, WWID_SIZE);

	vector_foreach_slot (pathvec, pp1, k) {
		/* skip this path for some reason */

		/* 1. if path has no unique id or wwid blacklisted */
		if (memcmp(empty_buff, pp1->wwid, WWID_SIZE) == 0 ||
		    blacklist(conf->blist, pp1->wwid))
			continue;

		/* 2. if path already coalesced */
		if (pp1->mpp)
			continue;

		/* 3. if path has disappeared */
		if (!pp1->size)
			continue;

		/* 4. path is out of scope */
		if (refwwid && strncmp(pp1->wwid, refwwid, WWID_SIZE))
			continue;

		/*
		 * at this point, we know we really got a new mp
		 */
		if ((mpp = add_map_with_path(vecs, pp1, 0)) == NULL)
			return 1;

		if (pp1->priority < 0)
			mpp->action = ACT_REJECT;

		if (!mpp->paths) {
			condlog(0, "%s coalesce_paths: no paths", mpp->alias);
			remove_map(mpp, vecs, NULL, 0);
			continue;
		}
		
		for (i = k + 1; i < VECTOR_SIZE(pathvec); i++) {
			pp2 = VECTOR_SLOT(pathvec, i);

			if (strcmp(pp1->wwid, pp2->wwid))
				continue;
			
			if (!pp2->size)
				continue;

			if (pp2->size != mpp->size) {
				/*
				 * ouch, avoid feeding that to the DM
				 */
				condlog(0, "%s: size %llu, expected %llu. "
					"Discard", pp2->dev_t, pp2->size,
					mpp->size);
				mpp->action = ACT_REJECT;
			}
			if (pp2->priority < 0)
				mpp->action = ACT_REJECT;
		}
		verify_paths(mpp, vecs, NULL);
		
		if (setup_map(mpp)) {
			remove_map(mpp, vecs, NULL, 0);
			continue;
		}

		if (mpp->action == ACT_UNDEF)
			select_action(mpp, curmp);

		r = domap(mpp);

		if (!r) {
			condlog(3, "%s: domap (%u) failure "
				   "for create/reload map",
				mpp->alias, r);
			remove_map(mpp, vecs, NULL, 0);
			continue;
		}
		else if (r < 0) {
			condlog(3, "%s: domap (%u) failure "
				   "for create/reload map",
				mpp->alias, r);
			return r;
		}

		if (mpp->no_path_retry != NO_PATH_RETRY_UNDEF) {
			if (mpp->no_path_retry == NO_PATH_RETRY_FAIL)
				dm_queue_if_no_path(mpp->alias, 0);
			else
				dm_queue_if_no_path(mpp->alias, 1);
		}

		if (newmp) {
			if (mpp->action != ACT_REJECT) {
				if (!vector_alloc_slot(newmp))
					return 1;
				vector_set_slot(newmp, mpp);
			}
			else
				remove_map(mpp, vecs, NULL, 0);
		}
	}
	/*
	 * Flush maps with only dead paths (ie not in sysfs)
	 * Keep maps with only failed paths
	 */
	if (newmp) {
		vector_foreach_slot (newmp, mpp, i) {
			char alias[WWID_SIZE];
			int j;

			if (!deadmap(mpp))
				continue;

			strncpy(alias, mpp->alias, WWID_SIZE);

			if ((j = find_slot(newmp, (void *)mpp)) != -1)
				vector_del_slot(newmp, j);

			remove_map(mpp, vecs, NULL, 0);

			if (dm_flush_map(mpp->alias, DEFAULT_TARGET))
				condlog(2, "remove: %s (dead) failed!",
					mpp->alias);
			else
				condlog(2, "remove: %s (dead)", mpp->alias);
		}
	}
	return 0;
}

extern char *
get_refwwid (char * dev, int dev_type, vector pathvec)
{
	struct path * pp;
	char buff[FILE_NAME_SIZE];
	char * refwwid;

	if (dev_type == DEV_NONE)
		return NULL;

	if (dev_type == DEV_DEVNODE) {
		basename(dev, buff);
		pp = find_path_by_dev(pathvec, buff);
		
		if (!pp) {
			pp = alloc_path();

			if (!pp)
				return NULL;

			strncpy(pp->dev, buff, FILE_NAME_SIZE);

			if (pathinfo(pp, conf->hwtable, DI_SYSFS | DI_WWID))
				return NULL;

			if (store_path(pathvec, pp)) {
				free_path(pp);
				return NULL;
			}
		}
		refwwid = pp->wwid;
		goto out;
	}

	if (dev_type == DEV_DEVT) {
		pp = find_path_by_devt(pathvec, dev);
		
		if (!pp) {
			if (devt2devname(buff, dev))
				return NULL;

			pp = alloc_path();

			if (!pp)
				return NULL;

			strncpy(pp->dev, buff, FILE_NAME_SIZE);

			if (pathinfo(pp, conf->hwtable, DI_SYSFS | DI_WWID))
				return NULL;
			
			if (store_path(pathvec, pp)) {
				free_path(pp);
				return NULL;
			}
		}
		refwwid = pp->wwid;
		goto out;
	}
	if (dev_type == DEV_DEVMAP) {
		/*
		 * may be a binding
		 */
		refwwid = get_user_friendly_wwid(dev,
						 conf->bindings_file);

		if (refwwid)
			return refwwid;

		/*
		 * or may be an alias
		 */
		refwwid = get_mpe_wwid(dev);

		/*
		 * or directly a wwid
		 */
		if (!refwwid)
			refwwid = dev;
	}
out:
	if (refwwid && strlen(refwwid))
		return STRDUP(refwwid);

	return NULL;
}

