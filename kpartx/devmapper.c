/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libdevmapper.h>
#include <ctype.h>
#include <linux/kdev_t.h>

extern int
dm_prereq (char * str, int x, int y, int z)
{
	int r = 1;
	struct dm_task *dmt;
	struct dm_versions *target;
	struct dm_versions *last_target;

	if (!(dmt = dm_task_create(DM_DEVICE_LIST_VERSIONS)))
		return 1;

	dm_task_no_open_count(dmt);

	if (!dm_task_run(dmt))
		goto out;

	target = dm_task_get_versions(dmt);

	/* Fetch targets and print 'em */
	do {
		last_target = target;

		if (!strncmp(str, target->name, strlen(str)) &&
		    /* dummy prereq on multipath version */
		    target->version[0] >= x &&
		    target->version[1] >= y &&
		    target->version[2] >= z
		   )
			r = 0;

		target = (void *) target + target->next;
	} while (last_target != target);

	out:
	dm_task_destroy(dmt);
	return r;
}

extern int
dm_simplecmd (int task, const char *name) {
	int r = 0;
	struct dm_task *dmt;

	if (!(dmt = dm_task_create(task)))
		return 0;

	if (!dm_task_set_name(dmt, name))
		goto out;

	dm_task_no_open_count(dmt);

	r = dm_task_run(dmt);

	out:
	dm_task_destroy(dmt);
	return r;
}

extern int
dm_addmap (int task, const char *name, const char *target,
	   const char *params, unsigned long size) {
	int r = 0;
	struct dm_task *dmt;

	if (!(dmt = dm_task_create (task)))
		return 0;

	if (!dm_task_set_name (dmt, name))
		goto addout;

	if (!dm_task_add_target (dmt, 0, size, target, params))
		goto addout;

	dm_task_no_open_count(dmt);

	r = dm_task_run (dmt);

	addout:
	dm_task_destroy (dmt);
	return r;
}

extern int
dm_map_present (char * str)
{
	int r = 0;
	struct dm_task *dmt;
	struct dm_info info;

	if (!(dmt = dm_task_create(DM_DEVICE_INFO)))
		return 0;

	if (!dm_task_set_name(dmt, str))
		goto out;

	dm_task_no_open_count(dmt);

	if (!dm_task_run(dmt))
		goto out;

	if (!dm_task_get_info(dmt, &info))
		goto out;

	if (info.exists)
		r = 1;
out:
	dm_task_destroy(dmt);
	return r;
}


const char *
dm_mapname(int major, int minor)
{
	struct dm_task *dmt;
	const char *mapname;

	if (!(dmt = dm_task_create(DM_DEVICE_INFO)))
		return NULL;

	dm_task_no_open_count(dmt);
	dm_task_set_major(dmt, major);
	dm_task_set_minor(dmt, minor);

	if (!dm_task_run(dmt))
		goto out;

	mapname = strdup(dm_task_get_name(dmt));
out:
	dm_task_destroy(dmt);
	return mapname;
}

