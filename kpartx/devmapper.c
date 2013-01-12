/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libdevmapper.h>
#include <ctype.h>
#include <errno.h>
#include "devmapper.h"

#define UUID_PREFIX "part%d-"
#define MAX_PREFIX_LEN 8
#define PARAMS_SIZE 1024

#ifndef LIBDM_API_COOKIE
static inline int dm_task_set_cookie(struct dm_task *dmt, uint32_t *c, int a)
{
	return 1;
}
#endif

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
dm_simplecmd (int task, const char *name, int no_flush, uint32_t *cookie) {
	int r = 0;
	int udev_wait_flag = (task == DM_DEVICE_RESUME ||
			      task == DM_DEVICE_REMOVE);
	struct dm_task *dmt;

	if (!(dmt = dm_task_create(task)))
		return 0;

	if (!dm_task_set_name(dmt, name))
		goto out;

	dm_task_no_open_count(dmt);
	dm_task_skip_lockfs(dmt);

	if (no_flush)
		dm_task_no_flush(dmt);

	if (udev_wait_flag && !dm_task_set_cookie(dmt, cookie, (udev_sync)? 0 : DM_UDEV_DISABLE_LIBRARY_FALLBACK))
		goto out;
	r = dm_task_run(dmt);

	out:
	dm_task_destroy(dmt);
	return r;
}

extern int
dm_addmap (int task, const char *name, const char *target,
	   const char *params, uint64_t size, int ro, const char *uuid, int part,
	   mode_t mode, uid_t uid, gid_t gid, uint32_t *cookie) {
	int r = 0;
	struct dm_task *dmt;
	char *prefixed_uuid = NULL;

	if (!(dmt = dm_task_create (task)))
		return 0;

	if (!dm_task_set_name (dmt, name))
		goto addout;

	if (!dm_task_add_target (dmt, 0, size, target, params))
		goto addout;

	if (ro && !dm_task_set_ro (dmt))
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

	if (!dm_task_set_mode(dmt, mode))
		goto addout;
	if (!dm_task_set_uid(dmt, uid))
		goto addout;
	if (!dm_task_set_gid(dmt, gid))
		goto addout;

	dm_task_no_open_count(dmt);

	if (task == DM_DEVICE_CREATE && !dm_task_set_cookie(dmt, cookie, (udev_sync)? 0 : DM_UDEV_DISABLE_LIBRARY_FALLBACK))
		goto addout;
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

int
dm_get_map(int major, int minor, char * outparams)
{
	int r = 1;
	struct dm_task *dmt;
	void *next = NULL;
	uint64_t start, length;
	char *target_type = NULL;
	char *params = NULL;

	if (!(dmt = dm_task_create(DM_DEVICE_TABLE)))
		return 1;

	dm_task_set_major(dmt, major);
	dm_task_set_minor(dmt, minor);
	dm_task_no_open_count(dmt);

	if (!dm_task_run(dmt))
		goto out;

	/* Fetch 1st target */
	next = dm_get_next_target(dmt, next, &start, &length,
				  &target_type, &params);

	if (snprintf(outparams, PARAMS_SIZE, "%s", params) <= PARAMS_SIZE)
		r = 0;
out:
	dm_task_destroy(dmt);
	return r;
}

#define FEATURE_NO_PART "no_partitions"

int
dm_no_partitions(int major, int minor)
{
	char params[PARAMS_SIZE], *ptr;
	int i, num_features;

	if (dm_get_map(major, minor, params))
		return 0;

	ptr = params;
	num_features = strtoul(params, &ptr, 10);
	if ((ptr == params) || num_features == 0) {
		/* No features found, return success */
		return 0;
	}
	for (i = 0; (i < num_features); i++) {
		if (!ptr || ptr > params + strlen(params))
			break;
		/* Skip whitespaces */
		while(ptr && *ptr == ' ') ptr++;
		if (!strncmp(ptr, FEATURE_NO_PART, strlen(FEATURE_NO_PART)))
			return 1;
		ptr = strchr(ptr, ' ');
	}
	return 0;
}
