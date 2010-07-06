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

#include "checkers.h"
#include "vector.h"
#include "memory.h"
#include "devmapper.h"
#include "defaults.h"
#include "structs.h"
#include "structs_vec.h"
#include "dmparser.h"
#include "config.h"
#include "blacklist.h"
#include "propsel.h"
#include "discovery.h"
#include "debug.h"
#include "switchgroup.h"
#include "print.h"
#include "configure.h"
#include "pgpolicies.h"
#include "dict.h"
#include "alias.h"
#include "prio.h"
#include "util.h"

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
	 * free features, selector, and hwhandler properties if they are being reused
	 */
	free_multipath_attributes(mpp);

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
	select_pg_timeout(mpp);
	select_mode(mpp);
	select_uid(mpp);
	select_gid(mpp);
	select_fast_io_fail(mpp);
	select_dev_loss(mpp);

	sysfs_set_scsi_tmo(mpp);
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

	if (!mpp)
		return 0;

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
select_action (struct multipath * mpp, vector curmp, int force_reload)
{
	struct multipath * cmpp;

	cmpp = find_mp_by_alias(curmp, mpp->alias);

	if (!cmpp) {
		cmpp = find_mp_by_wwid(curmp, mpp->wwid);

		if (cmpp) {
			condlog(2, "%s: rename %s to %s", mpp->wwid,
				cmpp->alias, mpp->alias);
			strncpy(mpp->alias_old, cmpp->alias, WWID_SIZE);
			mpp->action = ACT_RENAME;
			return;
		}
		mpp->action = ACT_CREATE;
		condlog(3, "%s: set ACT_CREATE (map does not exist)",
			mpp->alias);
		return;
	}

	if (!find_mp_by_wwid(curmp, mpp->wwid)) {
		condlog(2, "%s: remove (wwid changed)", cmpp->alias);
		dm_flush_map(mpp->alias);
		strncat(cmpp->wwid, mpp->wwid, WWID_SIZE);
		drop_multipath(curmp, cmpp->wwid, KEEP_PATHS);
		mpp->action = ACT_CREATE;
		condlog(3, "%s: set ACT_CREATE (map wwid change)",
			mpp->alias);
		return;
	}

	if (pathcount(mpp, PATH_UP) == 0) {
		mpp->action = ACT_NOTHING;
		condlog(3, "%s: set ACT_NOTHING (no usable path)",
			mpp->alias);
		return;
	}
	if (force_reload) {
		mpp->action = ACT_RELOAD;
		condlog(3, "%s: set ACT_RELOAD (forced by user)",
			mpp->alias);
		return;
	}
	if (cmpp->size != mpp->size) {
		mpp->action = ACT_RELOAD;
		condlog(3, "%s: set ACT_RELOAD (size change)",
			mpp->alias);
		return;
	}
	if (!mpp->no_path_retry && !mpp->pg_timeout &&
	    (strlen(cmpp->features) != strlen(mpp->features) ||
	     strcmp(cmpp->features, mpp->features))) {
		mpp->action =  ACT_RELOAD;
		condlog(3, "%s: set ACT_RELOAD (features change)",
			mpp->alias);
		return;
	}
	if (!cmpp->selector || strncmp(cmpp->hwhandler, mpp->hwhandler,
		    strlen(mpp->hwhandler))) {
		mpp->action = ACT_RELOAD;
		condlog(3, "%s: set ACT_RELOAD (hwhandler change)",
			mpp->alias);
		return;
	}
	if (!cmpp->selector || strncmp(cmpp->selector, mpp->selector,
		    strlen(mpp->selector))) {
		mpp->action = ACT_RELOAD;
		condlog(3, "%s: set ACT_RELOAD (selector change)",
			mpp->alias);
		return;
	}
	if (cmpp->minio != mpp->minio) {
		mpp->action = ACT_RELOAD;
		condlog(3, "%s: set ACT_RELOAD (minio change, %u->%u)",
			mpp->alias, cmpp->minio, mpp->minio);
		return;
	}
	if (!cmpp->pg || VECTOR_SIZE(cmpp->pg) != VECTOR_SIZE(mpp->pg)) {
		mpp->action = ACT_RELOAD;
		condlog(3, "%s: set ACT_RELOAD (path group number change)",
			mpp->alias);
		return;
	}
	if (pgcmp(mpp, cmpp)) {
		mpp->action = ACT_RELOAD;
		condlog(3, "%s: set ACT_RELOAD (path group topology change)",
			mpp->alias);
		return;
	}
	if (cmpp->nextpg != mpp->bestpg) {
		mpp->action = ACT_SWITCHPG;
		condlog(3, "%s: set ACT_SWITCHPG (next path group change)",
			mpp->alias);
		return;
	}
	mpp->action = ACT_NOTHING;
	condlog(3, "%s: set ACT_NOTHING (map unchanged)",
		mpp->alias);
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
					condlog(0, "%s: error reinstating",
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
 */
#define DOMAP_RETRY	-1
#define DOMAP_FAIL	0
#define DOMAP_OK	1
#define DOMAP_EXIST	2
#define DOMAP_DRY	3

extern int
domap (struct multipath * mpp)
{
	int r = 0;

	/*
	 * last chance to quit before touching the devmaps
	 */
	if (conf->dry_run && mpp->action != ACT_NOTHING) {
		print_multipath_topology(mpp, conf->verbosity);
		return DOMAP_DRY;
	}

	switch (mpp->action) {
	case ACT_REJECT:
	case ACT_NOTHING:
		return DOMAP_EXIST;

	case ACT_SWITCHPG:
		dm_switchgroup(mpp->alias, mpp->bestpg);
		/*
		 * we may have avoided reinstating paths because there where in
		 * active or disabled PG. Now that the topology has changed,
		 * retry.
		 */
		reinstate_paths(mpp);
		return DOMAP_EXIST;

	case ACT_CREATE:
		if (lock_multipath(mpp, 1)) {
			condlog(3, "%s: failed to create map (in use)",
				mpp->alias);
			return DOMAP_RETRY;
		}

		if (dm_map_present(mpp->alias)) {
			condlog(3, "%s: map already present", mpp->alias);
			lock_multipath(mpp, 0);
			break;
		}

		r = dm_addmap_create(mpp);

		if (!r)
			 r = dm_addmap_create_ro(mpp);

		lock_multipath(mpp, 0);
		break;

	case ACT_RELOAD:
		r = dm_addmap_reload(mpp);
		if (!r)
			r = dm_addmap_reload_ro(mpp);
		if (r)
			r = dm_simplecmd_noflush(DM_DEVICE_RESUME, mpp->alias);
		break;

 	case ACT_RESIZE:
  		r = dm_addmap_reload(mpp);
  		if (!r)
  			r = dm_addmap_reload_ro(mpp);
  		if (r)
  			r = dm_simplecmd_flush(DM_DEVICE_RESUME, mpp->alias, 1);
		break;

	case ACT_RENAME:
		r = dm_rename(mpp->alias_old, mpp->alias);
		break;

	default:
		break;
	}

	if (r) {
		/*
		 * DM_DEVICE_CREATE, DM_DEVICE_RENAME, or DM_DEVICE_RELOAD
		 * succeeded
		 */
		if (!conf->daemon) {
			/* multipath client mode */
			dm_switchgroup(mpp->alias, mpp->bestpg);
			if (mpp->action != ACT_NOTHING)
				print_multipath_topology(mpp, conf->verbosity);
		} else  {
			/* multipath daemon mode */
			mpp->stat_map_loads++;
			condlog(2, "%s: load table [0 %llu %s %s]", mpp->alias,
				mpp->size, TGT_MPATH, mpp->params);
			/*
			 * Required action is over, reset for the stateful daemon.
			 * But don't do it for creation as we use in the caller the
			 * mpp->action to figure out whether to start the watievent checker.
			 */
			if (mpp->action != ACT_CREATE)
				mpp->action = ACT_NOTHING;
		}
		return DOMAP_OK;
	}
	return DOMAP_FAIL;
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
coalesce_paths (struct vectors * vecs, vector newmp, char * refwwid, int force_reload)
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

	if (force_reload) {
		vector_foreach_slot (pathvec, pp1, k) {
			pp1->mpp = NULL;
		}
	}
	vector_foreach_slot (pathvec, pp1, k) {
		/* skip this path for some reason */

		/* 1. if path has no unique id or wwid blacklisted */
		if (memcmp(empty_buff, pp1->wwid, WWID_SIZE) == 0 ||
		    filter_path(conf, pp1) > 0)
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
		mpp = add_map_with_path(vecs, pp1, 0);
		if (!mpp)
			return 1;

		if (pp1->priority == PRIO_UNDEF)
			mpp->action = ACT_REJECT;

		if (!mpp->paths) {
			condlog(0, "%s: skip coalesce (no paths)", mpp->alias);
			remove_map(mpp, vecs, 0);
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
			if (pp2->priority == PRIO_UNDEF)
				mpp->action = ACT_REJECT;
		}
		verify_paths(mpp, vecs, NULL);
		
		if (setup_map(mpp)) {
			remove_map(mpp, vecs, 0);
			continue;
		}

		if (mpp->action == ACT_UNDEF)
			select_action(mpp, curmp, force_reload);

		r = domap(mpp);

		if (r == DOMAP_FAIL || r == DOMAP_RETRY) {
			condlog(3, "%s: domap (%u) failure "
				   "for create/reload map",
				mpp->alias, r);
			if (r == DOMAP_FAIL) {
				remove_map(mpp, vecs, 0);
				continue;
			} else /* if (r == DOMAP_RETRY) */
				return r;
		}
		if (r == DOMAP_DRY)
			continue;

		if (mpp->no_path_retry != NO_PATH_RETRY_UNDEF) {
			if (mpp->no_path_retry == NO_PATH_RETRY_FAIL)
				dm_queue_if_no_path(mpp->alias, 0);
			else
				dm_queue_if_no_path(mpp->alias, 1);
		}
		if (mpp->pg_timeout != PGTIMEOUT_UNDEF) {
			if (mpp->pg_timeout == -PGTIMEOUT_NONE)
				dm_set_pg_timeout(mpp->alias,  0);
			else
				dm_set_pg_timeout(mpp->alias, mpp->pg_timeout);
		}

		if (newmp) {
			if (mpp->action != ACT_REJECT) {
				if (!vector_alloc_slot(newmp))
					return 1;
				vector_set_slot(newmp, mpp);
			}
			else
				remove_map(mpp, vecs, 0);
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

			remove_map(mpp, vecs, 0);

			if (dm_flush_map(mpp->alias))
				condlog(2, "%s: remove failed (dead)",
					mpp->alias);
			else
				condlog(2, "%s: remove (dead)", mpp->alias);
		}
	}
	return 0;
}

extern char *
get_refwwid (char * dev, enum devtypes dev_type, vector pathvec)
{
	struct path * pp;
	char buff[FILE_NAME_SIZE];
	char * refwwid = NULL, tmpwwid[WWID_SIZE];

	if (dev_type == DEV_NONE)
		return NULL;

	if (dev_type == DEV_DEVNODE) {
		basenamecpy(dev, buff);
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

		if (((dm_get_uuid(dev, tmpwwid)) == 0) && (strlen(tmpwwid))) {
			refwwid = tmpwwid;
			goto out;
		}

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

