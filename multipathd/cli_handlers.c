/*
 * Copyright (c) 2005 Christophe Varoqui
 */
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

#include "main.h"
#include "cli.h"

int
cli_list_paths (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list paths (operator)");

	return show_paths(reply, len, vecs, PRINT_PATH_CHECKER);
}

int
cli_list_maps (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list maps (operator)");

	return show_maps(reply, len, vecs, PRINT_MAP_FAILBACK);
}

int
cli_list_maps_stats (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;

	condlog(3, "list maps (operator)");

	return show_maps(reply, len, vecs, PRINT_MAP_STATS);
}

int
cli_add_path (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, PATH);

	condlog(2, "%s: add path (operator)", param);

	if (blacklist(conf->blist, param)) {
		*reply = strdup("blacklisted");
		*len = strlen(*reply) + 1;
		condlog(2, "%s: path blacklisted", param);
		return 0;
	}
	return ev_add_path(param, vecs);
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

	condlog(2, "%s: add map (operator)", param);

	return ev_add_map(param, vecs);
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
