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

int dm_prereq(char * str, int x, int y, int z)
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

int dm_simplecmd(int task, const char *name, int no_flush, uint16_t udev_flags)
{
	int r = 0;
	int udev_wait_flag = (task == DM_DEVICE_RESUME ||
			      task == DM_DEVICE_REMOVE);
#ifdef LIBDM_API_COOKIE
	uint32_t cookie = 0;
#endif
	struct dm_task *dmt;

	if (!(dmt = dm_task_create(task)))
		return 0;

	if (!dm_task_set_name(dmt, name))
		goto out;

	dm_task_no_open_count(dmt);
	dm_task_skip_lockfs(dmt);

	if (no_flush)
		dm_task_no_flush(dmt);

#ifdef LIBDM_API_COOKIE
	if (!udev_sync)
		udev_flags |= DM_UDEV_DISABLE_LIBRARY_FALLBACK;
	if (udev_wait_flag && !dm_task_set_cookie(dmt, &cookie, udev_flags))
		goto out;
#endif
	r = dm_task_run(dmt);
#ifdef LIBDM_API_COOKIE
	if (udev_wait_flag)
			dm_udev_wait(cookie);
#endif
out:
	dm_task_destroy(dmt);
	return r;
}

int dm_addmap(int task, const char *name, const char *target,
	      const char *params, uint64_t size, int ro, const char *uuid,
	      int part, mode_t mode, uid_t uid, gid_t gid)
{
	int r = 0;
	struct dm_task *dmt;
	char *prefixed_uuid = NULL;
#ifdef LIBDM_API_COOKIE
	uint32_t cookie = 0;
	uint16_t udev_flags = 0;
#endif

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

#ifdef LIBDM_API_COOKIE
	if (!udev_sync)
		udev_flags = DM_UDEV_DISABLE_LIBRARY_FALLBACK;
	if (task == DM_DEVICE_CREATE &&
	    !dm_task_set_cookie(dmt, &cookie, udev_flags))
		goto addout;
#endif
	r = dm_task_run (dmt);
#ifdef LIBDM_API_COOKIE
	if (task == DM_DEVICE_CREATE)
			dm_udev_wait(cookie);
#endif
addout:
	dm_task_destroy (dmt);
	free(prefixed_uuid);

	return r;
}

int dm_map_present(char * str, char **uuid)
{
	int r = 0;
	struct dm_task *dmt;
	const char *uuidtmp;
	struct dm_info info;

	if (uuid)
		*uuid = NULL;

	if (!(dmt = dm_task_create(DM_DEVICE_INFO)))
		return 0;

	if (!dm_task_set_name(dmt, str))
		goto out;

	dm_task_no_open_count(dmt);

	if (!dm_task_run(dmt))
		goto out;

	if (!dm_task_get_info(dmt, &info))
		goto out;

	if (!info.exists)
		goto out;

	r = 1;
	if (uuid) {
		uuidtmp = dm_task_get_uuid(dmt);
		if (uuidtmp && strlen(uuidtmp))
			*uuid = strdup(uuidtmp);
	}
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
dm_mapuuid(const char *mapname)
{
	struct dm_task *dmt;
	const char *tmp;
	char *uuid = NULL;

	if (!(dmt = dm_task_create(DM_DEVICE_INFO)))
		return NULL;

	if (!dm_task_set_name(dmt, mapname))
		goto out;
	dm_task_no_open_count(dmt);

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
dm_devn (const char * mapname, int *major, int *minor)
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

static int
dm_get_map(char *mapname, char * outparams)
{
	int r = 1;
	struct dm_task *dmt;
	uint64_t start, length;
	char *target_type = NULL;
	char *params = NULL;

	if (!(dmt = dm_task_create(DM_DEVICE_TABLE)))
		return 1;

	if (!dm_task_set_name(dmt, mapname))
		goto out;
	dm_task_no_open_count(dmt);

	if (!dm_task_run(dmt))
		goto out;

	/* Fetch 1st target */
	dm_get_next_target(dmt, NULL, &start, &length,
			   &target_type, &params);

	if (snprintf(outparams, PARAMS_SIZE, "%s", params) <= PARAMS_SIZE)
		r = 0;
out:
	dm_task_destroy(dmt);
	return r;
}

static int
dm_get_opencount (const char * mapname)
{
	int r = -1;
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

	if (!info.exists)
		goto out;

	r = info.open_count;
out:
	dm_task_destroy(dmt);
	return r;
}

/*
 * returns:
 *    1 : match
 *    0 : no match
 *   -1 : empty map
 */
static int
dm_type(const char * name, char * type)
{
	int r = 0;
	struct dm_task *dmt;
	uint64_t start, length;
	char *target_type = NULL;
	char *params;

	if (!(dmt = dm_task_create(DM_DEVICE_TABLE)))
		return 0;

	if (!dm_task_set_name(dmt, name))
		goto out;

	dm_task_no_open_count(dmt);

	if (!dm_task_run(dmt))
		goto out;

	/* Fetch 1st target */
	dm_get_next_target(dmt, NULL, &start, &length,
			   &target_type, &params);

	if (!target_type)
		r = -1;
	else if (!strcmp(target_type, type))
		r = 1;

out:
	dm_task_destroy(dmt);
	return r;
}

/*
 * returns:
 *    0 : if both uuids end with same suffix which starts with UUID_PREFIX
 *    1 : otherwise
 */
int
dm_compare_uuid(const char *mapuuid, const char *partname)
{
	char *partuuid;
	int r = 1;

	partuuid = dm_mapuuid(partname);
	if (!partuuid)
		return 1;

	if (!strncmp(partuuid, "part", 4)) {
		char *p = strstr(partuuid, "mpath-");
		if (p && !strcmp(mapuuid, p))
			r = 0;
	}
	free(partuuid);
	return r;
}

struct remove_data {
	int verbose;
};

static int
do_foreach_partmaps (const char * mapname, const char *uuid,
		     int (*partmap_func)(const char *, void *),
		     void *data)
{
	struct dm_task *dmt;
	struct dm_names *names;
	struct remove_data *rd = data;
	unsigned next = 0;
	char params[PARAMS_SIZE];
	int major, minor;
	char dev_t[32];
	int r = 1;

	if (!(dmt = dm_task_create(DM_DEVICE_LIST)))
		return 1;

	dm_task_no_open_count(dmt);

	if (!dm_task_run(dmt))
		goto out;

	if (!(names = dm_task_get_names(dmt)))
		goto out;

	if (!names->dev) {
		r = 0; /* this is perfectly valid */
		goto out;
	}

	if (dm_devn(mapname, &major, &minor))
		goto out;

	sprintf(dev_t, "%d:%d", major, minor);
	do {
		/*
		 * skip our devmap
		 */
		if (!strcmp(names->name, mapname))
			goto next;

		/*
		 * skip if we cannot fetch the map table from the kernel
		 */
		if (dm_get_map(names->name, &params[0]))
			goto next;

		/*
		 * skip if the table does not map over the multipath map
		 */
		if (!strstr(params, dev_t))
			goto next;

		/*
		 * skip if devmap target is not "linear"
		 */
		if (!dm_type(names->name, "linear")) {
			if (rd->verbose)
				printf("%s: is not a linear target. Not removing\n",
				       names->name);
			goto next;
		}

		/*
		 * skip if uuids don't match
		 */
		if (dm_compare_uuid(uuid, names->name)) {
			if (rd->verbose)
				printf("%s: is not a kpartx partition. Not removing\n",
				       names->name);
			goto next;
		}

		if (partmap_func(names->name, data) != 0)
			goto out;
	next:
		next = names->next;
		names = (void *) names + next;
	} while (next);

	r = 0;
out:
	dm_task_destroy (dmt);
	return r;
}

static int
remove_partmap(const char *name, void *data)
{
	struct remove_data *rd = (struct remove_data *)data;
	int r = 0;

	if (dm_get_opencount(name)) {
		if (rd->verbose)
			printf("%s is in use. Not removing", name);
		return 1;
	}
	if (!dm_simplecmd(DM_DEVICE_REMOVE, name, 0, 0)) {
		if (rd->verbose)
			printf("%s: failed to remove\n", name);
		r = 1;
	} else if (rd->verbose)
		printf("del devmap : %s\n", name);
	return r;
}

int
dm_remove_partmaps (char * mapname, char *uuid, int verbose)
{
	struct remove_data rd = { verbose };
	return do_foreach_partmaps(mapname, uuid, remove_partmap, &rd);
}

#define FEATURE_NO_PART "no_partitions"

int
dm_no_partitions(char *mapname)
{
	char params[PARAMS_SIZE], *ptr;
	int i, num_features;

	if (dm_get_map(mapname, params))
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
