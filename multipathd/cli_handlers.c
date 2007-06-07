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
#include <config.h>
#include <configure.h>
#include <blacklist.h>
#include <debug.h>
#include <print.h>
#include <sysfs.h>

#include "main.h"
#include "cli.h"

int
show_paths (char ** r, int * len, struct vectors * vecs, char * style)
{
	int i;
	struct path * pp;
	char * c;
	char * reply;
	unsigned int maxlen = INITIAL_REPLY_LEN;
	int again = 1;

	get_path_layout(vecs->pathvec);
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

		if (again)
			reply = REALLOC(reply, maxlen *= 2);

	}
	*r = reply;
	*len = (int)(c - reply + 1);
	return 0;
}

int
show_map_topology (char ** r, int * len, struct multipath * mpp)
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

		c += snprint_multipath_topology(c, reply + maxlen - c, mpp, 2);
		again = ((c - reply) == (maxlen - 1));

		if (again)
			reply = REALLOC(reply, maxlen *= 2);

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
 
	get_path_layout(vecs->pathvec);
	reply = MALLOC(maxlen);

	while (again) {
		if (!reply)
			return 1;

		c = reply;

		vector_foreach_slot(vecs->mpvec, mpp, i)
			c += snprint_multipath_topology(c, reply + maxlen - c,
							mpp, 2);

		again = ((c - reply) == (maxlen - 1));

		if (again)
			reply = REALLOC(reply, maxlen *= 2);

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
			reply = REALLOC(reply, maxlen *= 2);
			continue;
		}
		c += snprint_blacklist(c, reply + maxlen - c);
		again = ((c - reply) == maxlen);
		if (again) {
			reply = REALLOC(reply, maxlen *= 2);
			continue;
		}
		c += snprint_blacklist_except(c, reply + maxlen - c);
		again = ((c - reply) == maxlen);
		if (again) {
			reply = REALLOC(reply, maxlen *= 2);
			continue;
		}
		c += snprint_hwtable(c, reply + maxlen - c, conf->hwtable);
		again = ((c - reply) == maxlen);
		if (again) {
			reply = REALLOC(reply, maxlen *= 2);
			continue;
		}
		c += snprint_mptable(c, reply + maxlen - c, conf->mptable);
		again = ((c - reply) == maxlen);
		if (again)
			reply = REALLOC(reply, maxlen *= 2);
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
cli_list_map_topology (void * v, char ** reply, int * len, void * data)
{
	struct multipath * mpp;
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, MAP);
	
	get_path_layout(vecs->pathvec);
	mpp = find_mp_by_str(vecs->mpvec, param);

	if (!mpp)
		return 1;

	condlog(3, "list multipath %s (operator)", param);

	return show_map_topology(reply, len, mpp);
}

int
cli_list_maps_topology (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list multipaths (operator)");

	return show_maps_topology(reply, len, vecs);
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

	get_multipath_layout(vecs->mpvec);
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

		if (again)
			reply = REALLOC(reply, maxlen *= 2);
	}
	*r = reply;
	*len = (int)(c - reply + 1);
	return 0;
}

int
cli_list_maps (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list maps (operator)");

	return show_maps(reply, len, vecs, PRINT_MAP_NAMES);
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
cli_add_path (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, PATH);
	int r;

	condlog(2, "%s: add path (operator)", param);

	if (filter_devnode(conf->blist_devnode, conf->elist_devnode,
	    param) > 0 || (r = ev_add_path(param, vecs)) == 2) {
		*reply = strdup("blacklisted");
		*len = strlen(*reply) + 1;
		condlog(2, "%s: path blacklisted", param);
		return 0;
	}
	return r;
}

int
cli_del_path (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, PATH);

	condlog(2, "%s: remove path (operator)", param);

	return ev_remove_path(param, vecs);
}

int
cli_add_map (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, MAP);
	int minor;
	char dev_path[PATH_SIZE];
	struct sysfs_device *sysdev;

	condlog(2, "%s: add map (operator)", param);

	if (filter_wwid(conf->blist_wwid, conf->elist_wwid, param) > 0) {
		*reply = strdup("blacklisted");
		*len = strlen(*reply) + 1;
		condlog(2, "%s: map blacklisted", param);
		return 0;
	}
	minor = dm_get_minor(param);
	if (minor < 0) {
		condlog(2, "%s: not a device mapper table", param);
		return 0;
	}
	sprintf(dev_path,"/block/dm-%d", minor);
	sysdev = sysfs_device_get(dev_path);
	if (!sysdev) {
		condlog(2, "%s: not found in sysfs", param);
		return 0;
	}
	return ev_add_map(sysdev, vecs);
}

int
cli_del_map (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, MAP);

	condlog(2, "%s: remove map (operator)", param);

	return ev_remove_map(param, vecs);
}

int
cli_switch_group(void * v, char ** reply, int * len, void * data)
{
	char * mapname = get_keyparam(v, MAP);
	int groupnum = atoi(get_keyparam(v, GROUP));
	
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
	int r = dm_simplecmd(DM_DEVICE_SUSPEND, param);

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
	int r = dm_simplecmd(DM_DEVICE_RESUME, param);

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
	
	pp = find_path_by_dev(vecs->pathvec, param);

	if (!pp)
		 pp = find_path_by_devt(vecs->pathvec, param);

	if (!pp || !pp->mpp || !pp->mpp->alias)
		return 1;

	condlog(2, "%s: reinstate path %s (operator)",
		pp->mpp->alias, pp->dev_t);

	return dm_reinstate_path(pp->mpp->alias, pp->dev_t);
}

int
cli_fail(void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, PATH);
	struct path * pp;
	
	pp = find_path_by_dev(vecs->pathvec, param);

	if (!pp)
		 pp = find_path_by_devt(vecs->pathvec, param);

	if (!pp || !pp->mpp || !pp->mpp->alias)
		return 1;

	condlog(2, "%s: fail path %s (operator)",
		pp->mpp->alias, pp->dev_t);

	return dm_fail_path(pp->mpp->alias, pp->dev_t);
}

int
show_blacklist (char ** r, int * len)
{
        char *c = NULL;
        char *reply = NULL;
        unsigned int maxlen = INITIAL_REPLY_LEN;
        int again = 1;

        while (again) {
		reply = MALLOC(maxlen);
		if (!reply)
			return 1;

                c = reply;
                c += snprint_blacklist_report(c, maxlen);
                again = ((c - reply) == maxlen);
                if (again) {
			maxlen  *= 2;
			FREE(reply);
                        continue;
                }
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

        while (again) {
                reply = MALLOC(maxlen);
                if (!reply)
                        return 1;

                c = reply;
                c += snprint_devices(c, maxlen, vecs);
                again = ((c - reply) == maxlen);
                if (again) {
                        maxlen  *= 2;
                        FREE(reply);
                        continue;
                }
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
