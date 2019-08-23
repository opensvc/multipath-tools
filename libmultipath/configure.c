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

/* Time in ms to wait for pending checkers in setup_map() */
#define WAIT_CHECKERS_PENDING_MS 10
#define WAIT_ALL_CHECKERS_PENDING_MS 90

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

static int wait_for_pending_paths(struct multipath *mpp,
				  struct config *conf,
				  int n_pending, int goal, int wait_ms)
{
	static const struct timespec millisec =
		{ .tv_sec = 0, .tv_nsec = 1000*1000 };
	int i, j;
	struct path *pp;
	struct pathgroup *pgp;
	struct timespec ts;

	do {
		vector_foreach_slot(mpp->pg, pgp, i) {
			vector_foreach_slot(pgp->paths, pp, j) {
				if (pp->state != PATH_PENDING)
					continue;
				pp->state = get_state(pp, conf,
						      0, PATH_PENDING);
				if (pp->state != PATH_PENDING &&
				    --n_pending <= goal)
					return 0;
			}
		}
		ts = millisec;
		while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
			/* nothing */;
	} while (--wait_ms > 0);

	return n_pending;
}

int setup_map(struct multipath *mpp, char *params, int params_size,
	      struct vectors *vecs)
{
	struct pathgroup * pgp;
	struct config *conf;
	int i, n_paths, marginal_pathgroups;

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
	if (mpp->disable_queueing && VECTOR_SIZE(mpp->paths) != 0)
		mpp->disable_queueing = 0;

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
	select_pgpolicy(conf, mpp);
	select_selector(conf, mpp);
	select_no_path_retry(conf, mpp);
	select_retain_hwhandler(conf, mpp);
	select_features(conf, mpp);
	select_hwhandler(conf, mpp);
	select_rr_weight(conf, mpp);
	select_minio(conf, mpp);
	select_mode(conf, mpp);
	select_uid(conf, mpp);
	select_gid(conf, mpp);
	select_fast_io_fail(conf, mpp);
	select_dev_loss(conf, mpp);
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

	sysfs_set_scsi_tmo(mpp, conf->checkint);
	marginal_pathgroups = conf->marginal_pathgroups;
	pthread_cleanup_pop(1);

	if (marginal_path_check_enabled(mpp))
		start_io_err_stat_thread(vecs);

	n_paths = VECTOR_SIZE(mpp->paths);
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
	 * If async state detection is used, see if pending state checks
	 * have finished, to get nr_active right. We can't wait until the
	 * checkers time out, as that may take 30s or more, and we are
	 * holding the vecs lock.
	 */
	if (conf->force_sync == 0 && n_paths > 0) {
		int n_pending = pathcount(mpp, PATH_PENDING);

		if (n_pending > 0)
			n_pending = wait_for_pending_paths(
				mpp, conf, n_pending, 0,
				WAIT_CHECKERS_PENDING_MS);
		/* ALL paths pending - wait some more, but be satisfied
		   with only some paths finished */
		if (n_pending == n_paths)
			n_pending = wait_for_pending_paths(
				mpp, conf, n_pending,
				n_paths >= 4 ? 2 : 1,
				WAIT_ALL_CHECKERS_PENDING_MS);
		if (n_pending > 0)
			condlog(2, "%s: setting up map with %d/%d path checkers pending",
				mpp->alias, n_pending, n_paths);
	}
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

static struct udev_device *
get_udev_for_mpp(const struct multipath *mpp)
{
	dev_t devnum;
	struct udev_device *udd;

	if (!mpp || !mpp->dmi) {
		condlog(1, "%s called with empty mpp", __func__);
		return NULL;
	}

	devnum = makedev(mpp->dmi->major, mpp->dmi->minor);
	udd = udev_device_new_from_devnum(udev, 'b', devnum);
	if (!udd) {
		condlog(1, "failed to get udev device for %s", mpp->alias);
		return NULL;
	}
	return udd;
}

static void
trigger_udev_change(const struct multipath *mpp)
{
	static const char change[] = "change";
	struct udev_device *udd = get_udev_for_mpp(mpp);
	if (!udd)
		return;
	condlog(3, "triggering %s uevent for %s", change, mpp->alias);
	sysfs_attr_set_value(udd, "uevent", change, sizeof(change)-1);
	udev_device_unref(udd);
}

static void trigger_partitions_udev_change(struct udev_device *dev,
					   const char *action, int len)
{
	struct udev_enumerate *part_enum;
	struct udev_list_entry *item;

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

		if (!strcmp("partition", udev_device_get_devtype(part))) {
			condlog(4, "%s: triggering %s event for %s", __func__,
				action, syspath);
			sysfs_attr_set_value(part, "uevent", action, len);
		}
		udev_device_unref(part);
	}
unref:
	udev_enumerate_unref(part_enum);
}

void
trigger_paths_udev_change(struct multipath *mpp, bool is_mpath)
{
	struct pathgroup *pgp;
	struct path *pp;
	int i, j;
	/*
	 * If a path changes from multipath to non-multipath, we must
	 * synthesize an artificial "add" event, otherwise the LVM2 rules
	 * (69-lvm2-lvmetad.rules) won't pick it up. Otherwise, we'd just
	 * irritate ourselves with an "add", so use "change".
	 */
	const char *action = is_mpath ? "change" : "add";

	if (!mpp || !mpp->pg)
		return;

	vector_foreach_slot (mpp->pg, pgp, i) {
		if (!pgp->paths)
			continue;
		vector_foreach_slot(pgp->paths, pp, j) {
			const char *env;

			if (!pp->udev)
				continue;
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
					continue;
			} else if (!is_mpath &&
				   (env == NULL || !strcmp(env, "0")))
				continue;

			condlog(3, "triggering %s uevent for %s (is %smultipath member)",
				action, pp->dev, is_mpath ? "" : "no ");
			sysfs_attr_set_value(pp->udev, "uevent",
					     action, strlen(action));
			trigger_partitions_udev_change(pp->udev, action,
						       strlen(action));
		}
	}

	mpp->needs_paths_uevent = 0;
}

static int
is_mpp_known_to_udev(const struct multipath *mpp)
{
	struct udev_device *udd = get_udev_for_mpp(mpp);
	int ret = (udd != NULL);
	udev_device_unref(udd);
	return ret;
}

static int
sysfs_set_max_sectors_kb(struct multipath *mpp, int is_reload)
{
	struct pathgroup * pgp;
	struct path *pp;
	char buff[11];
	int i, j, ret, err = 0;
	struct udev_device *udd;
	int max_sectors_kb;

	if (mpp->max_sectors_kb == MAX_SECTORS_KB_UNDEF)
		return 0;
	max_sectors_kb = mpp->max_sectors_kb;
	if (is_reload) {
		if (!mpp->dmi && dm_get_info(mpp->alias, &mpp->dmi) != 0) {
			condlog(1, "failed to get dm info for %s", mpp->alias);
			return 1;
		}
		udd = get_udev_for_mpp(mpp);
		if (!udd) {
			condlog(1, "failed to get udev device to set max_sectors_kb for %s", mpp->alias);
			return 1;
		}
		ret = sysfs_attr_get_value(udd, "queue/max_sectors_kb", buff,
					   sizeof(buff));
		udev_device_unref(udd);
		if (ret <= 0) {
			condlog(1, "failed to get current max_sectors_kb from %s", mpp->alias);
			return 1;
		}
		if (sscanf(buff, "%u\n", &max_sectors_kb) != 1) {
			condlog(1, "can't parse current max_sectors_kb from %s",
				mpp->alias);
			return 1;
		}
	}
	snprintf(buff, 11, "%d", max_sectors_kb);

	vector_foreach_slot (mpp->pg, pgp, i) {
		vector_foreach_slot(pgp->paths, pp, j) {
			ret = sysfs_attr_set_value(pp->udev,
						   "queue/max_sectors_kb",
						   buff, strlen(buff));
			if (ret < 0) {
				condlog(1, "failed setting max_sectors_kb on %s : %s", pp->dev, strerror(-ret));
				err = 1;
			}
		}
	}
	return err;
}

static void
select_action (struct multipath * mpp, vector curmp, int force_reload)
{
	struct multipath * cmpp;
	struct multipath * cmpp_by_name;
	char * mpp_feat, * cmpp_feat;

	cmpp = find_mp_by_wwid(curmp, mpp->wwid);
	cmpp_by_name = find_mp_by_alias(curmp, mpp->alias);

	if (!cmpp_by_name) {
		if (cmpp) {
			condlog(2, "%s: rename %s to %s", mpp->wwid,
				cmpp->alias, mpp->alias);
			strlcpy(mpp->alias_old, cmpp->alias, WWID_SIZE);
			mpp->action = ACT_RENAME;
			if (force_reload) {
				mpp->force_udev_reload = 1;
				mpp->action = ACT_FORCERENAME;
			}
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
		strlcpy(cmpp_by_name->wwid, mpp->wwid, WWID_SIZE);
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
		mpp->action = ACT_IMPOSSIBLE;
		return;
	}

	if (pathcount(mpp, PATH_UP) == 0) {
		mpp->action = ACT_IMPOSSIBLE;
		condlog(3, "%s: set ACT_IMPOSSIBLE (no usable path)",
			mpp->alias);
		return;
	}
	if (force_reload) {
		mpp->force_udev_reload = 1;
		mpp->action = ACT_RELOAD;
		condlog(3, "%s: set ACT_RELOAD (forced by user)",
			mpp->alias);
		return;
	}
	if (cmpp->size != mpp->size) {
		mpp->force_udev_reload = 1;
		mpp->action = ACT_RESIZE;
		condlog(3, "%s: set ACT_RESIZE (size change)",
			mpp->alias);
		return;
	}

	if (mpp->no_path_retry != NO_PATH_RETRY_UNDEF &&
	    !!strstr(mpp->features, "queue_if_no_path") !=
	    !!strstr(cmpp->features, "queue_if_no_path")) {
		mpp->action =  ACT_RELOAD;
		condlog(3, "%s: set ACT_RELOAD (no_path_retry change)",
			mpp->alias);
		return;
	}
	if ((mpp->retain_hwhandler != RETAIN_HWHANDLER_ON ||
	     strcmp(cmpp->hwhandler, "0") == 0) &&
	    (strlen(cmpp->hwhandler) != strlen(mpp->hwhandler) ||
	     strncmp(cmpp->hwhandler, mpp->hwhandler,
		    strlen(mpp->hwhandler)))) {
		mpp->action = ACT_RELOAD;
		condlog(3, "%s: set ACT_RELOAD (hwhandler change)",
			mpp->alias);
		return;
	}

	if (mpp->retain_hwhandler != RETAIN_HWHANDLER_UNDEF &&
	    !!strstr(mpp->features, "retain_attached_hw_handler") !=
	    !!strstr(cmpp->features, "retain_attached_hw_handler") &&
	    get_linux_version_code() < KERNEL_VERSION(4, 3, 0)) {
		mpp->action = ACT_RELOAD;
		condlog(3, "%s: set ACT_RELOAD (retain_hwhandler change)",
			mpp->alias);
		return;
	}

	cmpp_feat = STRDUP(cmpp->features);
	mpp_feat = STRDUP(mpp->features);
	if (cmpp_feat && mpp_feat) {
		remove_feature(&mpp_feat, "queue_if_no_path");
		remove_feature(&mpp_feat, "retain_attached_hw_handler");
		remove_feature(&cmpp_feat, "queue_if_no_path");
		remove_feature(&cmpp_feat, "retain_attached_hw_handler");
		if (strncmp(mpp_feat, cmpp_feat, PARAMS_SIZE)) {
			mpp->action =  ACT_RELOAD;
			condlog(3, "%s: set ACT_RELOAD (features change)",
				mpp->alias);
		}
	}
	FREE(cmpp_feat);
	FREE(mpp_feat);

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
	if (!is_mpp_known_to_udev(cmpp)) {
		mpp->action = ACT_RELOAD;
		condlog(3, "%s: set ACT_RELOAD (udev device not initialized)",
			mpp->alias);
		return;
	}
	mpp->action = ACT_NOTHING;
	condlog(3, "%s: set ACT_NOTHING (map unchanged)",
		mpp->alias);
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
	int verbosity;

	/*
	 * last chance to quit before touching the devmaps
	 */
	if (mpp->action == ACT_DRY_RUN) {
		conf = get_multipath_config();
		verbosity = conf->verbosity;
		put_multipath_config(conf);
		print_multipath_topology(mpp, verbosity);
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
	case ACT_IMPOSSIBLE:
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

		sysfs_set_max_sectors_kb(mpp, 0);
		if (is_daemon && mpp->ghost_delay > 0 && mpp->nr_active &&
		    pathcount(mpp, PATH_GHOST) == mpp->nr_active)
			mpp->ghost_delay_tick = mpp->ghost_delay;
		r = dm_addmap_create(mpp, params);

		lock_multipath(mpp, 0);
		break;

	case ACT_RELOAD:
		sysfs_set_max_sectors_kb(mpp, 1);
		if (mpp->ghost_delay_tick > 0 && pathcount(mpp, PATH_UP))
			mpp->ghost_delay_tick = 0;
		r = dm_addmap_reload(mpp, params, 0);
		break;

	case ACT_RESIZE:
		sysfs_set_max_sectors_kb(mpp, 1);
		if (mpp->ghost_delay_tick > 0 && pathcount(mpp, PATH_UP))
			mpp->ghost_delay_tick = 0;
		r = dm_addmap_reload(mpp, params, 1);
		break;

	case ACT_RENAME:
		conf = get_multipath_config();
		pthread_cleanup_push(put_multipath_config, conf);
		r = dm_rename(mpp->alias_old, mpp->alias,
			      conf->partition_delim, mpp->skip_kpartx);
		pthread_cleanup_pop(1);
		break;

	case ACT_FORCERENAME:
		conf = get_multipath_config();
		pthread_cleanup_push(put_multipath_config, conf);
		r = dm_rename(mpp->alias_old, mpp->alias,
			      conf->partition_delim, mpp->skip_kpartx);
		pthread_cleanup_pop(1);
		if (r) {
			sysfs_set_max_sectors_kb(mpp, 1);
			if (mpp->ghost_delay_tick > 0 &&
			    pathcount(mpp, PATH_UP))
				mpp->ghost_delay_tick = 0;
			r = dm_addmap_reload(mpp, params, 0);
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
		mpp->force_udev_reload = 0;
		if (mpp->action == ACT_CREATE &&
		    (remember_wwid(mpp->wwid) == 1 ||
		     mpp->needs_paths_uevent))
			trigger_paths_udev_change(mpp, true);
		if (!is_daemon) {
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
			else {
				conf = get_multipath_config();
				mpp->wait_for_udev = 1;
				mpp->uev_wait_tick = conf->uev_wait_timeout;
				put_multipath_config(conf);
			}
		}
		dm_setgeometry(mpp);
		return DOMAP_OK;
	} else if (r == DOMAP_FAIL && mpp->action == ACT_CREATE &&
		   mpp->needs_paths_uevent)
		trigger_paths_udev_change(mpp, false);

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
	FREE(reply);
out:
	mpath_disconnect(fd);
	return ret;
}

/*
 * The force_reload parameter determines how coalesce_paths treats existing maps.
 * FORCE_RELOAD_NONE: existing maps aren't touched at all
 * FORCE_RELOAD_YES: all maps are rebuilt from scratch and (re)loaded in DM
 * FORCE_RELOAD_WEAK: existing maps are compared to the current conf and only
 * reloaded in DM if there's a difference. This is useful during startup.
 */
int coalesce_paths (struct vectors * vecs, vector newmp, char * refwwid,
		    int force_reload, enum mpath_cmds cmd)
{
	int ret = CP_FAIL;
	int k, i, r;
	int is_daemon = (cmd == CMD_NONE) ? 1 : 0;
	char params[PARAMS_SIZE];
	struct multipath * mpp;
	struct path * pp1;
	struct path * pp2;
	vector curmp = vecs->mpvec;
	vector pathvec = vecs->pathvec;
	struct config *conf;
	int allow_queueing;
	uint64_t *size_mismatch_seen;

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
	size_mismatch_seen = calloc((VECTOR_SIZE(pathvec) - 1) / 64 + 1,
				    sizeof(uint64_t));
	if (size_mismatch_seen == NULL)
		return CP_FAIL;

	vector_foreach_slot (pathvec, pp1, k) {
		int invalid;
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
		if (pp1->mpp || is_bit_set_in_array(k, size_mismatch_seen))
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

		/*
		 * at this point, we know we really got a new mp
		 */
		mpp = add_map_with_path(vecs, pp1, 0);
		if (!mpp) {
			orphan_path(pp1, "failed to create multipath device");
			continue;
		}

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
					"Discard", pp2->dev, pp2->size,
					mpp->size);
				mpp->action = ACT_REJECT;
				set_bit_in_array(i, size_mismatch_seen);
			}
		}
		verify_paths(mpp, vecs);

		params[0] = '\0';
		if (setup_map(mpp, params, PARAMS_SIZE, vecs)) {
			remove_map(mpp, vecs, 0);
			continue;
		}

		if (cmd == CMD_DRY_RUN)
			mpp->action = ACT_DRY_RUN;
		if (mpp->action == ACT_UNDEF)
			select_action(mpp, curmp,
				      force_reload == FORCE_RELOAD_YES ? 1 : 0);

		r = domap(mpp, params, is_daemon);

		if (r == DOMAP_FAIL || r == DOMAP_RETRY) {
			condlog(3, "%s: domap (%u) failure "
				   "for create/reload map",
				mpp->alias, r);
			if (r == DOMAP_FAIL || is_daemon) {
				condlog(2, "%s: %s map",
					mpp->alias, (mpp->action == ACT_CREATE)?
					"ignoring" : "removing");
				remove_map(mpp, vecs, 0);
				continue;
			} else /* if (r == DOMAP_RETRY && !is_daemon) */ {
				ret = CP_RETRY;
				goto out;
			}
		}
		if (r == DOMAP_DRY)
			continue;

		if (r == DOMAP_EXIST && mpp->action == ACT_NOTHING &&
		    force_reload == FORCE_RELOAD_WEAK)
			/*
			 * First time we're called, and no changes applied.
			 * domap() was a noop. But we can't be sure that
			 * udev has already finished setting up this device
			 * (udev in initrd may have been shut down while
			 * processing this device or its children).
			 * Trigger a change event, just in case.
			 */
			trigger_udev_change(find_mp_by_wwid(curmp, mpp->wwid));

		conf = get_multipath_config();
		allow_queueing = conf->allow_queueing;
		put_multipath_config(conf);
		if (!is_daemon && !allow_queueing && !check_daemon()) {
			if (mpp->no_path_retry != NO_PATH_RETRY_UNDEF &&
			    mpp->no_path_retry != NO_PATH_RETRY_FAIL)
				condlog(3, "%s: multipathd not running, unset "
					"queue_if_no_path feature", mpp->alias);
			if (!dm_queue_if_no_path(mpp->alias, 0))
				remove_feature(&mpp->features,
					       "queue_if_no_path");
		}

		if (!is_daemon && mpp->action != ACT_NOTHING) {
			int verbosity;

			conf = get_multipath_config();
			verbosity = conf->verbosity;
			put_multipath_config(conf);
			print_multipath_topology(mpp, verbosity);
		}

		if (newmp) {
			if (mpp->action != ACT_REJECT) {
				if (!vector_alloc_slot(newmp))
					goto out;
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

			if (!deadmap(mpp))
				continue;

			strlcpy(alias, mpp->alias, WWID_SIZE);

			vector_del_slot(newmp, i);
			i--;
			remove_map(mpp, vecs, 0);

			if (dm_flush_map(alias))
				condlog(2, "%s: remove failed (dead)",
					alias);
			else
				condlog(2, "%s: remove (dead)", alias);
		}
	}
	ret = CP_OK;
out:
	free(size_mismatch_seen);
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

/*
 * returns:
 * 0 - success
 * 1 - failure
 * 2 - blacklist
 */
int get_refwwid(enum mpath_cmds cmd, char *dev, enum devtypes dev_type,
		vector pathvec, char **wwid)
{
	int ret = 1;
	struct path * pp;
	char buff[FILE_NAME_SIZE];
	char * refwwid = NULL, tmpwwid[WWID_SIZE];
	int flags = DI_SYSFS | DI_WWID;
	struct config *conf;
	int invalid = 0;

	if (!wwid)
		return 1;
	*wwid = NULL;

	if (dev_type == DEV_NONE)
		return 1;

	if (cmd != CMD_REMOVE_WWID)
		flags |= DI_BLACKLIST;

	if (dev_type == DEV_DEVNODE) {
		if (basenamecpy(dev, buff, FILE_NAME_SIZE) == 0) {
			condlog(1, "basename failed for '%s' (%s)",
				dev, buff);
			return 1;
		}

		pp = find_path_by_dev(pathvec, buff);
		if (!pp) {
			struct udev_device *udevice =
				get_udev_device(buff, dev_type);

			if (!udevice)
				return 1;

			conf = get_multipath_config();
			pthread_cleanup_push(put_multipath_config, conf);
			ret = store_pathinfo(pathvec, conf, udevice,
					     flags, &pp);
			pthread_cleanup_pop(1);
			udev_device_unref(udevice);
			if (!pp) {
				if (ret == 1)
					condlog(0, "%s: can't store path info",
						dev);
				return ret;
			}
		}
		conf = get_multipath_config();
		pthread_cleanup_push(put_multipath_config, conf);
		if (pp->udev && pp->uid_attribute &&
		    filter_property(conf, pp->udev, 3, pp->uid_attribute) > 0)
			invalid = 1;
		pthread_cleanup_pop(1);
		if (invalid)
			return 2;

		refwwid = pp->wwid;
		goto out;
	}

	if (dev_type == DEV_DEVT) {
		strchop(dev);
		if (devt2devname(buff, FILE_NAME_SIZE, dev)) {
			condlog(0, "%s: cannot find block device\n", dev);
			return 1;
		}
		pp = find_path_by_dev(pathvec, buff);
		if (!pp) {
			struct udev_device *udevice =
				get_udev_device(dev, dev_type);

			if (!udevice)
				return 1;

			conf = get_multipath_config();
			pthread_cleanup_push(put_multipath_config, conf);
			ret = store_pathinfo(pathvec, conf, udevice,
					     flags, &pp);
			pthread_cleanup_pop(1);
			udev_device_unref(udevice);
			if (!pp) {
				if (ret == 1)
					condlog(0, "%s can't store path info",
						buff);
				return ret;
			}
		}
		conf = get_multipath_config();
		pthread_cleanup_push(put_multipath_config, conf);
		if (pp->udev && pp->uid_attribute &&
		    filter_property(conf, pp->udev, 3, pp->uid_attribute) > 0)
			invalid = 1;
		pthread_cleanup_pop(1);
		if (invalid)
			return 2;
		refwwid = pp->wwid;
		goto out;
	}

	if (dev_type == DEV_UEVENT) {
		struct udev_device *udevice = get_udev_device(dev, dev_type);

		if (!udevice)
			return 1;

		conf = get_multipath_config();
		pthread_cleanup_push(put_multipath_config, conf);
		ret = store_pathinfo(pathvec, conf, udevice,
				     flags, &pp);
		pthread_cleanup_pop(1);
		udev_device_unref(udevice);
		if (!pp) {
			if (ret == 1)
				condlog(0, "%s: can't store path info", dev);
			return ret;
		}
		conf = get_multipath_config();
		pthread_cleanup_push(put_multipath_config, conf);
		if (pp->udev && pp->uid_attribute &&
		    filter_property(conf, pp->udev, 3, pp->uid_attribute) > 0)
			invalid = 1;
		pthread_cleanup_pop(1);
		if (invalid)
			return 2;
		refwwid = pp->wwid;
		goto out;
	}

	if (dev_type == DEV_DEVMAP) {

		conf = get_multipath_config();
		pthread_cleanup_push(put_multipath_config, conf);
		if (((dm_get_uuid(dev, tmpwwid, WWID_SIZE)) == 0)
		    && (strlen(tmpwwid))) {
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
		refwwid = get_mpe_wwid(conf->mptable, dev);

		/*
		 * or directly a wwid
		 */
		if (!refwwid)
			refwwid = dev;

check:
		if (refwwid && strlen(refwwid) &&
		    filter_wwid(conf->blist_wwid, conf->elist_wwid, refwwid,
				NULL) > 0)
			invalid = 1;
		pthread_cleanup_pop(1);
		if (invalid)
			return 2;
	}
out:
	if (refwwid && strlen(refwwid)) {
		*wwid = STRDUP(refwwid);
		return 0;
	}

	return 1;
}

int reload_map(struct vectors *vecs, struct multipath *mpp, int refresh,
	       int is_daemon)
{
	char params[PARAMS_SIZE] = {0};
	struct path *pp;
	int i, r;

	update_mpp_paths(mpp, vecs->pathvec);
	if (refresh) {
		vector_foreach_slot (mpp->paths, pp, i) {
			struct config *conf = get_multipath_config();
			pthread_cleanup_push(put_multipath_config, conf);
			r = pathinfo(pp, conf, DI_PRIO);
			pthread_cleanup_pop(1);
			if (r) {
				condlog(2, "%s: failed to refresh pathinfo",
					mpp->alias);
				return 1;
			}
		}
	}
	if (setup_map(mpp, params, PARAMS_SIZE, vecs)) {
		condlog(0, "%s: failed to setup map", mpp->alias);
		return 1;
	}
	select_action(mpp, vecs->mpvec, 1);

	r = domap(mpp, params, is_daemon);
	if (r == DOMAP_FAIL || r == DOMAP_RETRY) {
		condlog(3, "%s: domap (%u) failure "
			"for reload map", mpp->alias, r);
		return 1;
	}

	return 0;
}
