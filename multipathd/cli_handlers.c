/*
 * Copyright (c) 2005 Christophe Varoqui
 */

#define _GNU_SOURCE

#include "checkers.h"
#include "vector.h"
#include "structs.h"
#include "structs_vec.h"
#include <libdevmapper.h>
#include "devmapper.h"
#include "discovery.h"
#include "config.h"
#include "configure.h"
#include "blacklist.h"
#include "debug.h"
#include "dm-generic.h"
#include "print.h"
#include "sysfs.h"
#include <errno.h>
#include <libudev.h>
#include <mpath_persist.h>
#include "util.h"
#include "prkey.h"
#include "propsel.h"
#include "main.h"
#include "mpath_cmd.h"
#include "cli.h"
#include "uevent.h"
#include "foreign.h"
#include "strbuf.h"
#include "cli_handlers.h"

static int
show_paths (struct strbuf *reply, struct vectors *vecs, char *style, int pretty)
{
	int i;
	struct path * pp;
	int hdr_len = 0;
	fieldwidth_t *width __attribute__((cleanup(cleanup_ucharp))) = NULL;

	if (pretty) {
		if ((width = alloc_path_layout()) == NULL)
			return 1;
		get_path_layout(vecs->pathvec, 1, width);
		foreign_path_layout(width);
	}
	if (pretty && (hdr_len = snprint_path_header(reply, style, width)) < 0)
		return 1;

	vector_foreach_slot(vecs->pathvec, pp, i) {
		if (snprint_path(reply, style, pp, width) < 0)
			return 1;
	}
	if (snprint_foreign_paths(reply, style, width) < 0)
		return 1;

	if (pretty && get_strbuf_len(reply) == (size_t)hdr_len)
		/* No output - clear header */
		truncate_strbuf(reply, 0);

	return 0;
}

static int
show_path (struct strbuf *reply, struct vectors *vecs, struct path *pp,
	   char *style)
{
	if (snprint_path(reply, style, pp, NULL) < 0)
		return 1;
	return 0;
}

static int
show_map_topology (struct strbuf *reply, struct multipath *mpp,
		   struct vectors *vecs, const fieldwidth_t *width)
{
	if (update_multipath(vecs, mpp->alias, 0))
		return 1;

	if (snprint_multipath_topology(reply, mpp, 2, width) < 0)
		return 1;

	return 0;
}

static int
show_maps_topology (struct strbuf *reply, struct vectors * vecs)
{
	int i;
	struct multipath * mpp;
	fieldwidth_t *p_width __attribute__((cleanup(cleanup_ucharp))) = NULL;

	if ((p_width = alloc_path_layout()) == NULL)
		return 1;
	get_path_layout(vecs->pathvec, 0, p_width);
	foreign_path_layout(p_width);

	vector_foreach_slot(vecs->mpvec, mpp, i) {
		if (update_multipath(vecs, mpp->alias, 0)) {
			i--;
			continue;
		}
		if (snprint_multipath_topology(reply, mpp, 2, p_width) < 0)
			return 1;
	}
	if (snprint_foreign_topology(reply, 2, p_width) < 0)
		return 1;

	return 0;
}

static int
show_maps_json (struct strbuf *reply, struct vectors * vecs)
{
	int i;
	struct multipath * mpp;

	vector_foreach_slot(vecs->mpvec, mpp, i) {
		if (update_multipath(vecs, mpp->alias, 0)) {
			return 1;
		}
	}

	if (snprint_multipath_topology_json(reply, vecs) < 0)
		return 1;

	return 0;
}

static int
show_map_json (struct strbuf *reply, struct multipath * mpp,
	       struct vectors * vecs)
{
	if (update_multipath(vecs, mpp->alias, 0))
		return 1;

	if (snprint_multipath_map_json(reply, mpp) < 0)
		return 1;

	return 0;
}

static int
show_config (struct strbuf *reply, const struct _vector *hwtable,
	     const struct _vector *mpvec)
{
	struct config *conf;
	int rc;

	conf = get_multipath_config();
	pthread_cleanup_push(put_multipath_config, conf);
	rc = __snprint_config(conf, reply, hwtable, mpvec);
	pthread_cleanup_pop(1);
	if (rc < 0)
		return 1;
	return 0;
}

void
reset_stats(struct multipath * mpp)
{
	mpp->stat_switchgroup = 0;
	mpp->stat_path_failures = 0;
	mpp->stat_map_loads = 0;
	mpp->stat_total_queueing_time = 0;
	mpp->stat_queueing_timeouts = 0;
	mpp->stat_map_failures = 0;
}

static int
cli_list_config (void *v, struct strbuf *reply, void *data)
{
	condlog(3, "list config (operator)");

	return show_config(reply, NULL, NULL);
}

static void v_free(void *x)
{
	vector_free(x);
}

static int
cli_list_config_local (void *v, struct strbuf *reply, void *data)
{
	struct vectors *vecs = (struct vectors *)data;
	vector hwes;
	int ret;

	condlog(3, "list config local (operator)");

	hwes = get_used_hwes(vecs->pathvec);
	pthread_cleanup_push(v_free, hwes);
	ret = show_config(reply, hwes, vecs->mpvec);
	pthread_cleanup_pop(1);
	return ret;
}

static int
cli_list_paths (void *v, struct strbuf *reply, void *data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list paths (operator)");

	return show_paths(reply, vecs, PRINT_PATH_CHECKER, 1);
}

static int
cli_list_paths_fmt (void *v, struct strbuf *reply, void *data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * fmt = get_keyparam(v, KEY_FMT);

	condlog(3, "list paths (operator)");

	return show_paths(reply, vecs, fmt, 1);
}

static int
cli_list_paths_raw (void *v, struct strbuf *reply, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * fmt = get_keyparam(v, KEY_FMT);

	condlog(3, "list paths (operator)");

	return show_paths(reply, vecs, fmt, 0);
}

static int
cli_list_path (void *v, struct strbuf *reply, void *data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, KEY_PATH);
	struct path *pp;

	param = convert_dev(param, 1);
	condlog(3, "%s: list path (operator)", param);

	pp = find_path_by_dev(vecs->pathvec, param);
	if (!pp)
		return 1;

	return show_path(reply, vecs, pp, "%o");
}

static int
cli_list_map_topology (void *v, struct strbuf *reply, void *data)
{
	struct multipath * mpp;
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, KEY_MAP);
	fieldwidth_t *p_width __attribute__((cleanup(cleanup_ucharp))) = NULL;

	if ((p_width = alloc_path_layout()) == NULL)
		return 1;
	get_path_layout(vecs->pathvec, 0, p_width);
	param = convert_dev(param, 0);
	mpp = find_mp_by_str(vecs->mpvec, param);

	if (!mpp)
		return 1;

	condlog(3, "list multipath %s (operator)", param);

	return show_map_topology(reply, mpp, vecs, p_width);
}

static int
cli_list_maps_topology (void *v, struct strbuf *reply, void *data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list multipaths (operator)");

	return show_maps_topology(reply, vecs);
}

static int
cli_list_map_json (void *v, struct strbuf *reply, void *data)
{
	struct multipath * mpp;
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, KEY_MAP);

	param = convert_dev(param, 0);
	mpp = find_mp_by_str(vecs->mpvec, param);

	if (!mpp)
		return 1;

	condlog(3, "list multipath json %s (operator)", param);

	return show_map_json(reply, mpp, vecs);
}

static int
cli_list_maps_json (void *v, struct strbuf *reply, void *data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list multipaths json (operator)");

	return show_maps_json(reply, vecs);
}

static int
cli_list_wildcards (void *v, struct strbuf *reply, void *data)
{
	if (snprint_wildcards(reply) < 0)
		return 1;

	return 0;
}

static int
show_status (struct strbuf *reply, struct vectors *vecs)
{
	if (snprint_status(reply, vecs) < 0)
		return 1;

	return 0;
}

static int
show_daemon (struct strbuf *reply)
{
	if (print_strbuf(reply, "pid %d %s\n",
			 daemon_pid, daemon_status()) < 0)
		return 1;

	return 0;
}

static int
show_map (struct strbuf *reply, struct multipath *mpp, char *style,
	  const fieldwidth_t *width)
{
	if (snprint_multipath(reply, style, mpp, width) < 0)
		return 1;

	return 0;
}

static int
show_maps (struct strbuf *reply, struct vectors *vecs, char *style,
	   int pretty)
{
	int i;
	struct multipath * mpp;
	int hdr_len = 0;
	fieldwidth_t *width __attribute__((cleanup(cleanup_ucharp))) = NULL;

	if (pretty) {
		if ((width = alloc_multipath_layout()) == NULL)
			return 1;
		get_multipath_layout(vecs->mpvec, 1, width);
		foreign_multipath_layout(width);
	}

	if (pretty && (hdr_len = snprint_multipath_header(reply, style, width)) < 0)
		return 1;

	vector_foreach_slot(vecs->mpvec, mpp, i) {
		if (update_multipath(vecs, mpp->alias, 0)) {
			i--;
			continue;
		}
		if (snprint_multipath(reply, style, mpp, width) < 0)
			return 1;
	}
	if (snprint_foreign_multipaths(reply, style, width) < 0)
		return 1;

	if (pretty && get_strbuf_len(reply) == (size_t)hdr_len)
		/* No output - clear header */
		truncate_strbuf(reply, 0);

	return 0;
}

static int
cli_list_maps_fmt (void *v, struct strbuf *reply, void *data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * fmt = get_keyparam(v, KEY_FMT);

	condlog(3, "list maps (operator)");

	return show_maps(reply, vecs, fmt, 1);
}

static int
cli_list_maps_raw (void *v, struct strbuf *reply, void *data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * fmt = get_keyparam(v, KEY_FMT);

	condlog(3, "list maps (operator)");

	return show_maps(reply, vecs, fmt, 0);
}

static int
cli_list_map_fmt (void *v, struct strbuf *reply, void *data)
{
	struct multipath * mpp;
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, KEY_MAP);
	char * fmt = get_keyparam(v, KEY_FMT);
	fieldwidth_t *width __attribute__((cleanup(cleanup_ucharp))) = NULL;

	if ((width = alloc_multipath_layout()) == NULL)
		return 1;
	get_multipath_layout(vecs->mpvec, 1, width);
	param = convert_dev(param, 0);
	mpp = find_mp_by_str(vecs->mpvec, param);
	if (!mpp)
		return 1;

	condlog(3, "list map %s fmt %s (operator)", param, fmt);

	return show_map(reply, mpp, fmt, width);
}

static int
cli_list_maps (void *v, struct strbuf *reply, void *data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list maps (operator)");

	return show_maps(reply, vecs, PRINT_MAP_NAMES, 1);
}

static int
cli_list_status (void *v, struct strbuf *reply, void *data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list status (operator)");

	return show_status(reply, vecs);
}

static int
cli_list_maps_status (void *v, struct strbuf *reply, void *data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list maps status (operator)");

	return show_maps(reply, vecs, PRINT_MAP_STATUS, 1);
}

static int
cli_list_maps_stats (void *v, struct strbuf *reply, void *data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list maps stats (operator)");

	return show_maps(reply, vecs, PRINT_MAP_STATS, 1);
}

static int
cli_list_daemon (void *v, struct strbuf *reply, void *data)
{
	condlog(3, "list daemon (operator)");

	return show_daemon(reply);
}

static int
cli_reset_maps_stats (void *v, struct strbuf *reply, void *data)
{
	struct vectors * vecs = (struct vectors *)data;
	int i;
	struct multipath * mpp;

	condlog(3, "reset multipaths stats (operator)");

	vector_foreach_slot(vecs->mpvec, mpp, i) {
		reset_stats(mpp);
	}
	return 0;
}

static int
cli_reset_map_stats (void *v, struct strbuf *reply, void *data)
{
	struct vectors * vecs = (struct vectors *)data;
	struct multipath * mpp;
	char * param = get_keyparam(v, KEY_MAP);

	param = convert_dev(param, 0);
	mpp = find_mp_by_str(vecs->mpvec, param);

	if (!mpp)
		return 1;

	condlog(3, "reset multipath %s stats (operator)", param);
	reset_stats(mpp);
	return 0;
}

static int
add_partial_path(struct path *pp, struct vectors *vecs)
{
	char wwid[WWID_SIZE];
	struct udev_device *udd;

	udd = get_udev_device(pp->dev_t, DEV_DEVT);
	if (!udd)
		return 0;
	strcpy(wwid, pp->wwid);
	if (get_uid(pp, pp->state, udd, 0) != 0) {
		strcpy(pp->wwid, wwid);
		udev_device_unref(udd);
		return 0;
	}
	if (strlen(wwid) && strncmp(wwid, pp->wwid, WWID_SIZE) != 0) {
		condlog(0, "%s: path wwid changed from '%s' to '%s'. removing",
			pp->dev, wwid, pp->wwid);
		ev_remove_path(pp, vecs, 1);
		udev_device_unref(udd);
		return -1;
	}
	udev_device_unref(pp->udev);
	pp->udev = udd;
	return finish_path_init(pp, vecs);
}

static int
cli_add_path (void *v, struct strbuf *reply, void *data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, KEY_PATH);
	struct path *pp;
	int r;
	struct config *conf;
	int invalid = 0;

	param = convert_dev(param, 1);
	condlog(2, "%s: add path (operator)", param);
	conf = get_multipath_config();
	pthread_cleanup_push(put_multipath_config, conf);
	if (filter_devnode(conf->blist_devnode, conf->elist_devnode,
			   param) > 0)
		invalid = 1;
	pthread_cleanup_pop(1);
	if (invalid)
		goto blacklisted;

	pp = find_path_by_dev(vecs->pathvec, param);
	if (pp && pp->initialized != INIT_REMOVED) {
		condlog(2, "%s: path already in pathvec", param);

		if (pp->initialized == INIT_PARTIAL) {
			if (add_partial_path(pp, vecs) < 0)
				return 1;
		}
		else if (pp->recheck_wwid == RECHECK_WWID_ON &&
			 check_path_wwid_change(pp)) {
			condlog(0, "%s: wwid changed. Removing device",
				pp->dev);
			handle_path_wwid_change(pp, vecs);
			return 1;
		}

		if (pp->mpp)
			return 0;
	} else if (pp) {
		/* Trying to add a path in INIT_REMOVED state */
		struct multipath *prev_mpp;

		prev_mpp = pp->mpp;
		if (prev_mpp == NULL)
			condlog(0, "Bug: %s was in INIT_REMOVED state without being a multipath member",
				pp->dev);
		pp->mpp = NULL;
		pp->initialized = INIT_NEW;
		pp->wwid[0] = '\0';
		conf = get_multipath_config();
		pthread_cleanup_push(put_multipath_config, conf);
		r = pathinfo(pp, conf, DI_ALL | DI_BLACKLIST);
		pthread_cleanup_pop(1);

		if (prev_mpp) {
			/* Similar logic as in uev_add_path() */
			pp->mpp = prev_mpp;
			if (r == PATHINFO_OK &&
			    !strncmp(prev_mpp->wwid, pp->wwid, WWID_SIZE)) {
				condlog(2, "%s: path re-added to %s", pp->dev,
					pp->mpp->alias);
				/* Have the checker reinstate this path asap */
				pp->tick = 1;
				return 0;
			} else if (ev_remove_path(pp, vecs, true) &
				   REMOVE_PATH_SUCCESS)
				/* Path removed in ev_remove_path() */
				pp = NULL;
			else {
				/* Init state is now INIT_REMOVED again */
				pp->dmstate = PSTATE_FAILED;
				dm_fail_path(pp->mpp->alias, pp->dev_t);
				condlog(1, "%s: failed to re-add path still mapped in %s",
					pp->dev, pp->mpp->alias);
				return 1;
			}
		} else {
			switch (r) {
			case PATHINFO_SKIPPED:
				goto blacklisted;
			case PATHINFO_OK:
				break;
			default:
				condlog(0, "%s: failed to get pathinfo", param);
				return 1;
			}
		}
	}

	if (!pp) {
		struct udev_device *udevice;

		udevice = udev_device_new_from_subsystem_sysname(udev,
								 "block",
								 param);
		if (!udevice) {
			condlog(0, "%s: can't find path", param);
			return 1;
		}
		conf = get_multipath_config();
		pthread_cleanup_push(put_multipath_config, conf);
		r = store_pathinfo(vecs->pathvec, conf,
				   udevice, DI_ALL | DI_BLACKLIST, &pp);
		pthread_cleanup_pop(1);
		udev_device_unref(udevice);
		if (!pp) {
			if (r == 2)
				goto blacklisted;
			condlog(0, "%s: failed to store path info", param);
			return 1;
		}
	}
	return ev_add_path(pp, vecs, 1);
blacklisted:
	append_strbuf_str(reply, "blacklisted\n");
	condlog(2, "%s: path blacklisted", param);
	return 0;
}

static int
cli_del_path (void * v, struct strbuf *reply, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, KEY_PATH);
	struct path *pp;
	int ret;

	param = convert_dev(param, 1);
	condlog(2, "%s: remove path (operator)", param);
	pp = find_path_by_dev(vecs->pathvec, param);
	if (!pp) {
		condlog(0, "%s: path already removed", param);
		return 1;
	}
	ret = ev_remove_path(pp, vecs, 1);
	if (ret == REMOVE_PATH_DELAY)
		append_strbuf_str(reply, "delayed\n");
	else if (ret == REMOVE_PATH_MAP_ERROR)
		append_strbuf_str(reply, "map reload error. removed\n");
	return (ret == REMOVE_PATH_FAILURE);
}

static int
cli_add_map (void * v, struct strbuf *reply, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, KEY_MAP);
	int major = -1, minor = -1;
	char dev_path[FILE_NAME_SIZE];
	char *refwwid, *alias = NULL;
	int rc, count = 0;
	struct config *conf;
	int invalid = 0;

	param = convert_dev(param, 0);
	condlog(2, "%s: add map (operator)", param);

	conf = get_multipath_config();
	pthread_cleanup_push(put_multipath_config, conf);
	if (filter_wwid(conf->blist_wwid, conf->elist_wwid, param, NULL) > 0)
		invalid = 1;
	pthread_cleanup_pop(1);
	if (invalid) {
		append_strbuf_str(reply, "blacklisted\n");
		condlog(2, "%s: map blacklisted", param);
		return 1;
	}
	do {
		if (dm_get_major_minor(param, &major, &minor) < 0)
			condlog(count ? 2 : 3,
				"%s: not a device mapper table", param);
		else {
			sprintf(dev_path, "dm-%d", minor);
			alias = dm_mapname(major, minor);
		}
		/*if there is no mapname found, we first create the device*/
		if (!alias && !count) {
			condlog(3, "%s: mapname not found for %d:%d",
				param, major, minor);
			get_refwwid(CMD_NONE, param, DEV_DEVMAP,
				    vecs->pathvec, &refwwid);
			if (refwwid) {
				if (coalesce_paths(vecs, NULL, refwwid,
						   FORCE_RELOAD_NONE, CMD_NONE)
				    != CP_OK)
					condlog(2, "%s: coalesce_paths failed",
									param);
				free(refwwid);
			}
		} /*we attempt to create device only once*/
		count++;
	} while (!alias && (count < 2));

	if (!alias) {
		condlog(2, "%s: add map failed", param);
		return 1;
	}
	rc = ev_add_map(dev_path, alias, vecs);
	free(alias);
	return rc;
}

static int
cli_del_map (void * v, struct strbuf *reply, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, KEY_MAP);
	int major, minor;
	char *alias;
	int rc;

	param = convert_dev(param, 0);
	condlog(2, "%s: remove map (operator)", param);
	if (dm_get_major_minor(param, &major, &minor) < 0) {
		condlog(2, "%s: not a device mapper table", param);
		return 1;
	}
	alias = dm_mapname(major, minor);
	if (!alias) {
		condlog(2, "%s: mapname not found for %d:%d",
			param, major, minor);
		return 1;
	}
	rc = ev_remove_map(param, alias, minor, vecs);
	if (rc == 2)
		append_strbuf_str(reply, "delayed");

	free(alias);
	return rc;
}

static int
cli_del_maps (void *v, struct strbuf *reply, void *data)
{
	struct vectors * vecs = (struct vectors *)data;
	struct multipath *mpp;
	int i, ret = 0;

	condlog(2, "remove maps (operator)");
	vector_foreach_slot(vecs->mpvec, mpp, i) {
		if (flush_map(mpp, vecs, 0))
			ret++;
		else
			i--;
	}
	/* flush any multipath maps that aren't currently known by multipathd */
	ret |= dm_flush_maps(0, 0);
	return ret;
}

static int
cli_reload(void *v, struct strbuf *reply, void *data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * mapname = get_keyparam(v, KEY_MAP);
	struct multipath *mpp;
	int minor;

	mapname = convert_dev(mapname, 0);
	condlog(2, "%s: reload map (operator)", mapname);
	if (sscanf(mapname, "dm-%d", &minor) == 1)
		mpp = find_mp_by_minor(vecs->mpvec, minor);
	else
		mpp = find_mp_by_alias(vecs->mpvec, mapname);

	if (!mpp) {
		condlog(0, "%s: invalid map name. cannot reload", mapname);
		return 1;
	}
	if (mpp->wait_for_udev) {
		condlog(2, "%s: device not fully created, failing reload",
			mpp->alias);
		return 1;
	}

	return reload_and_sync_map(mpp, vecs, 0);
}

static int resize_map(struct multipath *mpp, unsigned long long size,
	       struct vectors * vecs)
{
	char *params __attribute__((cleanup(cleanup_charp))) = NULL;
	unsigned long long orig_size = mpp->size;

	mpp->size = size;
	update_mpp_paths(mpp, vecs->pathvec);
	if (setup_map(mpp, &params, vecs) != 0) {
		condlog(0, "%s: failed to setup map for resize : %s",
			mpp->alias, strerror(errno));
		mpp->size = orig_size;
		return 1;
	}
	mpp->action = ACT_RESIZE;
	mpp->force_udev_reload = 1;
	if (domap(mpp, params, 1) == DOMAP_FAIL) {
		condlog(0, "%s: failed to resize map : %s", mpp->alias,
			strerror(errno));
		mpp->size = orig_size;
		return 1;
	}
	return 0;
}

static int
cli_resize(void *v, struct strbuf *reply, void *data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * mapname = get_keyparam(v, KEY_MAP);
	struct multipath *mpp;
	int minor;
	unsigned long long size;
	struct pathgroup *pgp;
	struct path *pp;

	mapname = convert_dev(mapname, 0);
	condlog(2, "%s: resize map (operator)", mapname);
	if (sscanf(mapname, "dm-%d", &minor) == 1)
		mpp = find_mp_by_minor(vecs->mpvec, minor);
	else
		mpp = find_mp_by_alias(vecs->mpvec, mapname);

	if (!mpp) {
		condlog(0, "%s: invalid map name. cannot resize", mapname);
		return 1;
	}

	if (mpp->wait_for_udev) {
		condlog(2, "%s: device not fully created, failing resize",
			mpp->alias);
		return 1;
	}

	pgp = VECTOR_SLOT(mpp->pg, 0);

	if (!pgp){
		condlog(0, "%s: couldn't get path group. cannot resize",
			mapname);
		return 1;
	}
	pp = VECTOR_SLOT(pgp->paths, 0);

	if (!pp){
		condlog(0, "%s: couldn't get path. cannot resize", mapname);
		return 1;
	}
	if (!pp->udev || sysfs_get_size(pp, &size)) {
		condlog(0, "%s: couldn't get size for sysfs. cannot resize",
			mapname);
		return 1;
	}
	if (size == mpp->size) {
		condlog(0, "%s: map is still the same size (%llu)", mapname,
			mpp->size);
		return 0;
	}
	condlog(3, "%s old size is %llu, new size is %llu", mapname, mpp->size,
		size);

	if (resize_map(mpp, size, vecs) != 0)
		return 1;

	if (setup_multipath(vecs, mpp) != 0)
		return 1;
	sync_map_state(mpp);

	return 0;
}

static int
cli_force_no_daemon_q(void * v, struct strbuf *reply, void * data)
{
	struct config *conf;

	condlog(2, "force queue_without_daemon (operator)");
	conf = get_multipath_config();
	if (conf->queue_without_daemon == QUE_NO_DAEMON_OFF)
		conf->queue_without_daemon = QUE_NO_DAEMON_FORCE;
	put_multipath_config(conf);
	return 0;
}

static int
cli_restore_no_daemon_q(void * v, struct strbuf *reply, void * data)
{
	struct config *conf;

	condlog(2, "restore queue_without_daemon (operator)");
	conf = get_multipath_config();
	if (conf->queue_without_daemon == QUE_NO_DAEMON_FORCE)
		conf->queue_without_daemon = QUE_NO_DAEMON_OFF;
	put_multipath_config(conf);
	return 0;
}

static int
cli_restore_queueing(void *v, struct strbuf *reply, void *data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * mapname = get_keyparam(v, KEY_MAP);
	struct multipath *mpp;
	int minor;
	struct config *conf;

	mapname = convert_dev(mapname, 0);
	condlog(2, "%s: restore map queueing (operator)", mapname);
	if (sscanf(mapname, "dm-%d", &minor) == 1)
		mpp = find_mp_by_minor(vecs->mpvec, minor);
	else
		mpp = find_mp_by_alias(vecs->mpvec, mapname);

	if (!mpp) {
		condlog(0, "%s: invalid map name, cannot restore queueing", mapname);
		return 1;
	}

	mpp->disable_queueing = 0;
	conf = get_multipath_config();
	pthread_cleanup_push(put_multipath_config, conf);
	select_no_path_retry(conf, mpp);
	pthread_cleanup_pop(1);

	/*
	 * Don't call set_no_path_retry() for the NO_PATH_RETRY_FAIL case.
	 * That would disable queueing when "restorequeueing" is called,
	 * and the code never behaved that way. Users might not expect it.
	 * In almost all cases, queueing will be disabled anyway when we
	 * are here.
	 */
	if (mpp->no_path_retry != NO_PATH_RETRY_UNDEF &&
	    mpp->no_path_retry != NO_PATH_RETRY_FAIL)
		set_no_path_retry(mpp);

	return 0;
}

static int
cli_restore_all_queueing(void *v, struct strbuf *reply, void *data)
{
	struct vectors * vecs = (struct vectors *)data;
	struct multipath *mpp;
	int i;

	condlog(2, "restore queueing (operator)");
	vector_foreach_slot(vecs->mpvec, mpp, i) {
		mpp->disable_queueing = 0;
		struct config *conf = get_multipath_config();
		pthread_cleanup_push(put_multipath_config, conf);
		select_no_path_retry(conf, mpp);
		pthread_cleanup_pop(1);
		/* See comment in cli_restore_queueing() */
		if (mpp->no_path_retry != NO_PATH_RETRY_UNDEF &&
		    mpp->no_path_retry != NO_PATH_RETRY_FAIL)
			set_no_path_retry(mpp);
	}
	return 0;
}

static int
cli_disable_queueing(void *v, struct strbuf *reply, void *data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * mapname = get_keyparam(v, KEY_MAP);
	struct multipath *mpp;
	int minor;

	mapname = convert_dev(mapname, 0);
	condlog(2, "%s: disable map queueing (operator)", mapname);
	if (sscanf(mapname, "dm-%d", &minor) == 1)
		mpp = find_mp_by_minor(vecs->mpvec, minor);
	else
		mpp = find_mp_by_alias(vecs->mpvec, mapname);

	if (!mpp) {
		condlog(0, "%s: invalid map name, cannot disable queueing", mapname);
		return 1;
	}

	if (count_active_paths(mpp) == 0)
		mpp->stat_map_failures++;
	mpp->retry_tick = 0;
	mpp->no_path_retry = NO_PATH_RETRY_FAIL;
	mpp->disable_queueing = 1;
	set_no_path_retry(mpp);
	return 0;
}

static int
cli_disable_all_queueing(void *v, struct strbuf *reply, void *data)
{
	struct vectors * vecs = (struct vectors *)data;
	struct multipath *mpp;
	int i;

	condlog(2, "disable queueing (operator)");
	vector_foreach_slot(vecs->mpvec, mpp, i) {
		if (count_active_paths(mpp) == 0)
			mpp->stat_map_failures++;
		mpp->retry_tick = 0;
		mpp->no_path_retry = NO_PATH_RETRY_FAIL;
		mpp->disable_queueing = 1;
		set_no_path_retry(mpp);
	}
	return 0;
}

static int
cli_switch_group(void * v, struct strbuf *reply, void * data)
{
	char * mapname = get_keyparam(v, KEY_MAP);
	int groupnum = atoi(get_keyparam(v, KEY_GROUP));

	mapname = convert_dev(mapname, 0);
	condlog(2, "%s: switch to path group #%i (operator)", mapname, groupnum);

	return dm_switchgroup(mapname, groupnum);
}

static int
cli_reconfigure(void * v, struct strbuf *reply, void * data)
{
	condlog(2, "reconfigure (operator)");

	schedule_reconfigure(FORCE_RELOAD_WEAK);
	return 0;
}

int
cli_reconfigure_all(void * v, struct strbuf *reply, void * data)
{
	condlog(2, "reconfigure all (operator)");

	schedule_reconfigure(FORCE_RELOAD_YES);
	return 0;
}

static int
cli_suspend(void * v, struct strbuf *reply, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, KEY_MAP);
	int r;
	struct multipath * mpp;

	param = convert_dev(param, 0);
	mpp = find_mp_by_alias(vecs->mpvec, param);
	if (!mpp)
		return 1;

	if (mpp->wait_for_udev) {
		condlog(2, "%s: device not fully created, failing suspend",
			mpp->alias);
		return 1;
	}

	r = dm_simplecmd_noflush(DM_DEVICE_SUSPEND, param, 0);

	condlog(2, "%s: suspend (operator)", param);

	if (!r) /* error */
		return 1;

	dm_get_info(param, &mpp->dmi);
	return 0;
}

static int
cli_resume(void * v, struct strbuf *reply, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, KEY_MAP);
	int r;
	struct multipath * mpp;
	uint16_t udev_flags;

	param = convert_dev(param, 0);
	mpp = find_mp_by_alias(vecs->mpvec, param);
	if (!mpp)
		return 1;

	udev_flags = (mpp->skip_kpartx)? MPATH_UDEV_NO_KPARTX_FLAG : 0;
	if (mpp->wait_for_udev) {
		condlog(2, "%s: device not fully created, failing resume",
			mpp->alias);
		return 1;
	}

	r = dm_simplecmd_noflush(DM_DEVICE_RESUME, param, udev_flags);

	condlog(2, "%s: resume (operator)", param);

	if (!r) /* error */
		return 1;

	dm_get_info(param, &mpp->dmi);
	return 0;
}

static int
cli_reinstate(void * v, struct strbuf *reply, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, KEY_PATH);
	struct path * pp;

	param = convert_dev(param, 1);
	pp = find_path_by_dev(vecs->pathvec, param);

	if (!pp)
		 pp = find_path_by_devt(vecs->pathvec, param);

	if (!pp || !pp->mpp || !pp->mpp->alias)
		return 1;

	condlog(2, "%s: reinstate path %s (operator)",
		pp->mpp->alias, pp->dev_t);

	checker_enable(&pp->checker);
	return dm_reinstate_path(pp->mpp->alias, pp->dev_t);
}

static int
cli_reassign (void * v, struct strbuf *reply, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, KEY_MAP);
	struct multipath *mpp;

	param = convert_dev(param, 0);
	mpp = find_mp_by_alias(vecs->mpvec, param);
	if (!mpp)
		return 1;

	if (mpp->wait_for_udev) {
		condlog(2, "%s: device not fully created, failing reassign",
			mpp->alias);
		return 1;
	}

	condlog(3, "%s: reset devices (operator)", param);

	dm_reassign(param);
	return 0;
}

static int
cli_fail(void * v, struct strbuf *reply, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, KEY_PATH);
	struct path * pp;
	int r;

	param = convert_dev(param, 1);
	pp = find_path_by_dev(vecs->pathvec, param);

	if (!pp)
		 pp = find_path_by_devt(vecs->pathvec, param);

	if (!pp || !pp->mpp || !pp->mpp->alias)
		return 1;

	condlog(2, "%s: fail path %s (operator)",
		pp->mpp->alias, pp->dev_t);

	r = dm_fail_path(pp->mpp->alias, pp->dev_t);
	/*
	 * Suspend path checking to avoid auto-reinstating the path
	 */
	if (!r)
		checker_disable(&pp->checker);
	return r;
}

static int
show_blacklist (struct strbuf *reply)
{
	struct config *conf;
	bool fail;

	conf = get_multipath_config();
	pthread_cleanup_push(put_multipath_config, conf);
	fail = snprint_blacklist_report(conf, reply) < 0;
	pthread_cleanup_pop(1);

	if (fail)
		return 1;

	return 0;
}

static int
cli_list_blacklist (void * v, struct strbuf *reply, void * data)
{
	condlog(3, "list blacklist (operator)");

	return show_blacklist(reply);
}

static int
show_devices (struct strbuf *reply, struct vectors *vecs)
{
	struct config *conf;
	bool fail;

	conf = get_multipath_config();
	pthread_cleanup_push(put_multipath_config, conf);
	fail = snprint_devices(conf, reply, vecs) < 0;
	pthread_cleanup_pop(1);

	if (fail)
		return 1;

	return 0;
}

static int
cli_list_devices (void * v, struct strbuf *reply, void * data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list devices (operator)");

	return show_devices(reply, vecs);
}

static int
cli_quit (void * v, struct strbuf *reply, void * data)
{
	return 0;
}

static int
cli_shutdown (void * v, struct strbuf *reply, void * data)
{
	condlog(3, "shutdown (operator)");
	exit_daemon();
	return 0;
}

static int
cli_getprstatus (void * v, struct strbuf *reply, void * data)
{
	struct multipath * mpp;
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, KEY_MAP);

	param = convert_dev(param, 0);
	mpp = find_mp_by_str(vecs->mpvec, param);

	if (!mpp)
		return 1;

	condlog(3, "%s: prflag = %u", param, (unsigned int)mpp->prflag);

	if (print_strbuf(reply, "%d", mpp->prflag) < 0)
		return 1;

	condlog(3, "%s: reply = %s", param, get_strbuf_str(reply));

	return 0;
}

static int
cli_setprstatus(void * v, struct strbuf *reply, void * data)
{
	struct multipath * mpp;
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, KEY_MAP);

	param = convert_dev(param, 0);
	mpp = find_mp_by_str(vecs->mpvec, param);

	if (!mpp)
		return 1;

	if (!mpp->prflag) {
		mpp->prflag = 1;
		condlog(2, "%s: prflag set", param);
	}


	return 0;
}

static int
cli_unsetprstatus(void * v, struct strbuf *reply, void * data)
{
	struct multipath * mpp;
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, KEY_MAP);

	param = convert_dev(param, 0);
	mpp = find_mp_by_str(vecs->mpvec, param);

	if (!mpp)
		return 1;

	if (mpp->prflag) {
		mpp->prflag = 0;
		condlog(2, "%s: prflag unset", param);
	}

	return 0;
}

static int
cli_getprkey(void * v, struct strbuf *reply, void * data)
{
	struct multipath * mpp;
	struct vectors * vecs = (struct vectors *)data;
	char *mapname = get_keyparam(v, KEY_MAP);
	uint64_t key;

	mapname = convert_dev(mapname, 0);
	condlog(3, "%s: get persistent reservation key (operator)", mapname);
	mpp = find_mp_by_str(vecs->mpvec, mapname);

	if (!mpp)
		return 1;

	key = get_be64(mpp->reservation_key);
	if (!key) {
		append_strbuf_str(reply, "none\n");
		return 0;
	}

	if (print_strbuf(reply, "0x%" PRIx64 "%s\n", key,
			 mpp->sa_flags & MPATH_F_APTPL_MASK ? ":aptpl" : "") < 0)
		return 1;
	return 0;
}

static int
cli_unsetprkey(void * v, struct strbuf *reply, void * data)
{
	struct multipath * mpp;
	struct vectors * vecs = (struct vectors *)data;
	char *mapname = get_keyparam(v, KEY_MAP);
	int ret;
	struct config *conf;

	mapname = convert_dev(mapname, 0);
	condlog(3, "%s: unset persistent reservation key (operator)", mapname);
	mpp = find_mp_by_str(vecs->mpvec, mapname);

	if (!mpp)
		return 1;

	conf = get_multipath_config();
	pthread_cleanup_push(put_multipath_config, conf);
	ret = set_prkey(conf, mpp, 0, 0);
	pthread_cleanup_pop(1);

	return ret;
}

static int
cli_setprkey(void * v, struct strbuf *reply, void * data)
{
	struct multipath * mpp;
	struct vectors * vecs = (struct vectors *)data;
	char *mapname = get_keyparam(v, KEY_MAP);
	char *keyparam = get_keyparam(v, KEY_KEY);
	uint64_t prkey;
	uint8_t flags;
	int ret;
	struct config *conf;

	mapname = convert_dev(mapname, 0);
	condlog(3, "%s: set persistent reservation key (operator)", mapname);
	mpp = find_mp_by_str(vecs->mpvec, mapname);

	if (!mpp)
		return 1;

	if (parse_prkey_flags(keyparam, &prkey, &flags) != 0) {
		condlog(0, "%s: invalid prkey : '%s'", mapname, keyparam);
		return 1;
	}

	conf = get_multipath_config();
	pthread_cleanup_push(put_multipath_config, conf);
	ret = set_prkey(conf, mpp, prkey, flags);
	pthread_cleanup_pop(1);

	return ret;
}

static int cli_set_marginal(void * v, struct strbuf *reply, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, KEY_PATH);
	struct path * pp;

	param = convert_dev(param, 1);
	pp = find_path_by_dev(vecs->pathvec, param);

	if (!pp)
		pp = find_path_by_devt(vecs->pathvec, param);

	if (!pp || !pp->mpp || !pp->mpp->alias)
		return 1;

	condlog(2, "%s: set marginal path %s (operator)",
		pp->mpp->alias, pp->dev_t);
	if (pp->mpp->wait_for_udev) {
		condlog(2, "%s: device not fully created, failing set marginal",
			pp->mpp->alias);
		return 1;
	}
	pp->marginal = 1;

	return reload_and_sync_map(pp->mpp, vecs, 0);
}

static int cli_unset_marginal(void * v, struct strbuf *reply, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, KEY_PATH);
	struct path * pp;

	param = convert_dev(param, 1);
	pp = find_path_by_dev(vecs->pathvec, param);

	if (!pp)
		pp = find_path_by_devt(vecs->pathvec, param);

	if (!pp || !pp->mpp || !pp->mpp->alias)
		return 1;

	condlog(2, "%s: unset marginal path %s (operator)",
		pp->mpp->alias, pp->dev_t);
	if (pp->mpp->wait_for_udev) {
		condlog(2, "%s: device not fully created, "
			"failing unset marginal", pp->mpp->alias);
		return 1;
	}
	pp->marginal = 0;

	return reload_and_sync_map(pp->mpp, vecs, 0);
}

static int cli_unset_all_marginal(void * v, struct strbuf *reply, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * mapname = get_keyparam(v, KEY_MAP);
	struct multipath *mpp;
	struct pathgroup * pgp;
	struct path * pp;
	unsigned int i, j;
	int minor;

	mapname = convert_dev(mapname, 0);
	condlog(2, "%s: unset all marginal paths (operator)",
		mapname);

	if (sscanf(mapname, "dm-%d", &minor) == 1)
		mpp = find_mp_by_minor(vecs->mpvec, minor);
	else
		mpp = find_mp_by_alias(vecs->mpvec, mapname);

	if (!mpp) {
		condlog(0, "%s: invalid map name. "
			"cannot unset marginal paths", mapname);
		return 1;
	}
	if (mpp->wait_for_udev) {
		condlog(2, "%s: device not fully created, "
			"failing unset all marginal", mpp->alias);
		return 1;
	}

	vector_foreach_slot (mpp->pg, pgp, i)
		vector_foreach_slot (pgp->paths, pp, j)
			pp->marginal = 0;

	return reload_and_sync_map(mpp, vecs, 0);
}

#define HANDLER(x) x
#include "callbacks.c"
