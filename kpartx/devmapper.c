/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libdevmapper.h>
#include <ctype.h>
#include <linux/kdev_t.h>
#include <errno.h>
#include "devmapper.h"

#define UUID_PREFIX "part%d-"
#define MAX_PREFIX_LEN 8

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
dm_simplecmd (int task, const char *name, int no_flush) {
	int r = 0;
	struct dm_task *dmt;

	if (!(dmt = dm_task_create(task)))
		return 0;

	if (!dm_task_set_name(dmt, name))
		goto out;

	dm_task_no_open_count(dmt);
	dm_task_skip_lockfs(dmt);

	if (no_flush)
		dm_task_no_flush(dmt);

	r = dm_task_run(dmt);

	out:
	dm_task_destroy(dmt);
	return r;
}

extern int
dm_addmap (int task, const char *name, const char *target,
	   const char *params, uint64_t size, const char *uuid, int part) {
	int r = 0;
	struct dm_task *dmt;
	char *prefixed_uuid = NULL;

	if (!(dmt = dm_task_create (task)))
		return 0;

	if (!dm_task_set_name (dmt, name))
		goto addout;

	if (!dm_task_add_target (dmt, 0, size, target, params))
		goto addout;

	if (task == DM_DEVICE_CREATE && uuid) {
		prefixed_uuid = malloc(MAX_PREFIX_LEN + strlen(uuid) + 1);
		if (!prefixed_uuid) {
			fprintf(stderr, "cannot create prefixed uuid : %s\n",
				strerror(errno));
			goto addout;
		}
		sprintf(prefixed_uuid, UUID_PREFIX "%s", part, uuid);
		if (!dm_task_set_uuid(dmt, prefixed_uuid))
			goto addout;
	}

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


char *
dm_mapname(int major, int minor)
{
	struct dm_task *dmt;
	char *mapname = NULL;
	const char *map;

	if (!(dmt = dm_task_create(DM_DEVICE_INFO)))
		return NULL;

	dm_task_no_open_count(dmt);
	dm_task_set_major(dmt, major);
	dm_task_set_minor(dmt, minor);

	if (!dm_task_run(dmt))
		goto out;

	map = dm_task_get_name(dmt);
	if (map && strlen(map))
		mapname = strdup(map);

out:
	dm_task_destroy(dmt);
	return mapname;
}

/*
 * dm_get_first_dep
 *
 * Return the device number of the first dependend device
 * for a given target.
 */
dev_t dm_get_first_dep(char *devname)
{
	struct dm_task *dmt;
	struct dm_deps *dm_deps;
	dev_t ret = 0;

	if ((dmt = dm_task_create(DM_DEVICE_DEPS)) == NULL) {
		return ret;
	}
	if (!dm_task_set_name(dmt, devname)) {
		goto out;
	}
	if (!dm_task_run(dmt)) {
		goto out;
	}
	if ((dm_deps = dm_task_get_deps(dmt)) == NULL) {
		goto out;
	}
	if (dm_deps->count > 0) {
		ret = dm_deps->device[0];
	}
out:
	dm_task_destroy(dmt);

	return ret;
}

char *
dm_mapuuid(int major, int minor)
{
	struct dm_task *dmt;
	const char *tmp;
	char *uuid = NULL;

	if (!(dmt = dm_task_create(DM_DEVICE_INFO)))
		return NULL;

	dm_task_no_open_count(dmt);
	dm_task_set_major(dmt, major);
	dm_task_set_minor(dmt, minor);

	if (!dm_task_run(dmt))
		goto out;

	tmp = dm_task_get_uuid(dmt);
	if (tmp[0] != '\0')
		uuid = strdup(tmp);
out:
	dm_task_destroy(dmt);
	return uuid;
}

int
dm_devn (char * mapname, int *major, int *minor)
{
	int r = 1;
	struct dm_task *dmt;
	struct dm_info info;

	if (!(dmt = dm_task_create(DM_DEVICE_INFO)))
		return 0;

	if (!dm_task_set_name(dmt, mapname))
		goto out;

	if (!dm_task_run(dmt))
		goto out;

	if (!dm_task_get_info(dmt, &info))
		goto out;

	*major = info.major;
	*minor = info.minor;

	r = 0;
out:
	dm_task_destroy(dmt);
	return r;
}

