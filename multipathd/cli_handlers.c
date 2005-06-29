#include <memory.h>
#include <vector.h>
#include <structs.h>
#include <devmapper.h>

#include "main.h"
#include "cli.h"

char *
list_paths (void * v, void * data)
{
	struct paths * allpaths = (struct paths *)data;

	return show_paths(allpaths);
}

char *
list_maps (void * v, void * data)
{
	struct paths * allpaths = (struct paths *)data;

	return show_maps(allpaths);
}

char *
add_path (void * v, void * data)
{
	struct paths * allpaths = (struct paths *)data;
	char * param = get_keyparam(v, PATH);

	if (uev_add_path(param, allpaths))
		return NULL;

	return strdup("ok");
}

char *
del_path (void * v, void * data)
{
	struct paths * allpaths = (struct paths *)data;
	char * param = get_keyparam(v, PATH);

	if (uev_remove_path(param, allpaths))
		return NULL;

	return strdup("ok");
}

char *
add_map (void * v, void * data)
{
	struct paths * allpaths = (struct paths *)data;
	char * param = get_keyparam(v, MAP);

	if (uev_add_map(param, allpaths))
		return NULL;

	return strdup("ok");
}

char *
del_map (void * v, void * data)
{
	struct paths * allpaths = (struct paths *)data;
	char * param = get_keyparam(v, MAP);

	if (uev_remove_map(param, allpaths))
		return NULL;

	return strdup("ok");
}

char *
switch_group(void * v, void * data)
{
	char * mapname = get_keyparam(v, MAP);
	int groupnum = atoi(get_keyparam(v, GROUP));
	
	dm_switchgroup(mapname, groupnum);

	return strdup("ok");
}
