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
#include <libudev.h>
#include <mpath_cmd.h>

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
#include "uxsock.h"
#include "wwids.h"

/* group paths in pg by host adapter
 */
int group_by_host_adapter(struct pathgroup *pgp, vector adapters)
{
	struct adapter_group *agp;
	struct host_group *hgp;
	struct path *pp, *pp1;
	char adapter_name1[SLOT_NAME_SIZE];
	char adapter_name2[SLOT_NAME_SIZE];
	int i, j;
	int found_hostgroup = 0;

	while (VECTOR_SIZE(pgp->paths) > 0) {

		pp = VECTOR_SLOT(pgp->paths, 0);

		if (sysfs_get_host_adapter_name(pp, adapter_name1))
			goto out;
		/* create a new host adapter group
		 */
		agp = alloc_adaptergroup();
		if (!agp)
			goto out;
		agp->pgp = pgp;

		strncpy(agp->adapter_name, adapter_name1, SLOT_NAME_SIZE);
		store_adaptergroup(adapters, agp);

		/* create a new host port group
		 */
		hgp = alloc_hostgroup();
		if (!hgp)
			goto out;
		if (store_hostgroup(agp->host_groups, hgp))
			goto out;

		hgp->host_no = pp->sg_id.host_no;
		agp->num_hosts++;
		if (store_path(hgp->paths, pp))
			goto out;

		hgp->num_paths++;
		/* delete path from path group
		 */
		vector_del_slot(pgp->paths, 0);

		/* add all paths belonging to same host adapter
		 */
		vector_foreach_slot(pgp->paths, pp1, i) {
			if (sysfs_get_host_adapter_name(pp1, adapter_name2))
				goto out;
			if (strcmp(adapter_name1, adapter_name2) == 0) {
				found_hostgroup = 0;
				vector_foreach_slot(agp->host_groups, hgp, j) {
					if (hgp->host_no == pp1->sg_id.host_no) {
						if (store_path(hgp->paths, pp1))
							goto out;
						hgp->num_paths++;
						found_hostgroup = 1;
						break;
					}
				}
				if (!found_hostgroup) {
					/* this path belongs to new host port
					 * within this adapter
					 */
					hgp = alloc_hostgroup();
					if (!hgp)
						goto out;

					if (store_hostgroup(agp->host_groups, hgp))
						goto out;

					agp->num_hosts++;
					if (store_path(hgp->paths, pp1))
						goto out;

					hgp->host_no = pp1->sg_id.host_no;
					hgp->num_paths++;
				}
				/* delete paths from original path_group
				 * as they are added into adapter group now
				 */
				vector_del_slot(pgp->paths, i);
				i--;
			}
		}
	}
	return 0;

out:	/* add back paths into pg as re-ordering failed
	 */
	vector_foreach_slot(adapters, agp, i) {
			vector_foreach_slot(agp->host_groups, hgp, j) {
				while (VECTOR_SIZE(hgp->paths) > 0) {
					pp = VECTOR_SLOT(hgp->paths, 0);
					if (store_path(pgp->paths, pp))
						condlog(3, "failed to restore "
						"path %s into path group",
						 pp->dev);
					vector_del_slot(hgp->paths, 0);
				}
			}
		}
	free_adaptergroup(adapters);
	return 1;
}

/* re-order paths in pg by alternating adapters and host ports
 * for optimized selection
 */
int order_paths_in_pg_by_alt_adapters(struct pathgroup *pgp, vector adapters,
		 int total_paths)
{
	int next_adapter_index = 0;
	struct adapter_group *agp;
	struct host_group *hgp;
	struct path *pp;

	while (total_paths > 0) {
		agp = VECTOR_SLOT(adapters, next_adapter_index);
		if (!agp) {
			condlog(0, "can't get adapter group %d", next_adapter_index);
			return 1;
		}

		hgp = VECTOR_SLOT(agp->host_groups, agp->next_host_index);
		if (!hgp) {
			condlog(0, "can't get host group %d of adapter group %d", next_adapter_index, agp->next_host_index);
			return 1;
		}

		if (!hgp->num_paths) {
			agp->next_host_index++;
			agp->next_host_index %= agp->num_hosts;
			next_adapter_index++;
			next_adapter_index %= VECTOR_SIZE(adapters);
			continue;
		}

		pp  = VECTOR_SLOT(hgp->paths, 0);

		if (store_path(pgp->paths, pp))
			return 1;

		total_paths--;

		vector_del_slot(hgp->paths, 0);

		hgp->num_paths--;

		agp->next_host_index++;
		agp->next_host_index %= agp->num_hosts;
		next_adapter_index++;
		next_adapter_index %= VECTOR_SIZE(adapters);
	}

	/* all paths are added into path_group
	 * in crafted child order
	 */
	return 0;
}

/* round-robin: order paths in path group to alternate
 * between all host adapters
 */
int rr_optimize_path_order(struct pathgroup *pgp)
{
	vector adapters;
	struct path *pp;
	int total_paths;
	int i;

	total_paths = VECTOR_SIZE(pgp->paths);
	vector_foreach_slot(pgp->paths, pp, i) {
		if (pp->sg_id.proto_id != SCSI_PROTOCOL_FCP &&
			pp->sg_id.proto_id != SCSI_PROTOCOL_SAS &&
			pp->sg_id.proto_id != SCSI_PROTOCOL_ISCSI &&
			pp->sg_id.proto_id != SCSI_PROTOCOL_SRP) {
			/* return success as default path order
			 * is maintained in path group
			 */
			return 0;
		}
	}
	adapters = vector_alloc();
	if (!adapters)
		return 0;

	/* group paths in path group by host adapters
	 */
	if (group_by_host_adapter(pgp, adapters)) {
		/* already freed adapters */
		condlog(3, "Failed to group paths by adapters");
		return 0;
	}

	/* re-order paths in pg to alternate between adapters and host ports
	 */
	if (order_paths_in_pg_by_alt_adapters(pgp, adapters, total_paths)) {
		condlog(3, "Failed to re-order paths in pg by adapters "
			"and host ports");
		free_adaptergroup(adapters);
		/* return failure as original paths are
		 * removed form pgp
		 */
		return 1;
	}

	free_adaptergroup(adapters);
	return 0;
}

extern int
setup_map (struct multipath * mpp, char * params, int params_size)
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
	select_mode(mpp);
	select_uid(mpp);
	select_gid(mpp);
	select_fast_io_fail(mpp);
	select_dev_loss(mpp);
	select_reservation_key(mpp);
	select_retain_hwhandler(mpp);
	select_deferred_remove(mpp);
	select_delay_watch_checks(mpp);
	select_delay_wait_checks(mpp);

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

	mpp->nr_active = pathcount(mpp, PATH_UP) + pathcount(mpp, PATH_GHOST);

	/*
	 * ponders each path group and determine highest prio pg
	 * to switch over (default to first)
	 */
	mpp->bestpg = select_path_group(mpp);

	/* re-order paths in all path groups in an optimized way
	 * for round-robin path selectors to get maximum throughput.
	 */
	if (!strncmp(mpp->selector, "round-robin", 11)) {
		vector_foreach_slot(mpp->pg, pgp, i) {
			if (VECTOR_SIZE(pgp->paths) <= 2)
				continue;
			if (rr_optimize_path_order(pgp)) {
				condlog(2, "cannot re-order paths for "
					"optimization: %s",
					mpp->alias);
				return 1;
			}
		}
	}

	/*
	 * transform the mp->pg vector of vectors of paths
	 * into a mp->params strings to feed the device-mapper
	 */
	if (assemble_map(mpp, params, params_size)) {
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
			if (pgp->id == cpgp->id &&
			    !pathcmp(pgp, cpgp)) {
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
	struct multipath * cmpp_by_name;

	cmpp = find_mp_by_wwid(curmp, mpp->wwid);
	cmpp_by_name = find_mp_by_alias(curmp, mpp->alias);

	if (!cmpp_by_name) {
		if (cmpp) {
			condlog(2, "%s: rename %s to %s", mpp->wwid,
				cmpp->alias, mpp->alias);
			strncpy(mpp->alias_old, cmpp->alias, WWID_SIZE);
			mpp->action = ACT_RENAME;
			if (force_reload)
				mpp->action = ACT_RENAME2;
			return;
		}
		mpp->action = ACT_CREATE;
		condlog(3, "%s: set ACT_CREATE (map does not exist)",
			mpp->alias);
		return;
	}

	if (!cmpp) {
		condlog(2, "%s: remove (wwid changed)", mpp->alias);
		dm_flush_map(mpp->alias);
		strncpy(cmpp_by_name->wwid, mpp->wwid, WWID_SIZE);
		drop_multipath(curmp, cmpp_by_name->wwid, KEEP_PATHS);
		mpp->action = ACT_CREATE;
		condlog(3, "%s: set ACT_CREATE (map wwid change)",
			mpp->alias);
		return;
	}

	if (cmpp != cmpp_by_name) {
		condlog(2, "%s: unable to rename %s to %s (%s is used by %s)",
			mpp->wwid, cmpp->alias, mpp->alias,
			mpp->alias, cmpp_by_name->wwid);
		/* reset alias to existing alias */
		FREE(mpp->alias);
		mpp->alias = STRDUP(cmpp->alias);
		mpp->action = ACT_NOTHING;
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
		mpp->action = ACT_RESIZE;
		condlog(3, "%s: set ACT_RESIZE (size change)",
			mpp->alias);
		return;
	}
	if (!mpp->no_path_retry &&
	    (strlen(cmpp->features) != strlen(mpp->features) ||
	     strcmp(cmpp->features, mpp->features))) {
		mpp->action =  ACT_RELOAD;
		condlog(3, "%s: set ACT_RELOAD (features change)",
			mpp->alias);
		return;
	}
	if (mpp->retain_hwhandler != RETAIN_HWHANDLER_ON &&
            (strlen(cmpp->hwhandler) != strlen(mpp->hwhandler) ||
	     strncmp(cmpp->hwhandler, mpp->hwhandler,
		    strlen(mpp->hwhandler)))) {
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
	int x, y;

	if (!mpp || !mpp->pg)
		return 0;

	vector_foreach_slot (mpp->pg, pgp, i) {
		if (!pgp->paths)
			continue;
		vector_foreach_slot(pgp->paths, pp, j) {
			if (lock && flock(pp->fd, LOCK_EX | LOCK_NB) &&
			    errno == EWOULDBLOCK)
				goto fail;
			else if (!lock)
				flock(pp->fd, LOCK_UN);
		}
	}
	return 0;
fail:
	vector_foreach_slot (mpp->pg, pgp, x) {
		if (x > i)
			return 1;
		if (!pgp->paths)
			continue;
		vector_foreach_slot(pgp->paths, pp, y) {
			if (x == i && y >= j)
				return 1;
			flock(pp->fd, LOCK_UN);
		}
	}
	return 1;
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
domap (struct multipath * mpp, char * params)
{
	int r = DOMAP_FAIL;

	/*
	 * last chance to quit before touching the devmaps
	 */
	if (conf->cmd == CMD_DRY_RUN && mpp->action != ACT_NOTHING) {
		print_multipath_topology(mpp, conf->verbosity);
		return DOMAP_DRY;
	}

	if (mpp->action == ACT_CREATE &&
	    dm_map_present(mpp->alias)) {
		condlog(3, "%s: map already present", mpp->alias);
		mpp->action = ACT_RELOAD;
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

		r = dm_addmap_create(mpp, params);

		lock_multipath(mpp, 0);
		break;

	case ACT_RELOAD:
		r = dm_addmap_reload(mpp, params);
		if (r)
			r = dm_simplecmd_noflush(DM_DEVICE_RESUME, mpp->alias,
						 0, MPATH_UDEV_RELOAD_FLAG);
		break;

	case ACT_RESIZE:
		r = dm_addmap_reload(mpp, params);
		if (r)
			r = dm_simplecmd_flush(DM_DEVICE_RESUME, mpp->alias, 1, 0);
		break;

	case ACT_RENAME:
		r = dm_rename(mpp->alias_old, mpp->alias);
		break;

	case ACT_RENAME2:
		r = dm_rename(mpp->alias_old, mpp->alias);
		if (r) {
			r = dm_addmap_reload(mpp, params);
			if (r)
				r = dm_simplecmd_noflush(DM_DEVICE_RESUME, mpp->alias, 0, MPATH_UDEV_RELOAD_FLAG);
		}
		break;

	default:
		break;
	}

	if (r == DOMAP_OK) {
		/*
		 * DM_DEVICE_CREATE, DM_DEVICE_RENAME, or DM_DEVICE_RELOAD
		 * succeeded
		 */
		if (mpp->action == ACT_CREATE)
			remember_wwid(mpp->wwid);
		if (!conf->daemon) {
			/* multipath client mode */
			dm_switchgroup(mpp->alias, mpp->bestpg);
		} else  {
			/* multipath daemon mode */
			mpp->stat_map_loads++;
			condlog(2, "%s: load table [0 %llu %s %s]", mpp->alias,
				mpp->size, TGT_MPATH, params);
			/*
			 * Required action is over, reset for the stateful daemon.
			 * But don't do it for creation as we use in the caller the
			 * mpp->action to figure out whether to start the watievent checker.
			 */
			if (mpp->action != ACT_CREATE)
				mpp->action = ACT_NOTHING;
		}
		dm_setgeometry(mpp);
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

int check_daemon(void)
{
	int fd;
	char *reply;
	int ret = 0;

	fd = mpath_connect();
	if (fd == -1)
		return 0;

	if (send_packet(fd, "show daemon") != 0)
		goto out;
	if (recv_packet(fd, &reply, conf->uxsock_timeout) != 0)
		goto out;

	if (strstr(reply, "shutdown"))
		goto out_free;

	ret = 1;

out_free:
	FREE(reply);
out:
	mpath_disconnect(fd);
	return ret;
}

extern int
coalesce_paths (struct vectors * vecs, vector newmp, char * refwwid, int force_reload)
{
	int r = 1;
	int k, i;
	char params[PARAMS_SIZE];
	struct multipath * mpp;
	struct path * pp1;
	struct path * pp2;
	vector curmp = vecs->mpvec;
	vector pathvec = vecs->pathvec;

	/* ignore refwwid if it's empty */
	if (refwwid && !strlen(refwwid))
		refwwid = NULL;

	if (force_reload) {
		vector_foreach_slot (pathvec, pp1, k) {
			pp1->mpp = NULL;
		}
	}
	vector_foreach_slot (pathvec, pp1, k) {
		/* skip this path for some reason */

		/* 1. if path has no unique id or wwid blacklisted */
		if (strlen(pp1->wwid) == 0 ||
		    filter_path(conf, pp1) > 0) {
			orphan_path(pp1, "wwid blacklisted");
			continue;
		}

		/* 2. if path already coalesced */
		if (pp1->mpp)
			continue;

		/* 3. if path has disappeared */
		if (pp1->state == PATH_REMOVED) {
			orphan_path(pp1, "path removed");
			continue;
		}

		/* 4. path is out of scope */
		if (refwwid && strncmp(pp1->wwid, refwwid, WWID_SIZE))
			continue;

		/* If find_multipaths was selected check if the path is valid */
		if (!refwwid && !should_multipath(pp1, pathvec)) {
			orphan_path(pp1, "only one path");
			continue;
		}

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

			if (!mpp->size && pp2->size)
				mpp->size = pp2->size;

			if (mpp->size && pp2->size &&
			    pp2->size != mpp->size) {
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
		verify_paths(mpp, vecs);

		params[0] = '\0';
		if (setup_map(mpp, params, PARAMS_SIZE)) {
			remove_map(mpp, vecs, 0);
			continue;
		}

		if (mpp->action == ACT_UNDEF)
			select_action(mpp, curmp, force_reload);

		r = domap(mpp, params);

		if (r == DOMAP_FAIL || r == DOMAP_RETRY) {
			condlog(3, "%s: domap (%u) failure "
				   "for create/reload map",
				mpp->alias, r);
			if (r == DOMAP_FAIL) {
				condlog(2, "%s: %s map",
					mpp->alias, (mpp->action == ACT_CREATE)?
					"ignoring" : "removing");
				remove_map(mpp, vecs, 0);
				continue;
			} else /* if (r == DOMAP_RETRY) */
				return r;
		}
		if (r == DOMAP_DRY)
			continue;

		if (!conf->daemon && !conf->allow_queueing && !check_daemon()) {
			if (mpp->no_path_retry != NO_PATH_RETRY_UNDEF &&
			    mpp->no_path_retry != NO_PATH_RETRY_FAIL)
				condlog(3, "%s: multipathd not running, unset "
					"queue_if_no_path feature", mpp->alias);
			if (!dm_queue_if_no_path(mpp->alias, 0))
				remove_feature(&mpp->features,
					       "queue_if_no_path");
		}
		else if (mpp->no_path_retry != NO_PATH_RETRY_UNDEF) {
			if (mpp->no_path_retry == NO_PATH_RETRY_FAIL) {
				condlog(3, "%s: unset queue_if_no_path feature",
					mpp->alias);
				if (!dm_queue_if_no_path(mpp->alias, 0))
					remove_feature(&mpp->features,
						       "queue_if_no_path");
			} else {
				condlog(3, "%s: set queue_if_no_path feature",
					mpp->alias);
				if (!dm_queue_if_no_path(mpp->alias, 1))
					add_feature(&mpp->features,
						    "queue_if_no_path");
			}
		}

		if (!conf->daemon && mpp->action != ACT_NOTHING)
			print_multipath_topology(mpp, conf->verbosity);

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

			if (dm_flush_map(alias))
				condlog(2, "%s: remove failed (dead)",
					alias);
			else
				condlog(2, "%s: remove (dead)", alias);
		}
	}
	return 0;
}

/*
 * returns:
 * 0 - success
 * 1 - failure
 * 2 - blacklist
 */
extern int
get_refwwid (char * dev, enum devtypes dev_type, vector pathvec, char **wwid)
{
	int ret = 1;
	struct path * pp;
	char buff[FILE_NAME_SIZE];
	char * refwwid = NULL, tmpwwid[WWID_SIZE];

	if (!wwid)
		return 1;
	*wwid = NULL;

	if (dev_type == DEV_NONE)
		return 1;

	if (dev_type == DEV_DEVNODE) {
		if (basenamecpy(dev, buff, FILE_NAME_SIZE) == 0) {
			condlog(1, "basename failed for '%s' (%s)",
				dev, buff);
			return 1;
		}

		pp = find_path_by_dev(pathvec, buff);
		if (!pp) {
			struct udev_device *udevice = udev_device_new_from_subsystem_sysname(conf->udev, "block", buff);

			if (!udevice) {
				condlog(2, "%s: can't get udev device", buff);
				return 1;
			}
			ret = store_pathinfo(pathvec, conf->hwtable, udevice,
					     DI_SYSFS | DI_WWID, &pp);
			udev_device_unref(udevice);
			if (!pp) {
				if (ret == 1)
					condlog(0, "%s: can't store path info",
						dev);
				return ret;
			}
		}
		if (pp->udev && filter_property(conf, pp->udev) > 0)
			return 2;

		refwwid = pp->wwid;
		goto out;
	}

	if (dev_type == DEV_DEVT) {
		strchop(dev);
		pp = find_path_by_devt(pathvec, dev);
		if (!pp) {
			struct udev_device *udevice = udev_device_new_from_devnum(conf->udev, 'b', parse_devt(dev));

			if (!udevice) {
				condlog(2, "%s: can't get udev device", dev);
				return 1;
			}
			ret = store_pathinfo(pathvec, conf->hwtable, udevice,
					     DI_SYSFS | DI_WWID, &pp);
			udev_device_unref(udevice);
			if (!pp) {
				if (ret == 1)
					condlog(0, "%s can't store path info",
						buff);
				return ret;
			}
		}
		if (pp->udev && filter_property(conf, pp->udev) > 0)
			return 2;

		refwwid = pp->wwid;
		goto out;
	}

	if (dev_type == DEV_UEVENT) {
		struct udev_device *udevice = udev_device_new_from_environment(conf->udev);

		if (!udevice) {
			condlog(2, "%s: can't get udev device", dev);
			return 1;
		}
		ret = store_pathinfo(pathvec, conf->hwtable, udevice,
				     DI_SYSFS | DI_WWID, &pp);
		udev_device_unref(udevice);
		if (!pp) {
			if (ret == 1)
				condlog(0, "%s: can't store path info",
					dev);
			return ret;
		}
		if (pp->udev && filter_property(conf, pp->udev) > 0)
			return 2;

		refwwid = pp->wwid;
		goto out;
	}

	if (dev_type == DEV_DEVMAP) {

		if (((dm_get_uuid(dev, tmpwwid)) == 0) && (strlen(tmpwwid))) {
			refwwid = tmpwwid;
			goto check;
		}

		/*
		 * may be a binding
		 */
		if (get_user_friendly_wwid(dev, tmpwwid,
					   conf->bindings_file) == 0) {
			refwwid = tmpwwid;
			goto check;
		}

		/*
		 * or may be an alias
		 */
		refwwid = get_mpe_wwid(dev);

		/*
		 * or directly a wwid
		 */
		if (!refwwid)
			refwwid = dev;

check:
		if (refwwid && strlen(refwwid)) {
			if (filter_wwid(conf->blist_wwid, conf->elist_wwid,
					refwwid, NULL) > 0)
			return 2;
		}
	}
out:
	if (refwwid && strlen(refwwid)) {
		*wwid = STRDUP(refwwid);
		return 0;
	}

	return 1;
}

extern int reload_map(struct vectors *vecs, struct multipath *mpp, int refresh)
{
	char params[PARAMS_SIZE] = {0};
	struct path *pp;
	int i, r;

	update_mpp_paths(mpp, vecs->pathvec);
	if (refresh) {
		vector_foreach_slot (mpp->paths, pp, i) {
			r = pathinfo(pp, conf->hwtable, DI_PRIO);
			if (r) {
				condlog(2, "%s: failed to refresh pathinfo",
					mpp->alias);
				return 1;
			}
		}
	}
	if (setup_map(mpp, params, PARAMS_SIZE)) {
		condlog(0, "%s: failed to setup map", mpp->alias);
		return 1;
	}
	select_action(mpp, vecs->mpvec, 1);

	r = domap(mpp, params);
	if (r == DOMAP_FAIL || r == DOMAP_RETRY) {
		condlog(3, "%s: domap (%u) failure "
			"for reload map", mpp->alias, r);
		return 1;
	}
	if (mpp->no_path_retry != NO_PATH_RETRY_UNDEF) {
		if (mpp->no_path_retry == NO_PATH_RETRY_FAIL)
			dm_queue_if_no_path(mpp->alias, 0);
		else
			dm_queue_if_no_path(mpp->alias, 1);
	}

	return 0;
}
