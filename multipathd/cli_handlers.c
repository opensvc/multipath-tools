/*
 * Copyright (c) 2005 Christophe Varoqui
 */
#include <checkers.h>
#include <memory.h>
#include <vector.h>
#include <structs.h>
#include <structs_vec.h>
#include <libdevmapper.h>
#include <devmapper.h>
#include <discovery.h>
#include <config.h>
#include <configure.h>
#include <blacklist.h>
#include <debug.h>
#include <print.h>
#include <sysfs.h>
#include <errno.h>
#include <libudev.h>
#include <util.h>

#include "main.h"
#include "cli.h"
#include "uevent.h"

#define REALLOC_REPLY(r, a, m)					\
	do {							\
		if ((a)) {					\
			(r) = REALLOC((r), (m) * 2);		\
			if ((r)) {				\
				memset((r) + (m), 0, (m));	\
				(m) *= 2;			\
			}					\
		}						\
	} while (0)

int
show_paths (char ** r, int * len, struct vectors * vecs, char * style)
{
	int i;
	struct path * pp;
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

		if (VECTOR_SIZE(vecs->pathvec) > 0)
			c += snprint_path_header(c, reply + maxlen - c,
						 style);

		vector_foreach_slot(vecs->pathvec, pp, i)
			c += snprint_path(c, reply + maxlen - c,
					  style, pp);

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

		again = ((c - reply) == (maxlen - 1));

		REALLOC_REPLY(reply, again, maxlen);
	}
	*r = reply;
	*len = (int)(c - reply + 1);
	return 0;
}

int
show_config (char ** r, int * len)
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
		c += snprint_defaults(c, reply + maxlen - c);
		again = ((c - reply) == maxlen);
		if (again) {
			reply = REALLOC(reply, maxlen * 2);
			if (!reply)
				return 1;
			memset(reply + maxlen, 0, maxlen);
			maxlen *= 2;
			continue;
		}
		c += snprint_blacklist(c, reply + maxlen - c);
		again = ((c - reply) == maxlen);
		if (again) {
			reply = REALLOC(reply, maxlen * 2);
			if (!reply)
				return 1;
			memset(reply + maxlen, 0, maxlen);
			maxlen *= 2;
			continue;
		}
		c += snprint_blacklist_except(c, reply + maxlen - c);
		again = ((c - reply) == maxlen);
		if (again) {
			reply = REALLOC(reply, maxlen * 2);
			if (!reply)
				return 1;
			memset(reply + maxlen, 0, maxlen);
			maxlen *= 2;
			continue;
		}
		c += snprint_hwtable(c, reply + maxlen - c, conf->hwtable);
		again = ((c - reply) == maxlen);
		if (again) {
			reply = REALLOC(reply, maxlen * 2);
			if (!reply)
				return 1;
			memset(reply + maxlen, 0, maxlen);
			maxlen *= 2;
			continue;
		}
		c += snprint_mptable(c, reply + maxlen - c, conf->mptable);
		again = ((c - reply) == maxlen);
		REALLOC_REPLY(reply, again, maxlen);
	}
	*r = reply;
	*len = (int)(c - reply + 1);
	return 0;
}

int
cli_list_config (void * v, char ** reply, int * len, void * data)
{
	condlog(3, "list config (operator)");

	return show_config(reply, len);
}

int
cli_list_paths (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list paths (operator)");

	return show_paths(reply, len, vecs, PRINT_PATH_CHECKER);
}

int
cli_list_paths_fmt (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * fmt = get_keyparam(v, FMT);

	condlog(3, "list paths (operator)");

	return show_paths(reply, len, vecs, fmt);
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
show_maps (char ** r, int *len, struct vectors * vecs, char * style)
{
	int i;
	struct multipath * mpp;
	char * c;
	char * reply;
	unsigned int maxlen = INITIAL_REPLY_LEN;
	int again = 1;

	get_multipath_layout(vecs->mpvec, 1);
	reply = MALLOC(maxlen);

	while (again) {
		if (!reply)
			return 1;

		c = reply;
		if (VECTOR_SIZE(vecs->mpvec) > 0)
			c += snprint_multipath_header(c, reply + maxlen - c,
						      style);

		vector_foreach_slot(vecs->mpvec, mpp, i)
			c += snprint_multipath(c, reply + maxlen - c,
					       style, mpp);

		again = ((c - reply) == (maxlen - 1));

		REALLOC_REPLY(reply, again, maxlen);
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

	return show_maps(reply, len, vecs, fmt);
}

int
cli_list_maps (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list maps (operator)");

	return show_maps(reply, len, vecs, PRINT_MAP_NAMES);
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

	return show_maps(reply, len, vecs, PRINT_MAP_STATUS);
}

int
cli_list_maps_stats (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list maps stats (operator)");

	return show_maps(reply, len, vecs, PRINT_MAP_STATS);
}

int
cli_list_daemon (void * v, char ** reply, int * len, void * data)
{
	condlog(3, "list daemon (operator)");

	return show_daemon(reply, len);
}

int
cli_add_path (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, PATH);
	struct path *pp;
	int r;

	param = convert_dev(param, 1);
	condlog(2, "%s: add path (operator)", param);

	if (filter_devnode(conf->blist_devnode, conf->elist_devnode,
			   param) > 0)
		goto blacklisted;

	pp = find_path_by_dev(vecs->pathvec, param);
	if (pp) {
		condlog(2, "%s: path already in pathvec", param);
		if (pp->mpp)
			return 0;
	} else {
		struct udev_device *udevice;

		udevice = udev_device_new_from_subsystem_sysname(conf->udev,
								 "block",
								 param);
		r = store_pathinfo(vecs->pathvec, conf->hwtable,
				   udevice, DI_ALL, &pp);
		udev_device_unref(udevice);
		if (!pp) {
			if (r == 2)
				goto blacklisted;
			condlog(0, "%s: failed to store path info", param);
			return 1;
		}
		pp->checkint = conf->checkint;
	}
	return ev_add_path(pp, vecs);
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
		return 0;
	}
	return ev_remove_path(pp, vecs);
}

int
cli_add_map (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, MAP);
	int major, minor;
	char dev_path[PATH_SIZE];
	char *alias;
	int rc;

	param = convert_dev(param, 0);
	condlog(2, "%s: add map (operator)", param);

	if (filter_wwid(conf->blist_wwid, conf->elist_wwid, param) > 0) {
		*reply = strdup("blacklisted\n");
		*len = strlen(*reply) + 1;
		condlog(2, "%s: map blacklisted", param);
		return 0;
	}
	minor = dm_get_minor(param);
	if (minor < 0) {
		condlog(2, "%s: not a device mapper table", param);
		return 0;
	}
	major = dm_get_major(param);
	if (major < 0) {
		condlog(2, "%s: not a device mapper table", param);
		return 0;
	}
	sprintf(dev_path,"dm-%d", minor);
	alias = dm_mapname(major, minor);
	if (!alias) {
		condlog(2, "%s: mapname not found for %d:%d",
			param, major, minor);
		return 0;
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
	char dev_path[PATH_SIZE];
	char *alias;
	int rc;

	param = convert_dev(param, 0);
	condlog(2, "%s: remove map (operator)", param);
	minor = dm_get_minor(param);
	if (minor < 0) {
		condlog(2, "%s: not a device mapper table", param);
		return 0;
	}
	major = dm_get_major(param);
	if (major < 0) {
		condlog(2, "%s: not a device mapper table", param);
		return 0;
	}
	sprintf(dev_path,"dm-%d", minor);
	alias = dm_mapname(major, minor);
	if (!alias) {
		condlog(2, "%s: mapname not found for %d:%d",
			param, major, minor);
		return 0;
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

	return reload_map(vecs, mpp, 0);
}

int resize_map(struct multipath *mpp, unsigned long long size,
	       struct vectors * vecs)
{
	char params[PARAMS_SIZE] = {0};
	unsigned long long orig_size = mpp->size;

	mpp->size = size;
	update_mpp_paths(mpp, vecs->pathvec);
	setup_map(mpp, params, PARAMS_SIZE);
	mpp->action = ACT_RESIZE;
	if (domap(mpp, params) <= 0) {
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
	condlog(2, "force queue_without_daemon (operator)");
	if (conf->queue_without_daemon == QUE_NO_DAEMON_OFF)
		conf->queue_without_daemon = QUE_NO_DAEMON_FORCE;
	return 0;
}

int
cli_restore_no_daemon_q(void * v, char ** reply, int * len, void * data)
{
	condlog(2, "restore queue_without_daemon (operator)");
	if (conf->queue_without_daemon == QUE_NO_DAEMON_FORCE)
		conf->queue_without_daemon = QUE_NO_DAEMON_OFF;
	return 0;
}

int
cli_restore_queueing(void *v, char **reply, int *len, void *data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * mapname = get_keyparam(v, MAP);
	struct multipath *mpp;
	int minor;

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

	if (mpp->no_path_retry != NO_PATH_RETRY_UNDEF &&
			mpp->no_path_retry != NO_PATH_RETRY_FAIL) {
		dm_queue_if_no_path(mpp->alias, 1);
		if (mpp->nr_active > 0)
			mpp->retry_tick = 0;
		else
			mpp->retry_tick = mpp->no_path_retry * conf->checkint;
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
		if (mpp->no_path_retry != NO_PATH_RETRY_UNDEF &&
		    mpp->no_path_retry != NO_PATH_RETRY_FAIL) {
			dm_queue_if_no_path(mpp->alias, 1);
			if (mpp->nr_active > 0)
				mpp->retry_tick = 0;
			else
				mpp->retry_tick = mpp->no_path_retry * conf->checkint;
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

	mpp->retry_tick = 0;
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
		mpp->retry_tick = 0;
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
	struct vectors * vecs = (struct vectors *)data;

	condlog(2, "reconfigure (operator)");

	return reconfigure(vecs);
}

int
cli_suspend(void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, MAP);
	int r = dm_simplecmd_noflush(DM_DEVICE_SUSPEND, param);

	param = convert_dev(param, 0);
	condlog(2, "%s: suspend (operator)", param);

	if (!r) /* error */
		return 1;

	struct multipath * mpp = find_mp_by_alias(vecs->mpvec, param);

	if (!mpp)
		return 1;

	dm_get_info(param, &mpp->dmi);
	return 0;
}

int
cli_resume(void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, MAP);
	int r = dm_simplecmd_noflush(DM_DEVICE_RESUME, param);

	param = convert_dev(param, 0);
	condlog(2, "%s: resume (operator)", param);

	if (!r) /* error */
		return 1;

	struct multipath * mpp = find_mp_by_alias(vecs->mpvec, param);

	if (!mpp)
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
	char * param = get_keyparam(v, MAP);

	param = convert_dev(param, 0);
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

	reply = MALLOC(maxlen);

	while (again) {
		if (!reply)
			return 1;

		c = reply;
		c += snprint_blacklist_report(c, maxlen);
		again = ((c - reply) == maxlen);
		REALLOC_REPLY(reply, again, maxlen);
	}

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

	reply = MALLOC(maxlen);

	while (again) {
		if (!reply)
			return 1;

		c = reply;
		c += snprint_devices(c, maxlen, vecs);
		again = ((c - reply) == maxlen);
		REALLOC_REPLY(reply, again, maxlen);
	}

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

	*reply =(char *)malloc(2);
	*len = 2;
	memset(*reply,0,2);


	sprintf(*reply,"%d",mpp->prflag);
	(*reply)[1]='\0';


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
