#include <memory.h>
#include <vector.h>
#include <structs.h>
#include <libdevmapper.h>
#include <devmapper.h>
#include <config.h>
#include <blacklist.h>

#include "main.h"
#include "cli.h"

int
cli_list_paths (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;

	return show_paths(reply, len, vecs);
}

int
cli_list_maps (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;

	return show_maps(reply, len, vecs);
}

int
cli_add_path (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, PATH);

	if (blacklist(conf->blist, param)) {
		*reply = strdup("blacklisted");
		*len = strlen(*reply) + 1;
		return 0;
	}
	return uev_add_path(param, vecs);
}

int
cli_del_path (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, PATH);

	return uev_remove_path(param, vecs);
}

int
cli_add_map (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, MAP);

	return uev_add_map(param, vecs);
}

int
cli_del_map (void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, MAP);

	return uev_remove_map(param, vecs);
}

int
cli_switch_group(void * v, char ** reply, int * len, void * data)
{
	char * mapname = get_keyparam(v, MAP);
	int groupnum = atoi(get_keyparam(v, GROUP));
	
	return dm_switchgroup(mapname, groupnum);
}

int
cli_dump_pathvec(void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
			
	return dump_pathvec(reply, len, vecs);
}

int
cli_reconfigure(void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
			
	return reconfigure(vecs);
}

int
cli_suspend(void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, MAP);
	int r = dm_simplecmd(DM_DEVICE_SUSPEND, param);

	if (!r) /* error */
		return 1;
	
	struct multipath * mpp = find_mp_by_alias(vecs->mpvec, param);

	if (!mpp)
		return 1;
	
	mpp->dmstate = MAPSTATE_SUSPENDED;
	return 0;
}

int
cli_resume(void * v, char ** reply, int * len, void * data)
{
	struct vectors * vecs = (struct vectors *)data;
	char * param = get_keyparam(v, MAP);
	int r = dm_simplecmd(DM_DEVICE_RESUME, param);

	if (!r) /* error */
		return 1;
	
	struct multipath * mpp = find_mp_by_alias(vecs->mpvec, param);

	if (!mpp)
		return 1;
	
	mpp->dmstate = MAPSTATE_ACTIVE;
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

	return dm_reinstate(pp->mpp->alias, pp->dev_t);
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

	return dm_fail_path(pp->mpp->alias, pp->dev_t);
}
