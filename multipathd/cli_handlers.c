/*
 * Copyright (c) 2005 Christophe Varoqui
 */

#define _GNU_SOURCE

#include "checkers.h"
#include "memory.h"
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
#include "cli_handlers.h"

int
show_paths (char ** r, int * len, struct vectors * vecs, char * style,
	    int pretty)
{
	int i;
	struct path * pp;
	char * c;
	char * reply, * header;
	unsigned int maxlen = INITIAL_REPLY_LEN;
	int again = 1;

	get_path_layout(vecs->pathvec, 1);
	foreign_path_layout();

	reply = MALLOC(maxlen);

	while (again) {
		if (!reply)
			return 1;

		c = reply;

		if (pretty)
			c += snprint_path_header(c, reply + maxlen - c,
						 style);
		header = c;

		vector_foreach_slot(vecs->pathvec, pp, i)
			c += snprint_path(c, reply + maxlen - c,
					  style, pp, pretty);

		c += snprint_foreign_paths(c, reply + maxlen - c,
					   style, pretty);

		again = ((c - reply) == (maxlen - 1));

		REALLOC_REPLY(reply, again, maxlen);
	}

	if (pretty && c == header) {
		/* No output - clear header */
		*reply = '\0';
		c = reply;
	}

	*r = reply;
	*len = (int)(c - reply + 1);
	return 0;
}

int
show_path (char ** r, int * len, struct vectors * vecs, struct path *pp,
	   char * style)
{
	char * c;
	char * reply;
	unsigned int maxlen = INITIAL_REPLY_LEN;
	int again = 1;

	get_path_layout(vecs->pathvec, 1);
	reply = MALLOC(maxlen);

	while (again) {
		if (!reply)
			return 1;

		c = reply;

		c += snprint_path(c, reply + maxlen - c, style, pp, 0);

		again = ((c - reply) == (maxlen - 1));

		REALLOC_REPLY(reply, again, maxlen);
	}
	*r = reply;
	*len = (int)(c - reply + 1);
	return 0;
}

int
show_map_topology (char ** r, int * len, struct multipath * mpp,
		   struct vectors * vecs)
{
	char * c;
	char * reply;
	unsigned int maxlen = INITIAL_REPLY_LEN;
	int again = 1;

	if (update_multipath(vecs, mpp->alias, 0))
		return 1;
	reply = MALLOC(maxlen);

	while (again) {
		if (!reply)
			return 1;

		c = reply;

		c += snprint_multipath_topology(c, reply + maxlen - c, mpp, 2);
		again = ((c - reply) == (maxlen - 1));

		REALLOC_REPLY(reply, again, maxlen);
	}
	*r = reply;
	*len = (int)(c - reply + 1);
	return 0;
}

int
show_maps_topology (char ** r, int * len, struct vectors * vecs)
{
	int i;
	struct multipath * mpp;
	char * c;
	char * reply;
	unsigned int maxlen = INITIAL_REPLY_LEN;
	int again = 1;

	get_path_layout(vecs->pathvec, 0);
	foreign_path_layout();

	reply = MALLOC(maxlen);

	while (again) {
		if (!reply)
			return 1;

		c = reply;

		vector_foreach_slot(vecs->mpvec, mpp, i) {
			if (update_multipath(vecs, mpp->alias, 0)) {
				i--;
				continue;
			}
			c += snprint_multipath_topology(c, reply + maxlen - c,
							mpp, 2);
		}
		c += snprint_foreign_topology(c, reply + maxlen - c, 2);

		again = ((c - reply) == (maxlen - 1));

		REALLOC_REPLY(reply, again, maxlen);
	}

	*r = reply;
	*len = (int)(c - reply + 1);
	return 0;
}

int
show_maps_json (char ** r, int * len, struct vectors * vecs)
{
	int i;
	struct multipath * mpp;
	char * c;
	char * reply;
	unsigned int maxlen = INITIAL_REPLY_LEN;
	int again = 1;

	if (VECTOR_SIZE(vecs->mpvec) > 0)
		maxlen *= PRINT_JSON_MULTIPLIER * VECTOR_SIZE(vecs->mpvec);

	vector_foreach_slot(vecs->mpvec, mpp, i) {
		if (update_multipath(vecs, mpp->alias, 0)) {
			return 1;
		}
	}

	reply = MALLOC(maxlen);

	while (again) {
		if (!reply)
			return 1;

		c = reply;

		c += snprint_multipath_topology_json(c, maxlen, vecs);
		again = ((c - reply) == maxlen);

		REALLOC_REPLY(reply, again, maxlen);
	}
	*r = reply;
	*len = (int)(c - reply);
	return 0;
}

int
show_map_json (char ** r, int * len, struct multipath * mpp,
		   struct vectors * vecs)
{
	char * c;
	char * reply;
	unsigned int maxlen = INITIAL_REPLY_LEN;
	int again = 1;

	if (update_multipath(vecs, mpp->alias, 0))
		return 1;
	reply = MALLOC(maxlen);

	while (again) {
		if (!reply)
			return 1;

		c = reply;

		c += snprint_multipath_map_json(c, maxlen, mpp, 1);
		again = ((c - reply) == maxlen);

		REALLOC_REPLY(reply, again, maxlen);
	}
	*r = reply;
	*len = (int)(c - reply);
	return 0;
}

static int
show_config (char ** r, int * len, const struct _vector *hwtable,
	     const struct _vector *mpvec)
{
	struct config *conf;
	char *reply;

	conf = get_multipath_config();
	pthread_cleanup_push(put_multipath_config, conf);
	reply = snprint_config(conf, len, hwtable, mpvec);
	pthread_cleanup_pop(1);
	if (reply == NULL)
		return 1;
	*r = reply;
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

int
cli_list_config (void * v, char ** reply, int * len, void * data)
{
	condlog(3, "list config (operator)");

	return show_config(reply, len, NULL, NULL);
}

static void v_free(void *x)
{
	vector_free(x);
}

int
cli_list_config_local (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	vector hwes;
	int ret;

	condlog(3, "list config local (operator)");

	hwes = get_used_hwes(vecs->pathvec);
	pthread_cleanup_push(v_free, hwes);
	ret = show_config(reply, len, hwes, vecs->mpvec);
	pthread_cleanup_pop(1);
	return ret;
}

int
cli_list_paths (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list paths (operator)");

	return show_paths(reply, len, vecs, PRINT_PATH_CHECKER, 1);
}

int
cli_list_paths_fmt (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * fmt = get_keyparam(v, FMT);

	condlog(3, "list paths (operator)");

	return show_paths(reply, len, vecs, fmt, 1);
}

int
cli_list_paths_raw (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * fmt = get_keyparam(v, FMT);

	condlog(3, "list paths (operator)");

	return show_paths(reply, len, vecs, fmt, 0);
}

int
cli_list_path (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, PATH);
	struct path *pp;

	param = convert_dev(param, 1);
	condlog(3, "%s: list path (operator)", param);

	pp = find_path_by_dev(vecs->pathvec, param);
	if (!pp)
		return 1;

	return show_path(reply, len, vecs, pp, "%o");
}

int
cli_list_map_topology (void * v, char ** reply, int * len, void * data)
{
	struct multipath * mpp;
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, MAP);

	param = convert_dev(param, 0);
	get_path_layout(vecs->pathvec, 0);
	mpp = find_mp_by_str(vecs->mpvec, param);

	if (!mpp)
		return 1;

	condlog(3, "list multipath %s (operator)", param);

	return show_map_topology(reply, len, mpp, vecs);
}

int
cli_list_maps_topology (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list multipaths (operator)");

	return show_maps_topology(reply, len, vecs);
}

int
cli_list_map_json (void * v, char ** reply, int * len, void * data)
{
	struct multipath * mpp;
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, MAP);

	param = convert_dev(param, 0);
	get_path_layout(vecs->pathvec, 0);
	mpp = find_mp_by_str(vecs->mpvec, param);

	if (!mpp)
		return 1;

	condlog(3, "list multipath json %s (operator)", param);

	return show_map_json(reply, len, mpp, vecs);
}

int
cli_list_maps_json (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list multipaths json (operator)");

	return show_maps_json(reply, len, vecs);
}

int
cli_list_wildcards (void * v, char ** reply, int * len, void * data)
{
	char * c;

	*reply = MALLOC(INITIAL_REPLY_LEN);

	if (!*reply)
		return 1;

	c = *reply;
	c += snprint_wildcards(c, INITIAL_REPLY_LEN);

	*len = INITIAL_REPLY_LEN;
	return 0;
}

int
show_status (char ** r, int *len, struct vectors * vecs)
{
	char * c;
	char * reply;

	unsigned int maxlen = INITIAL_REPLY_LEN;
	reply = MALLOC(maxlen);

	if (!reply)
		return 1;

	c = reply;
	c += snprint_status(c, reply + maxlen - c, vecs);

	*r = reply;
	*len = (int)(c - reply + 1);
	return 0;
}

int
show_daemon (char ** r, int *len)
{
	char * c;
	char * reply;

	unsigned int maxlen = INITIAL_REPLY_LEN;
	reply = MALLOC(maxlen);

	if (!reply)
		return 1;

	c = reply;
	c += snprintf(c, INITIAL_REPLY_LEN, "pid %d %s\n",
		      daemon_pid, daemon_status());

	*r = reply;
	*len = (int)(c - reply + 1);
	return 0;
}

int
show_map (char ** r, int *len, struct multipath * mpp, char * style,
	  int pretty)
{
	char * c;
	char * reply;
	unsigned int maxlen = INITIAL_REPLY_LEN;
	int again = 1;

	reply = MALLOC(maxlen);
	while (again) {
		if (!reply)
			return 1;

		c = reply;
		c += snprint_multipath(c, reply + maxlen - c, style,
				       mpp, pretty);

		again = ((c - reply) == (maxlen - 1));

		REALLOC_REPLY(reply, again, maxlen);
	}
	*r = reply;
	*len = (int)(c - reply + 1);
	return 0;
}

int
show_maps (char ** r, int *len, struct vectors * vecs, char * style,
	   int pretty)
{
	int i;
	struct multipath * mpp;
	char * c, *header;
	char * reply;
	unsigned int maxlen = INITIAL_REPLY_LEN;
	int again = 1;

	get_multipath_layout(vecs->mpvec, 1);
	foreign_multipath_layout();

	reply = MALLOC(maxlen);

	while (again) {
		if (!reply)
			return 1;

		c = reply;
		if (pretty)
			c += snprint_multipath_header(c, reply + maxlen - c,
						      style);
		header = c;

		vector_foreach_slot(vecs->mpvec, mpp, i) {
			if (update_multipath(vecs, mpp->alias, 0)) {
				i--;
				continue;
			}
			c += snprint_multipath(c, reply + maxlen - c,
					       style, mpp, pretty);

		}
		c += snprint_foreign_multipaths(c, reply + maxlen - c,
						style, pretty);
		again = ((c - reply) == (maxlen - 1));

		REALLOC_REPLY(reply, again, maxlen);
	}

	if (pretty && c == header) {
		/* No output - clear header */
		*reply = '\0';
		c = reply;
	}
	*r = reply;
	*len = (int)(c - reply + 1);
	return 0;
}

int
cli_list_maps_fmt (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * fmt = get_keyparam(v, FMT);

	condlog(3, "list maps (operator)");

	return show_maps(reply, len, vecs, fmt, 1);
}

int
cli_list_maps_raw (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * fmt = get_keyparam(v, FMT);

	condlog(3, "list maps (operator)");

	return show_maps(reply, len, vecs, fmt, 0);
}

int
cli_list_map_fmt (void * v, char ** reply, int * len, void * data)
{
	struct multipath * mpp;
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, MAP);
	char * fmt = get_keyparam(v, FMT);

	param = convert_dev(param, 0);
	get_path_layout(vecs->pathvec, 0);
	get_multipath_layout(vecs->mpvec, 1);
	mpp = find_mp_by_str(vecs->mpvec, param);
	if (!mpp)
		return 1;

	condlog(3, "list map %s fmt %s (operator)", param, fmt);

	return show_map(reply, len, mpp, fmt, 1);
}

int
cli_list_map_raw (void * v, char ** reply, int * len, void * data)
{
	struct multipath * mpp;
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, MAP);
	char * fmt = get_keyparam(v, FMT);

	param = convert_dev(param, 0);
	get_path_layout(vecs->pathvec, 0);
	get_multipath_layout(vecs->mpvec, 1);
	mpp = find_mp_by_str(vecs->mpvec, param);
	if (!mpp)
		return 1;

	condlog(3, "list map %s fmt %s (operator)", param, fmt);

	return show_map(reply, len, mpp, fmt, 0);
}

int
cli_list_maps (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list maps (operator)");

	return show_maps(reply, len, vecs, PRINT_MAP_NAMES, 1);
}

int
cli_list_status (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list status (operator)");

	return show_status(reply, len, vecs);
}

int
cli_list_maps_status (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list maps status (operator)");

	return show_maps(reply, len, vecs, PRINT_MAP_STATUS, 1);
}

int
cli_list_maps_stats (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list maps stats (operator)");

	return show_maps(reply, len, vecs, PRINT_MAP_STATS, 1);
}

int
cli_list_daemon (void * v, char ** reply, int * len, void * data)
{
	condlog(3, "list daemon (operator)");

	return show_daemon(reply, len);
}

int
cli_reset_maps_stats (void * v, char ** reply, int * len, void * data)
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

int
cli_reset_map_stats (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	struct multipath * mpp;
	char * param = get_keyparam(v, MAP);

	param = convert_dev(param, 0);
	mpp = find_mp_by_str(vecs->mpvec, param);

	if (!mpp)
		return 1;

	condlog(3, "reset multipath %s stats (operator)", param);
	reset_stats(mpp);
	return 0;
}

int
cli_add_path (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, PATH);
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
	if (pp) {
		condlog(2, "%s: path already in pathvec", param);
		if (pp->mpp)
			return 0;
	} else {
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
	*reply = strdup("blacklisted\n");
	*len = strlen(*reply) + 1;
	condlog(2, "%s: path blacklisted", param);
	return 0;
}

int
cli_del_path (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, PATH);
	struct path *pp;

	param = convert_dev(param, 1);
	condlog(2, "%s: remove path (operator)", param);
	pp = find_path_by_dev(vecs->pathvec, param);
	if (!pp) {
		condlog(0, "%s: path already removed", param);
		return 1;
	}
	return ev_remove_path(pp, vecs, 1);
}

int
cli_add_map (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, MAP);
	int major, minor;
	char dev_path[PATH_SIZE];
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
		*reply = strdup("blacklisted\n");
		*len = strlen(*reply) + 1;
		condlog(2, "%s: map blacklisted", param);
		return 1;
	}
	do {
		if (dm_get_major_minor(param, &major, &minor) < 0)
			condlog(2, "%s: not a device mapper table", param);
		else {
			sprintf(dev_path, "dm-%d", minor);
			alias = dm_mapname(major, minor);
		}
		/*if there is no mapname found, we first create the device*/
		if (!alias && !count) {
			condlog(2, "%s: mapname not found for %d:%d",
				param, major, minor);
			get_refwwid(CMD_NONE, param, DEV_DEVMAP,
				    vecs->pathvec, &refwwid);
			if (refwwid) {
				if (coalesce_paths(vecs, NULL, refwwid,
						   FORCE_RELOAD_NONE, CMD_NONE)
				    != CP_OK)
					condlog(2, "%s: coalesce_paths failed",
									param);
				dm_lib_release();
				FREE(refwwid);
			}
		} /*we attempt to create device only once*/
		count++;
	} while (!alias && (count < 2));

	if (!alias) {
		condlog(2, "%s: add map failed", param);
		return 1;
	}
	rc = ev_add_map(dev_path, alias, vecs);
	FREE(alias);
	return rc;
}

int
cli_del_map (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, MAP);
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
	FREE(alias);
	return rc;
}

int
cli_reload(void *v, char **reply, int *len, void *data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * mapname = get_keyparam(v, MAP);
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

	return update_path_groups(mpp, vecs, 0);
}

int resize_map(struct multipath *mpp, unsigned long long size,
	       struct vectors * vecs)
{
	char params[PARAMS_SIZE] = {0};
	unsigned long long orig_size = mpp->size;

	mpp->size = size;
	update_mpp_paths(mpp, vecs->pathvec);
	if (setup_map(mpp, params, PARAMS_SIZE, vecs) != 0) {
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

int
cli_resize(void *v, char **reply, int *len, void *data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * mapname = get_keyparam(v, MAP);
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

	dm_lib_release();
	if (setup_multipath(vecs, mpp) != 0)
		return 1;
	sync_map_state(mpp);

	return 0;
}

int
cli_force_no_daemon_q(void * v, char ** reply, int * len, void * data)
{
	struct config *conf;

	condlog(2, "force queue_without_daemon (operator)");
	conf = get_multipath_config();
	if (conf->queue_without_daemon == QUE_NO_DAEMON_OFF)
		conf->queue_without_daemon = QUE_NO_DAEMON_FORCE;
	put_multipath_config(conf);
	return 0;
}

int
cli_restore_no_daemon_q(void * v, char ** reply, int * len, void * data)
{
	struct config *conf;

	condlog(2, "restore queue_without_daemon (operator)");
	conf = get_multipath_config();
	if (conf->queue_without_daemon == QUE_NO_DAEMON_FORCE)
		conf->queue_without_daemon = QUE_NO_DAEMON_OFF;
	put_multipath_config(conf);
	return 0;
}

int
cli_restore_queueing(void *v, char **reply, int *len, void *data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * mapname = get_keyparam(v, MAP);
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

	if (mpp->no_path_retry != NO_PATH_RETRY_UNDEF &&
			mpp->no_path_retry != NO_PATH_RETRY_FAIL) {
		dm_queue_if_no_path(mpp->alias, 1);
		if (mpp->no_path_retry > 0) {
			if (mpp->nr_active > 0)
				mpp->retry_tick = 0;
			else
				enter_recovery_mode(mpp);
		}
	}
	return 0;
}

int
cli_restore_all_queueing(void *v, char **reply, int *len, void *data)
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
		if (mpp->no_path_retry != NO_PATH_RETRY_UNDEF &&
		    mpp->no_path_retry != NO_PATH_RETRY_FAIL) {
			dm_queue_if_no_path(mpp->alias, 1);
			if (mpp->no_path_retry > 0) {
				if (mpp->nr_active > 0)
					mpp->retry_tick = 0;
				else
					enter_recovery_mode(mpp);
			}
		}
	}
	return 0;
}

int
cli_disable_queueing(void *v, char **reply, int *len, void *data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * mapname = get_keyparam(v, MAP);
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

	if (mpp->nr_active == 0)
		mpp->stat_map_failures++;
	mpp->retry_tick = 0;
	mpp->no_path_retry = NO_PATH_RETRY_FAIL;
	mpp->disable_queueing = 1;
	dm_queue_if_no_path(mpp->alias, 0);
	return 0;
}

int
cli_disable_all_queueing(void *v, char **reply, int *len, void *data)
{
	struct vectors * vecs = (struct vectors *)data;
	struct multipath *mpp;
	int i;

	condlog(2, "disable queueing (operator)");
	vector_foreach_slot(vecs->mpvec, mpp, i) {
		if (mpp->nr_active == 0)
			mpp->stat_map_failures++;
		mpp->retry_tick = 0;
		mpp->no_path_retry = NO_PATH_RETRY_FAIL;
		mpp->disable_queueing = 1;
		dm_queue_if_no_path(mpp->alias, 0);
	}
	return 0;
}

int
cli_switch_group(void * v, char ** reply, int * len, void * data)
{
	char * mapname = get_keyparam(v, MAP);
	int groupnum = atoi(get_keyparam(v, GROUP));

	mapname = convert_dev(mapname, 0);
	condlog(2, "%s: switch to path group #%i (operator)", mapname, groupnum);

	return dm_switchgroup(mapname, groupnum);
}

int
cli_reconfigure(void * v, char ** reply, int * len, void * data)
{
	int rc;

	condlog(2, "reconfigure (operator)");

	rc = set_config_state(DAEMON_CONFIGURE); 
	if (rc == ETIMEDOUT) {
		condlog(2, "timeout starting reconfiguration");
		return 1;
	} else if (rc == EINVAL)
		/* daemon shutting down */
		return 1;
	return 0;
}

int
cli_suspend(void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, MAP);
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

int
cli_resume(void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, MAP);
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

int
cli_reinstate(void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, PATH);
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

int
cli_reassign (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, MAP);
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

int
cli_fail(void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, PATH);
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

int
show_blacklist (char ** r, int * len)
{
	char *c = NULL;
	char *reply = NULL;
	unsigned int maxlen = INITIAL_REPLY_LEN;
	int again = 1;
	struct config *conf;
	int fail = 0;

	reply = MALLOC(maxlen);

	conf = get_multipath_config();
	pthread_cleanup_push(put_multipath_config, conf);
	while (again) {
		if (!reply) {
			fail = 1;
			break;
		}

		c = reply;
		c += snprint_blacklist_report(conf, c, maxlen);
		again = ((c - reply) == maxlen);
		REALLOC_REPLY(reply, again, maxlen);
	}
	pthread_cleanup_pop(1);

	if (fail)
		return 1;
	*r = reply;
	*len = (int)(c - reply + 1);
	return 0;
}

int
cli_list_blacklist (void * v, char ** reply, int * len, void * data)
{
	condlog(3, "list blacklist (operator)");

	return show_blacklist(reply, len);
}

int
show_devices (char ** r, int * len, struct vectors *vecs)
{
	char *c = NULL;
	char *reply = NULL;
	unsigned int maxlen = INITIAL_REPLY_LEN;
	int again = 1;
	struct config *conf;
	int fail = 0;

	reply = MALLOC(maxlen);

	conf = get_multipath_config();
	pthread_cleanup_push(put_multipath_config, conf);
	while (again) {
		if (!reply) {
			fail = 1;
			break;
		}

		c = reply;
		c += snprint_devices(conf, c, maxlen, vecs);
		again = ((c - reply) == maxlen);
		REALLOC_REPLY(reply, again, maxlen);
	}
	pthread_cleanup_pop(1);

	if (fail)
		return 1;
	*r = reply;
	*len = (int)(c - reply + 1);

	return 0;
}

int
cli_list_devices (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list devices (operator)");

	return show_devices(reply, len, vecs);
}

int
cli_quit (void * v, char ** reply, int * len, void * data)
{
	return 0;
}

int
cli_shutdown (void * v, char ** reply, int * len, void * data)
{
	condlog(3, "shutdown (operator)");
	exit_daemon();
	return 0;
}

int
cli_getprstatus (void * v, char ** reply, int * len, void * data)
{
	struct multipath * mpp;
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, MAP);

	param = convert_dev(param, 0);
	get_path_layout(vecs->pathvec, 0);
	mpp = find_mp_by_str(vecs->mpvec, param);

	if (!mpp)
		return 1;

	condlog(3, "%s: prflag = %u", param, (unsigned int)mpp->prflag);

	*len = asprintf(reply, "%d", mpp->prflag);
	if (*len < 0)
		return 1;

	condlog(3, "%s: reply = %s", param, *reply);

	return 0;
}

int
cli_setprstatus(void * v, char ** reply, int * len, void * data)
{
	struct multipath * mpp;
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, MAP);

	param = convert_dev(param, 0);
	get_path_layout(vecs->pathvec, 0);
	mpp = find_mp_by_str(vecs->mpvec, param);

	if (!mpp)
		return 1;

	if (!mpp->prflag) {
		mpp->prflag = 1;
		condlog(2, "%s: prflag set", param);
	}


	return 0;
}

int
cli_unsetprstatus(void * v, char ** reply, int * len, void * data)
{
	struct multipath * mpp;
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, MAP);

	param = convert_dev(param, 0);
	get_path_layout(vecs->pathvec, 0);
	mpp = find_mp_by_str(vecs->mpvec, param);

	if (!mpp)
		return 1;

	if (mpp->prflag) {
		mpp->prflag = 0;
		condlog(2, "%s: prflag unset", param);
	}

	return 0;
}

int
cli_getprkey(void * v, char ** reply, int * len, void * data)
{
	struct multipath * mpp;
	struct vectors * vecs = (struct vectors *)data;
	char *mapname = get_keyparam(v, MAP);
	char *flagstr = "";

	mapname = convert_dev(mapname, 0);
	condlog(3, "%s: get persistent reservation key (operator)", mapname);
	mpp = find_mp_by_str(vecs->mpvec, mapname);

	if (!mpp)
		return 1;

	*reply = malloc(26);

	if (!get_be64(mpp->reservation_key)) {
		sprintf(*reply, "none\n");
		*len = strlen(*reply) + 1;
		return 0;
	}
	if (mpp->sa_flags & MPATH_F_APTPL_MASK)
		flagstr = ":aptpl";
	snprintf(*reply, 26, "0x%" PRIx64 "%s\n",
		 get_be64(mpp->reservation_key), flagstr);
	(*reply)[19] = '\0';
	*len = strlen(*reply) + 1;
	return 0;
}

int
cli_unsetprkey(void * v, char ** reply, int * len, void * data)
{
	struct multipath * mpp;
	struct vectors * vecs = (struct vectors *)data;
	char *mapname = get_keyparam(v, MAP);
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

int
cli_setprkey(void * v, char ** reply, int * len, void * data)
{
	struct multipath * mpp;
	struct vectors * vecs = (struct vectors *)data;
	char *mapname = get_keyparam(v, MAP);
	char *keyparam = get_keyparam(v, KEY);
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

int cli_set_marginal(void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, PATH);
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

	return update_path_groups(pp->mpp, vecs, 0);
}

int cli_unset_marginal(void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, PATH);
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

	return update_path_groups(pp->mpp, vecs, 0);
}

int cli_unset_all_marginal(void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * mapname = get_keyparam(v, MAP);
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

	return update_path_groups(mpp, vecs, 0);
}
