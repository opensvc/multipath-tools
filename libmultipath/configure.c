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
#include <ctype.h>
#include <libdevmapper.h>
#include <libudev.h>
#include "mpath_cmd.h"

#include "checkers.h"
#include "vector.h"
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
#include "dm-generic.h"
#include "print.h"
#include "configure.h"
#include "pgpolicies.h"
#include "dict.h"
#include "alias.h"
#include "prio.h"
#include "util.h"
#include "uxsock.h"
#include "wwids.h"
#include "sysfs.h"
#include "io_err_stat.h"

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

		strlcpy(agp->adapter_name, adapter_name1, SLOT_NAME_SIZE);
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
		if (pp->bus != SYSFS_BUS_SCSI ||
		    (pp->sg_id.proto_id != SCSI_PROTOCOL_FCP &&
		     pp->sg_id.proto_id != SCSI_PROTOCOL_SAS &&
		     pp->sg_id.proto_id != SCSI_PROTOCOL_ISCSI &&
		     pp->sg_id.proto_id != SCSI_PROTOCOL_SRP)) {
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

int setup_map(struct multipath *mpp, char **params, struct vectors *vecs)
{
	struct pathgroup * pgp;
	struct path *pp;
	struct config *conf;
	int i, marginal_pathgroups;
	char *save_attr;

	/*
	 * don't bother if devmap size is unknown
	 */
	if (mpp->size <= 0) {
		condlog(3, "%s: devmap size is unknown", mpp->alias);
		return 1;
	}

	if (mpp->disable_queueing && VECTOR_SIZE(mpp->paths) != 0)
		mpp->disable_queueing = 0;

	/* Force QUEUE_MODE_BIO for maps with nvme:tcp paths */
	vector_foreach_slot(mpp->paths, pp, i) {
		if (pp->bus == SYSFS_BUS_NVME &&
		    pp->sg_id.proto_id == NVME_PROTOCOL_TCP) {
			mpp->queue_mode = QUEUE_MODE_BIO;
			break;
		}
	}
	/*
	 * If this map was created with add_map_without_path(),
	 * mpp->hwe might not be set yet.
	 */
	if (!mpp->hwe)
		extract_hwe_from_path(mpp);

	/*
	 * properties selectors
	 *
	 * Ordering matters for some properties:
	 * - features after no_path_retry and retain_hwhandler
	 * - hwhandler after retain_hwhandler
	 * No guarantee that this list is complete, check code in
	 * propsel.c if in doubt.
	 */
	conf = get_multipath_config();
	pthread_cleanup_push(put_multipath_config, conf);

	select_pgfailback(conf, mpp);
	select_detect_pgpolicy(conf, mpp);
	select_detect_pgpolicy_use_tpg(conf, mpp);
	select_pgpolicy(conf, mpp);

	/*
	 * If setup_map() is called from e.g. from reload_map() or resize_map(),
	 * make sure that we don't corrupt attributes.
	 */
	save_attr = steal_ptr(mpp->selector);
	select_selector(conf, mpp);
	if (!mpp->selector)
		mpp->selector = save_attr;
	else
		free(save_attr);

	select_no_path_retry(conf, mpp);
	select_retain_hwhandler(conf, mpp);

	save_attr = steal_ptr(mpp->features);
	select_features(conf, mpp);
	if (!mpp->features)
		mpp->features = save_attr;
	else
		free(save_attr);

	save_attr = steal_ptr(mpp->hwhandler);
	select_hwhandler(conf, mpp);
	if (!mpp->hwhandler)
		mpp->hwhandler = save_attr;
	else
		free(save_attr);

	select_rr_weight(conf, mpp);
	select_minio(conf, mpp);
	select_mode(conf, mpp);
	select_uid(conf, mpp);
	select_gid(conf, mpp);
	select_reservation_key(conf, mpp);
	select_deferred_remove(conf, mpp);
	select_marginal_path_err_sample_time(conf, mpp);
	select_marginal_path_err_rate_threshold(conf, mpp);
	select_marginal_path_err_recheck_gap_time(conf, mpp);
	select_marginal_path_double_failed_time(conf, mpp);
	select_san_path_err_threshold(conf, mpp);
	select_san_path_err_forget_rate(conf, mpp);
	select_san_path_err_recovery_time(conf, mpp);
	select_delay_checks(conf, mpp);
	select_skip_kpartx(conf, mpp);
	select_max_sectors_kb(conf, mpp);
	select_ghost_delay(conf, mpp);
	select_flush_on_last_del(conf, mpp);

	sysfs_set_scsi_tmo(conf, mpp);
	marginal_pathgroups = conf->marginal_pathgroups;
	mpp->sync_tick = conf->max_checkint;
	pthread_cleanup_pop(1);

	if (!mpp->features || !mpp->hwhandler || !mpp->selector) {
		condlog(0, "%s: map select failed", mpp->alias);
		return 1;
	}

	if (marginal_path_check_enabled(mpp))
		start_io_err_stat_thread(vecs);

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
	if (group_paths(mpp, marginal_pathgroups))
		return 1;

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
	if (assemble_map(mpp, params)) {
		condlog(0, "%s: problem assembling map", mpp->alias);
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

static void cleanup_bitfield(struct bitfield **p)
{
	free(*p);
}

static int
pgcmp (struct multipath * mpp, struct multipath * cmpp)
{
	int i, j;
	struct pathgroup * pgp;
	struct pathgroup * cpgp;
	int r = 0;
	struct bitfield *bf __attribute__((cleanup(cleanup_bitfield))) = NULL;

	if (!mpp)
		return 0;

	if (VECTOR_SIZE(mpp->pg) != VECTOR_SIZE(cmpp->pg))
		return 1;

	bf = alloc_bitfield(VECTOR_SIZE(cmpp->pg));
	if (!bf)
		return 1;

	vector_foreach_slot (mpp->pg, pgp, i) {
		compute_pgid(pgp);

		vector_foreach_slot (cmpp->pg, cpgp, j) {
			if (pgp->id == cpgp->id &&
			    !pathcmp(pgp, cpgp)) {
				set_bit_in_bitfield(j, bf);
				r = 0;
				break;
			}
			r++;
		}
		if (r)
			return r;
	}
	vector_foreach_slot (cmpp->pg, cpgp, j) {
		if (!is_bit_set_in_bitfield(j, bf))
			return 1;
	}
	return r;
}

void trigger_partitions_udev_change(struct udev_device *dev,
				    const char *action, int len)
{
	struct udev_enumerate *part_enum;
	struct udev_list_entry *item;
	const char *devtype;

	part_enum = udev_enumerate_new(udev);
	if (!part_enum)
		return;

	if (udev_enumerate_add_match_parent(part_enum, dev) < 0 ||
	    udev_enumerate_add_match_subsystem(part_enum, "block") < 0 ||
	    udev_enumerate_scan_devices(part_enum) < 0)
		goto unref;

	udev_list_entry_foreach(item,
				udev_enumerate_get_list_entry(part_enum)) {
		const char *syspath;
		struct udev_device *part;

		syspath = udev_list_entry_get_name(item);
		part = udev_device_new_from_syspath(udev, syspath);
		if (!part)
			continue;

		devtype = udev_device_get_devtype(part);
		if (devtype && !strcmp("partition", devtype)) {
			ssize_t ret;

			condlog(4, "%s: triggering %s event for %s", __func__,
				action, syspath);
			ret = sysfs_attr_set_value(part, "uevent", action, len);
			if (ret != len)
				log_sysfs_attr_set_value(2, ret,
					"%s: failed to trigger %s uevent",
					syspath, action);
		}
		udev_device_unref(part);
	}
unref:
	udev_enumerate_unref(part_enum);
}

void
trigger_path_udev_change(struct path *pp, bool is_mpath)
{
	/*
	 * If a path changes from multipath to non-multipath, we must
	 * synthesize an artificial "add" event, otherwise the LVM2 rules
	 * (69-lvm2-lvmetad.rules) won't pick it up. Otherwise, we'd just
	 * irritate ourselves with an "add", so use "change".
	 */
	const char *action = is_mpath ? "change" : "add";
	const char *env;
	ssize_t len, ret;

	if (!pp->udev)
		return;
	/*
	 * Paths that are already classified as multipath
	 * members don't need another uevent.
	 */
	env = udev_device_get_property_value(
		pp->udev, "DM_MULTIPATH_DEVICE_PATH");

	if (is_mpath && env != NULL && !strcmp(env, "1")) {
		/*
		 * If FIND_MULTIPATHS_WAIT_UNTIL is not "0",
		 * path is in "maybe" state and timer is running
		 * Send uevent now (see multipath.rules).
		 */
		env = udev_device_get_property_value(
			pp->udev, "FIND_MULTIPATHS_WAIT_UNTIL");
		if (env == NULL || !strcmp(env, "0"))
			return;
	} else if (!is_mpath &&
		   (env == NULL || !strcmp(env, "0")))
		return;

	condlog(3, "triggering %s uevent for %s (is %smultipath member)",
		action, pp->dev, is_mpath ? "" : "no ");

	len = strlen(action);
	ret = sysfs_attr_set_value(pp->udev, "uevent", action, len);
	if (ret != len)
		log_sysfs_attr_set_value(2, ret,
					 "%s: failed to trigger %s uevent",
					 pp->dev, action);
	trigger_partitions_udev_change(pp->udev, action,
				       strlen(action));
}

void
trigger_paths_udev_change(struct multipath *mpp, bool is_mpath)
{
	struct pathgroup *pgp;
	struct path *pp;
	int i, j;

	if (!mpp || !mpp->pg)
		return;

	vector_foreach_slot (mpp->pg, pgp, i) {
		if (!pgp->paths)
			continue;
		vector_foreach_slot(pgp->paths, pp, j)
			trigger_path_udev_change(pp, is_mpath);
	}
}

static int sysfs_set_max_sectors_kb(struct multipath *mpp)
{
	struct pathgroup * pgp;
	struct path *pp;
	char buff[11];
	ssize_t len;
	int i, j, ret, err = 0;

	if (mpp->max_sectors_kb == MAX_SECTORS_KB_UNDEF)
		return 0;

	len = snprintf(buff, sizeof(buff), "%d", mpp->max_sectors_kb);

	vector_foreach_slot (mpp->pg, pgp, i) {
		vector_foreach_slot(pgp->paths, pp, j) {
			ret = sysfs_attr_set_value(pp->udev,
						   "queue/max_sectors_kb",
						   buff, len);
			if (ret != len) {
				log_sysfs_attr_set_value(1, ret,
					"failed setting max_sectors_kb on %s",
					pp->dev);
				err = 1;
			}
		}
	}
	return err;
}

static bool is_udev_ready(struct multipath *cmpp)
{
	struct udev_device *mpp_ud;
	const char *env;
	bool rc;

	/*
	 * MPATH_DEVICE_READY != 1 can mean two things:
	 *  (a) no usable paths
	 *  (b) device was never fully processed (e.g. udev killed)
	 * If we are in this code path (startup or forced reconfigure),
	 * (b) can mean that upper layers like kpartx have never been
	 * run for this map. Thus force udev reload.
	 */

	mpp_ud = get_udev_for_mpp(cmpp);
	if (!mpp_ud)
		return true;
	env = udev_device_get_property_value(mpp_ud, "MPATH_DEVICE_READY");
	rc = (env != NULL && !strcmp(env, "1"));
	udev_device_unref(mpp_ud);
	condlog(4, "%s: %s: \"%s\" -> %d\n", __func__, cmpp->alias,
		env ? env : "", rc);
	return rc;
}

static void
select_reload_action(struct multipath *mpp, const char *reason)
{
	mpp->action = mpp->action == ACT_RENAME ? ACT_RELOAD_RENAME :
		      ACT_RELOAD;
	condlog(3, "%s: set ACT_RELOAD (%s)", mpp->alias, reason);
}

void select_action (struct multipath *mpp, const struct vector_s *curmp,
		    int force_reload)
{
	struct multipath * cmpp;
	struct multipath * cmpp_by_name;
	char * mpp_feat, * cmpp_feat;

	mpp->action = ACT_NOTHING;
	cmpp = find_mp_by_wwid(curmp, mpp->wwid);
	cmpp_by_name = find_mp_by_alias(curmp, mpp->alias);
	if (mpp->need_reload || (cmpp && cmpp->need_reload))
		force_reload = 1;

	if (!cmpp) {
		if (cmpp_by_name) {
			condlog(1, "%s: can't use alias \"%s\" used by %s, falling back to WWID",
				mpp->wwid, mpp->alias, cmpp_by_name->wwid);
			/* We can do this because wwid wasn't found */
			free(mpp->alias);
			mpp->alias = strdup(mpp->wwid);
		}
		mpp->action = ACT_CREATE;
		condlog(3, "%s: set ACT_CREATE (map does not exist%s)",
			mpp->alias, cmpp_by_name ? ", name changed" : "");
		return;
	}

	if (!cmpp_by_name) {
		condlog(2, "%s: rename %s to %s", mpp->wwid, cmpp->alias,
			mpp->alias);
		strlcpy(mpp->alias_old, cmpp->alias, WWID_SIZE);
		mpp->action = ACT_RENAME;
		/* don't return here. Check for other needed actions */
	} else if (cmpp != cmpp_by_name) {
		condlog(2, "%s: unable to rename %s to %s (%s is used by %s)",
			mpp->wwid, cmpp->alias, mpp->alias,
			mpp->alias, cmpp_by_name->wwid);
		/* reset alias to existing alias */
		free(mpp->alias);
		mpp->alias = strdup(cmpp->alias);
		mpp->action = ACT_IMPOSSIBLE;
		/* don't return here. Check for other needed actions */
	}

	if (cmpp->size != mpp->size) {
		mpp->force_udev_reload = 1;
		mpp->action = mpp->action == ACT_RENAME ? ACT_RESIZE_RENAME :
			      ACT_RESIZE;
		condlog(3, "%s: set ACT_RESIZE (size change)",
			mpp->alias);
		return;
	}

	if (force_reload) {
		mpp->force_udev_reload = 1;
		select_reload_action(mpp, "forced by user");
		return;
	}

	if (!is_udev_ready(cmpp) && count_active_paths(mpp) > 0) {
		mpp->force_udev_reload = 1;
		select_reload_action(mpp, "udev incomplete");
		return;
	}

	if (mpp->no_path_retry != NO_PATH_RETRY_UNDEF &&
	    !!strstr(mpp->features, "queue_if_no_path") !=
	    !!strstr(cmpp->features, "queue_if_no_path")) {
		select_reload_action(mpp, "no_path_retry change");
		return;
	}
	if ((mpp->retain_hwhandler != RETAIN_HWHANDLER_ON ||
	     strcmp(cmpp->hwhandler, "0") == 0) &&
	    (strlen(cmpp->hwhandler) != strlen(mpp->hwhandler) ||
	     strncmp(cmpp->hwhandler, mpp->hwhandler,
		    strlen(mpp->hwhandler)))) {
		select_reload_action(mpp, "hwhandler change");
		return;
	}

	if (mpp->retain_hwhandler != RETAIN_HWHANDLER_UNDEF &&
	    !!strstr(mpp->features, "retain_attached_hw_handler") !=
	    !!strstr(cmpp->features, "retain_attached_hw_handler") &&
	    get_linux_version_code() < KERNEL_VERSION(4, 3, 0)) {
		select_reload_action(mpp, "retain_hwhandler change");
		return;
	}

	cmpp_feat = strdup(cmpp->features);
	mpp_feat = strdup(mpp->features);
	if (cmpp_feat && mpp_feat) {
		remove_feature(&mpp_feat, "queue_if_no_path");
		remove_feature(&mpp_feat, "retain_attached_hw_handler");
		remove_feature(&cmpp_feat, "queue_if_no_path");
		remove_feature(&cmpp_feat, "retain_attached_hw_handler");
		if (strcmp(mpp_feat, cmpp_feat)) {
			select_reload_action(mpp, "features change");
			free(cmpp_feat);
			free(mpp_feat);
			return;
		}
	}
	free(cmpp_feat);
	free(mpp_feat);

	if (!cmpp->selector || strncmp(cmpp->selector, mpp->selector,
		    strlen(mpp->selector))) {
		select_reload_action(mpp, "selector change");
		return;
	}
	if (cmpp->minio != mpp->minio) {
		select_reload_action(mpp, "minio change");
		return;
	}
	if (!cmpp->pg || VECTOR_SIZE(cmpp->pg) != VECTOR_SIZE(mpp->pg)) {
		select_reload_action(mpp, "path group number change");
		return;
	}
	if (pgcmp(mpp, cmpp)) {
		select_reload_action(mpp, "path group topology change");
		return;
	}
	if (cmpp->nextpg != mpp->bestpg) {
		mpp->action = mpp->action == ACT_RENAME ? ACT_SWITCHPG_RENAME :
			      ACT_SWITCHPG;
		condlog(3, "%s: set ACT_SWITCHPG (next path group change)",
			mpp->alias);
		return;
	}
	if (mpp->action == ACT_NOTHING)
		condlog(3, "%s: set ACT_NOTHING (map unchanged)", mpp->alias);
	return;
}

int reinstate_paths(struct multipath *mpp)
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
			if (lock && flock(pp->fd, LOCK_SH | LOCK_NB) &&
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

int domap(struct multipath *mpp, char *params, int is_daemon)
{
	int r = DOMAP_FAIL;
	struct config *conf;

	/*
	 * last chance to quit before touching the devmaps
	 */
	if (mpp->action == ACT_DRY_RUN) {
		print_multipath_topology(mpp, libmp_verbosity);
		return DOMAP_DRY;
	}

	if (mpp->action == ACT_CREATE) {
		char wwid[WWID_SIZE];
		int rc = dm_get_wwid(mpp->alias, wwid, sizeof(wwid));

		if (rc == DMP_OK && !strncmp(mpp->wwid, wwid, sizeof(wwid))) {
			condlog(3, "%s: map already present",
				mpp->alias);
			mpp->action = ACT_RELOAD;
		} else if (rc == DMP_OK) {
			condlog(1, "%s: map \"%s\" already present with WWID \"%s\", skipping\n"
				   "please check alias settings in config and bindings file",
				mpp->wwid, mpp->alias, wwid);
			mpp->action = ACT_REJECT;
		} else if (rc == DMP_NO_MATCH) {
			condlog(1, "%s: alias \"%s\" already taken by a non-multipath map",
				mpp->wwid, mpp->alias);
			mpp->action = ACT_REJECT;
		}
	}
	if (mpp->action == ACT_CREATE) {
		char alias[WWID_SIZE];
		int rc = dm_find_map_by_wwid(mpp->wwid, alias, NULL);

		if (rc == DMP_NO_MATCH) {
			condlog(1, "%s: wwid \"%s\" already in use by non-multipath map \"%s\"",
				mpp->alias, mpp->wwid, alias);
			mpp->action = ACT_REJECT;
		} else if (rc == DMP_OK || rc == DMP_EMPTY) {
			/*
			 * we already handled the case were rc == DMO_OK and
			 * the alias == mpp->alias above. So the alias must be
			 * different here.
			 */
			condlog(3, "%s: map already present with a different name \"%s\". reloading",
				mpp->alias, alias);
			strlcpy(mpp->alias_old, alias, WWID_SIZE);
			mpp->action = ACT_RELOAD_RENAME;
		}
	}
	if (mpp->action == ACT_RENAME || mpp->action == ACT_SWITCHPG_RENAME ||
	    mpp->action == ACT_RELOAD_RENAME ||
	    mpp->action == ACT_RESIZE_RENAME) {
		conf = get_multipath_config();
		pthread_cleanup_push(put_multipath_config, conf);
		r = dm_rename(mpp->alias_old, mpp->alias,
			      conf->partition_delim, mpp->skip_kpartx);
		pthread_cleanup_pop(1);
		if (r == DOMAP_FAIL)
			return r;
	}
	switch (mpp->action) {
	case ACT_REJECT:
	case ACT_NOTHING:
	case ACT_IMPOSSIBLE:
		return DOMAP_EXIST;

	case ACT_SWITCHPG:
	case ACT_SWITCHPG_RENAME:
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

		sysfs_set_max_sectors_kb(mpp);
		if (is_daemon && mpp->ghost_delay > 0 && count_active_paths(mpp) &&
		    pathcount(mpp, PATH_UP) == 0)
			mpp->ghost_delay_tick = mpp->ghost_delay;
		r = dm_addmap_create(mpp, params);

		lock_multipath(mpp, 0);
		break;

	case ACT_RELOAD:
	case ACT_RELOAD_RENAME:
		if (mpp->ghost_delay_tick > 0 && pathcount(mpp, PATH_UP))
			mpp->ghost_delay_tick = 0;
		r = dm_addmap_reload(mpp, params, 0);
		break;

	case ACT_RESIZE:
	case ACT_RESIZE_RENAME:
		if (mpp->ghost_delay_tick > 0 && pathcount(mpp, PATH_UP))
			mpp->ghost_delay_tick = 0;
		r = dm_addmap_reload(mpp, params, 1);
		break;

	case ACT_RENAME:
		break;

	default:
		r = DOMAP_FAIL;
		break;
	}

	if (r == DOMAP_OK) {
		/*
		 * DM_DEVICE_CREATE, DM_DEVICE_RENAME, or DM_DEVICE_RELOAD
		 * succeeded
		 */
		mpp->force_udev_reload = 0;
		if (mpp->action == ACT_CREATE) {
			remember_wwid(mpp->wwid);
			trigger_paths_udev_change(mpp, true);
		}
		if (!is_daemon) {
			/* multipath client mode */
			dm_switchgroup(mpp->alias, mpp->bestpg);
		} else  {
			/* multipath daemon mode */
			mpp->stat_map_loads++;
			condlog(4, "%s: load table [0 %llu %s %s]", mpp->alias,
				mpp->size, TGT_MPATH, params);
			/*
			 * Required action is over, reset for the stateful daemon.
			 * But don't do it for creation as we use in the caller the
			 * mpp->action to figure out whether to start the watievent checker.
			 */
			if (mpp->action != ACT_CREATE)
				mpp->action = ACT_NOTHING;
			else {
				conf = get_multipath_config();
				mpp->wait_for_udev = 1;
				mpp->uev_wait_tick = conf->uev_wait_timeout;
				put_multipath_config(conf);
			}
		}
		dm_setgeometry(mpp);
		return DOMAP_OK;
	} else if (r == DOMAP_FAIL && mpp->action == ACT_CREATE)
		trigger_paths_udev_change(mpp, false);

	return DOMAP_FAIL;
}

extern int
check_daemon(void)
{
	int fd;
	char *reply;
	int ret = 0;
	unsigned int timeout;
	struct config *conf;

	fd = mpath_connect();
	if (fd == -1)
		return 0;

	if (send_packet(fd, "show daemon") != 0)
		goto out;
	conf = get_multipath_config();
	timeout = conf->uxsock_timeout;
	put_multipath_config(conf);
	if (recv_packet(fd, &reply, timeout) != 0)
		goto out;

	if (reply && strstr(reply, "shutdown"))
		goto out_free;

	ret = 1;

out_free:
	free(reply);
out:
	mpath_disconnect(fd);
	return ret;
}

/*
 * The force_reload parameter determines how coalesce_paths treats existing maps.
 * FORCE_RELOAD_NONE: existing maps aren't touched at all
 * FORCE_RELOAD_YES: all maps are rebuilt from scratch and (re)loaded in DM
 * FORCE_RELOAD_WEAK: existing maps are compared to the current conf and only
 * reloaded in DM if there's a difference. This is normally sufficient.
 */
int coalesce_paths (struct vectors *vecs, vector mpvec, char *refwwid,
		    int force_reload, enum mpath_cmds cmd)
{
	int ret = CP_FAIL;
	int k, i, r;
	int is_daemon = (cmd == CMD_NONE) ? 1 : 0;
	char *params __attribute__((cleanup(cleanup_charp))) = NULL;
	struct multipath * mpp;
	struct path * pp1 = NULL;
	struct path * pp2;
	vector curmp = vecs->mpvec;
	vector pathvec = vecs->pathvec;
	vector newmp;
	struct config *conf = NULL;
	int allow_queueing;
	struct bitfield *size_mismatch_seen;
	struct multipath * cmpp;

	/* ignore refwwid if it's empty */
	if (refwwid && !strlen(refwwid))
		refwwid = NULL;

	if (force_reload != FORCE_RELOAD_NONE) {
		vector_foreach_slot (pathvec, pp1, k) {
			pp1->mpp = NULL;
		}
	}

	if (VECTOR_SIZE(pathvec) == 0)
		return CP_OK;
	size_mismatch_seen = alloc_bitfield(VECTOR_SIZE(pathvec));
	if (size_mismatch_seen == NULL)
		return CP_FAIL;

	if (mpvec)
		newmp = mpvec;
	else
		newmp = vector_alloc();
	if (!newmp) {
		condlog(0, "cannot allocate newmp");
		goto out;
	}

	vector_foreach_slot (pathvec, pp1, k) {
		int invalid;

		if (should_exit()) {
			ret = CP_FAIL;
			goto out;
		}

		/* skip this path for some reason */

		/* 1. if path has no unique id or wwid blacklisted */
		if (strlen(pp1->wwid) == 0) {
			orphan_path(pp1, "no WWID");
			continue;
		}

		conf = get_multipath_config();
		pthread_cleanup_push(put_multipath_config, conf);
		invalid = (filter_path(conf, pp1) > 0);
		pthread_cleanup_pop(1);
		if (invalid) {
			orphan_path(pp1, "blacklisted");
			continue;
		}

		/* 2. if path already coalesced, or seen and discarded */
		if (pp1->mpp || is_bit_set_in_bitfield(k, size_mismatch_seen))
			continue;

		/* 3. if path has disappeared */
		if (pp1->state == PATH_REMOVED) {
			orphan_path(pp1, "path removed");
			continue;
		}

		/* 4. path is out of scope */
		if (refwwid && strncmp(pp1->wwid, refwwid, WWID_SIZE - 1))
			continue;

		/* If find_multipaths was selected check if the path is valid */
		if (!refwwid && !should_multipath(pp1, pathvec, curmp)) {
			orphan_path(pp1, "only one path");
			continue;
		}

		cmpp = find_mp_by_wwid(curmp, pp1->wwid);
		if (cmpp && cmpp->queue_mode == QUEUE_MODE_RQ &&
		    pp1->bus == SYSFS_BUS_NVME && pp1->sg_id.proto_id ==
		    NVME_PROTOCOL_TCP) {
			orphan_path(pp1, "nvme:tcp path not allowed with request queue_mode multipath device");
			continue;
		}
		/*
		 * at this point, we know we really got a new mp
		 */
		mpp = add_map_with_path(vecs, pp1, 0, cmpp);
		if (!mpp) {
			orphan_path(pp1, "failed to create multipath device");
			continue;
		}

		if (!mpp->paths) {
			condlog(0, "%s: skip coalesce (no paths)", mpp->alias);
			remove_map(mpp, vecs->pathvec, NULL);
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
					"Discard", pp2->dev, pp2->size,
					mpp->size);
				mpp->action = ACT_REJECT;
				set_bit_in_bitfield(i, size_mismatch_seen);
			}
		}
		verify_paths(mpp);

		if (cmpp)
			mpp->queue_mode = cmpp->queue_mode;
		if (cmd == CMD_DRY_RUN && mpp->action == ACT_UNDEF)
			mpp->action = ACT_DRY_RUN;
		if (setup_map(mpp, &params, vecs)) {
			remove_map(mpp, vecs->pathvec, NULL);
			continue;
		}

		if (mpp->action == ACT_UNDEF)
			select_action(mpp, curmp,
				      force_reload == FORCE_RELOAD_YES ? 1 : 0);

		r = domap(mpp, params, is_daemon);
		free(params);
		params = NULL;

		if (r == DOMAP_FAIL || r == DOMAP_RETRY) {
			condlog(3, "%s: domap (%u) failure "
				   "for create/reload map",
				mpp->alias, r);
			if (r == DOMAP_FAIL || is_daemon) {
				condlog(2, "%s: %s map",
					mpp->alias, (mpp->action == ACT_CREATE)?
					"ignoring" : "removing");
				remove_map(mpp, vecs->pathvec, NULL);
				continue;
			} else /* if (r == DOMAP_RETRY && !is_daemon) */ {
				ret = CP_RETRY;
				goto out;
			}
		}
		if (r == DOMAP_DRY) {
			if (!vector_alloc_slot(newmp)) {
				remove_map(mpp, vecs->pathvec, NULL);
				goto out;
			}
			vector_set_slot(newmp, mpp);
			continue;
		}

		conf = get_multipath_config();
		allow_queueing = conf->allow_queueing;
		put_multipath_config(conf);
		if (!is_daemon && !allow_queueing && !check_daemon()) {
			if (mpp->no_path_retry != NO_PATH_RETRY_UNDEF &&
			    mpp->no_path_retry != NO_PATH_RETRY_FAIL)
				condlog(3, "%s: multipathd not running, unset "
					"queue_if_no_path feature", mpp->alias);
			dm_queue_if_no_path(mpp, 0);
		}

		if (!is_daemon && mpp->action != ACT_NOTHING)
			print_multipath_topology(mpp, libmp_verbosity);

		if (mpp->action != ACT_REJECT) {
			if (!vector_alloc_slot(newmp)) {
				remove_map(mpp, vecs->pathvec, NULL);
				goto out;
			}
			vector_set_slot(newmp, mpp);
		}
		else
			remove_map(mpp, vecs->pathvec, NULL);
	}
	ret = CP_OK;
out:
	free(size_mismatch_seen);
	if (!mpvec) {
		vector_foreach_slot (newmp, mpp, i)
			remove_map(mpp, vecs->pathvec, NULL);
		vector_free(newmp);
	}
	return ret;
}

struct udev_device *get_udev_device(const char *dev, enum devtypes dev_type)
{
	struct udev_device *ud = NULL;
	const char *base;

	if (dev == NULL || *dev == '\0')
		return NULL;

	switch (dev_type) {
	case DEV_DEVNODE:
	case DEV_DEVMAP:
		/* This should be GNU basename, compiler will warn if not */
		base = basename(dev);
		if (*base == '\0')
			break;
		ud = udev_device_new_from_subsystem_sysname(udev, "block",
							    base);
		break;
	case DEV_DEVT:
		ud = udev_device_new_from_devnum(udev, 'b', parse_devt(dev));
		break;
	case DEV_UEVENT:
		ud = udev_device_new_from_environment(udev);
		break;
	default:
		condlog(0, "Internal error: get_udev_device called with invalid type %d\n",
			dev_type);
		break;
	}
	if (ud == NULL)
		condlog(2, "get_udev_device: failed to look up %s with type %d",
			dev, dev_type);
	return ud;
}

static int _get_refwwid(enum mpath_cmds cmd, const char *dev,
			enum devtypes dev_type,
			vector pathvec, struct config *conf, char **wwid)
{
	int ret = 1;
	struct path * pp;
	char buff[FILE_NAME_SIZE];
	const char *refwwid = NULL;
	char tmpwwid[WWID_SIZE];
	struct udev_device *udevice;
	int flags = DI_SYSFS | DI_WWID;

	if (!wwid)
		return PATHINFO_FAILED;
	*wwid = NULL;

	if (dev_type == DEV_NONE)
		return PATHINFO_FAILED;

	if (cmd != CMD_REMOVE_WWID)
		flags |= DI_BLACKLIST;

	switch (dev_type) {
	case DEV_DEVNODE:
		if (basenamecpy(dev, buff, FILE_NAME_SIZE) == 0) {
			condlog(1, "basename failed for '%s' (%s)",
				dev, buff);
			return PATHINFO_FAILED;
		}

		/* dev is used in common code below */
		dev = buff;
		pp = find_path_by_dev(pathvec, dev);
		goto common;

	case DEV_DEVT:
		pp = find_path_by_devt(pathvec, dev);
		goto common;

	case DEV_UEVENT:
		pp = NULL;
		/* For condlog below, dev is unused in get_udev_device() */
		dev = "environment";
	common:
		if (!pp) {
			udevice = get_udev_device(dev, dev_type);

			if (!udevice) {
				condlog(0, "%s: cannot find block device", dev);
				return PATHINFO_FAILED;
			}

			ret = store_pathinfo(pathvec, conf, udevice,
					     flags, &pp);
			udev_device_unref(udevice);
			if (!pp) {
				if (ret == PATHINFO_FAILED)
					condlog(0, "%s: can't store path info",
						dev);
				return ret;
			}
		}
		if (flags & DI_BLACKLIST &&
		    filter_property(conf, pp->udev, 3, pp->uid_attribute) > 0)
			return PATHINFO_SKIPPED;
		refwwid = pp->wwid;
		break;

	case DEV_DEVMAP:
		if (((dm_get_wwid(dev, tmpwwid, WWID_SIZE)) == DMP_OK)
		    && (strlen(tmpwwid)))
			refwwid = tmpwwid;

		/* or may be a binding */
		else if (get_user_friendly_wwid(dev, tmpwwid) == 0)
			refwwid = tmpwwid;

		/* or may be an alias */
		else {
			refwwid = get_mpe_wwid(conf->mptable, dev);

			/* or directly a wwid */
			if (!refwwid)
				refwwid = dev;
		}

		if (flags & DI_BLACKLIST && refwwid && strlen(refwwid) &&
		    filter_wwid(conf->blist_wwid, conf->elist_wwid, refwwid,
				NULL) > 0)
			return PATHINFO_SKIPPED;
		break;
	default:
		break;
	}

	if (refwwid && strlen(refwwid)) {
		*wwid = strdup(refwwid);
		return PATHINFO_OK;
	}

	return PATHINFO_FAILED;
}

/*
 * Returns: PATHINFO_OK, PATHINFO_FAILED, or PATHINFO_SKIPPED (see pathinfo())
 */
int get_refwwid(enum mpath_cmds cmd, const char *dev, enum devtypes dev_type,
		vector pathvec, char **wwid)

{
	int ret;
	struct config *conf = get_multipath_config();

	pthread_cleanup_push(put_multipath_config, conf);
	ret = _get_refwwid(cmd, dev, dev_type, pathvec, conf, wwid);
	pthread_cleanup_pop(1);
	return ret;
}
