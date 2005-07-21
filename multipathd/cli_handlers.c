#include <memory.h>
#include <vector.h>
#include <structs.h>
#include <devmapper.h>

#include "main.h"
#include "cli.h"

int
cli_list_paths (void * v, char ** reply, int * len, void * data)
{
	struct paths * allpaths = (struct paths *)data;

	return show_paths(reply, len, allpaths);
}

int
cli_list_maps (void * v, char ** reply, int * len, void * data)
{
	struct paths * allpaths = (struct paths *)data;

	return show_maps(reply, len, allpaths);
}

int
cli_add_path (void * v, char ** reply, int * len, void * data)
{
	struct paths * allpaths = (struct paths *)data;
	char * param = get_keyparam(v, PATH);

	return uev_add_path(param, allpaths);
}

int
cli_del_path (void * v, char ** reply, int * len, void * data)
{
	struct paths * allpaths = (struct paths *)data;
	char * param = get_keyparam(v, PATH);

	return uev_remove_path(param, allpaths);
}

int
cli_add_map (void * v, char ** reply, int * len, void * data)
{
	struct paths * allpaths = (struct paths *)data;
	char * param = get_keyparam(v, MAP);

	return uev_add_map(param, allpaths);
}

int
cli_del_map (void * v, char ** reply, int * len, void * data)
{
	struct paths * allpaths = (struct paths *)data;
	char * param = get_keyparam(v, MAP);

	return uev_remove_map(param, allpaths);
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
	struct paths * allpaths = (struct paths *)data;
			
	return dump_pathvec(reply, len, allpaths);
}
