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
#include <sys/sysmacros.h>
#include "devmapper.h"

#define _UUID_PREFIX "part"
#define UUID_PREFIX _UUID_PREFIX "%d-"
#define _UUID_PREFIX_LEN (sizeof(_UUID_PREFIX) - 1)
#define MAX_PREFIX_LEN (_UUID_PREFIX_LEN + 4)
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

static void
strip_slash (char * device)
{
	char * p = device;

	while (*(p++) != 0x0) {

		if (*p == '/')
			*p = '!';
	}
}

static int format_partname(char *buf, size_t bufsiz,
			   const char *mapname, const char *delim, int part)
{
	if (snprintf(buf, bufsiz, "%s%s%d", mapname, delim, part) >= bufsiz)
		return 0;
	strip_slash(buf);
	return 1;
}

static char *make_prefixed_uuid(int part, const char *uuid)
{
	char *prefixed_uuid;
	int len = MAX_PREFIX_LEN + strlen(uuid) + 1;

	prefixed_uuid = malloc(len);
	if (!prefixed_uuid) {
		fprintf(stderr, "cannot create prefixed uuid : %s\n",
			strerror(errno));
		return NULL;
	}
	snprintf(prefixed_uuid, len, UUID_PREFIX "%s", part, uuid);
	return prefixed_uuid;
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
		prefixed_uuid = make_prefixed_uuid(part, uuid);
		if (prefixed_uuid == NULL)
			goto addout;
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

static int dm_map_present(char *str, char **uuid)
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

static int dm_rename (const char *old, const char *new)
{
	int r = 0;
	struct dm_task *dmt;
	uint16_t udev_flags = DM_UDEV_DISABLE_LIBRARY_FALLBACK;
	uint32_t cookie = 0;

	dmt = dm_task_create(DM_DEVICE_RENAME);
	if (!dmt)
		return r;

	if (!dm_task_set_name(dmt, old) ||
	    !dm_task_set_newname(dmt, new) ||
	    !dm_task_no_open_count(dmt) ||
	    !dm_task_set_cookie(dmt, &cookie, udev_flags))
		goto out;

	r = dm_task_run(dmt);
	dm_udev_wait(cookie);

out:
	dm_task_destroy(dmt);
	return r;
}

static char *dm_find_uuid(const char *uuid)
{
	struct dm_task *dmt;
	char *name = NULL;
	const char *tmp;

	if ((dmt = dm_task_create(DM_DEVICE_INFO)) == NULL)
		return NULL;

	if (!dm_task_set_uuid(dmt, uuid) ||
	    !dm_task_run(dmt))
		goto out;

	tmp = dm_task_get_name(dmt);
	if (tmp != NULL && *tmp != '\0')
		name = strdup(tmp);

out:
	dm_task_destroy(dmt);
	return name;
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
 * Return the device number of the first dependent device
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
		return 1;

	if (!dm_task_set_name(dmt, mapname))
		goto out;

	if (!dm_task_run(dmt))
		goto out;

	if (!dm_task_get_info(dmt, &info) || info.exists == 0)
		goto out;

	*major = info.major;
	*minor = info.minor;

	r = 0;
out:
	dm_task_destroy(dmt);
	return r;
}

static int
dm_get_map(const char *mapname, char * outparams)
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
	if (dm_get_next_target(dmt, NULL, &start, &length,
			       &target_type, &params) != NULL)
		/* more than one target */
		r = -1;
	else if (!target_type)
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

	if (!strncmp(partuuid, _UUID_PREFIX, _UUID_PREFIX_LEN)) {
		char *p = partuuid + _UUID_PREFIX_LEN;
		/* skip partition number */
		while (isdigit(*p))
			p++;
		if (p != partuuid + _UUID_PREFIX_LEN && *p == '-' &&
		    !strcmp(mapuuid, p + 1))
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
		     dev_t devt,
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
	int is_dmdev = 1;

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

	if (dm_devn(mapname, &major, &minor) ||
	    (major != major(devt) || minor != minor(devt)))
		/*
		 * The latter could happen if a dm device "/dev/mapper/loop0"
		 * exits while kpartx is called on "/dev/loop0".
		 */
		is_dmdev = 0;

	sprintf(dev_t, "%d:%d", major(devt), minor(devt));
	do {
		/*
		 * skip our devmap
		 */
		if (is_dmdev && !strcmp(names->name, mapname))
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
		if (dm_type(names->name, "linear") != 1) {
			if (rd->verbose)
				printf("%s: is not a linear target. Not removing\n",
				       names->name);
			goto next;
		}

		/*
		 * skip if uuids don't match
		 */
		if (uuid && dm_compare_uuid(uuid, names->name)) {
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
dm_remove_partmaps (char * mapname, char *uuid, dev_t devt, int verbose)
{
	struct remove_data rd = { verbose };
	return do_foreach_partmaps(mapname, uuid, devt, remove_partmap, &rd);
}

int dm_find_part(const char *parent, const char *delim, int part,
		 const char *parent_uuid,
		 char *name, size_t namesiz, char **part_uuid, int verbose)
{
	int r;
	char params[PARAMS_SIZE];
	char *tmp;
	char *uuid;
	int major, minor;
	char dev_t[32];

	if (!format_partname(name, namesiz, parent, delim, part)) {
		if (verbose)
			fprintf(stderr, "partname too small\n");
		return 0;
	}

	r = dm_map_present(name, part_uuid);
	if (r == 1 || parent_uuid == NULL || *parent_uuid == '\0')
		return r;

	uuid = make_prefixed_uuid(part, parent_uuid);
	if (!uuid)
		return 0;

	tmp = dm_find_uuid(uuid);
	if (tmp == NULL)
		goto out;

	/* Sanity check on partition, see dm_foreach_partmaps */
	if (dm_type(tmp, "linear") != 1)
		goto out;

	/*
	 * Try nondm uuid first. That way we avoid confusing
	 * a device name with a device mapper name.
	 */
	if (!nondm_parse_uuid(parent_uuid, &major, &minor) &&
	    dm_devn(parent, &major, &minor))
		goto out;
	snprintf(dev_t, sizeof(dev_t), "%d:%d", major, minor);

	if (dm_get_map(tmp, params))
		goto out;

	if (!strstr(params, dev_t))
		goto out;

	if (verbose)
		fprintf(stderr, "found map %s for uuid %s, renaming to %s\n",
		       tmp, uuid, name);

	r = dm_rename(tmp, name);
	if (r == 1) {
		free(tmp);
		*part_uuid = uuid;
		return 1;
	}
	if (verbose)
		fprintf(stderr, "renaming %s->%s failed\n", tmp, name);
out:
	free(uuid);
	free(tmp);
	return r;
}

char *nondm_create_uuid(dev_t devt)
{
#define NONDM_UUID_BUFLEN (34 + sizeof(NONDM_UUID_PREFIX) + \
			   sizeof(NONDM_UUID_SUFFIX))
	static char uuid_buf[NONDM_UUID_BUFLEN];
	snprintf(uuid_buf, sizeof(uuid_buf), "%s_%u:%u_%s",
		 NONDM_UUID_PREFIX, major(devt), minor(devt),
		 NONDM_UUID_SUFFIX);
	uuid_buf[NONDM_UUID_BUFLEN-1] = '\0';
	return uuid_buf;
}

int nondm_parse_uuid(const char *uuid, int *major, int *minor)
{
	const char *p;
	char *e;
	int ma, mi;

	if (strncmp(uuid, NONDM_UUID_PREFIX "_", sizeof(NONDM_UUID_PREFIX)))
		return 0;
	p = uuid + sizeof(NONDM_UUID_PREFIX);
	ma = strtoul(p, &e, 10);
	if (e == p || *e != ':')
		return 0;
	p = e + 1;
	mi = strtoul(p, &e, 10);
	if (e == p || *e != '_')
		return 0;
	p = e + 1;
	if (strcmp(p, NONDM_UUID_SUFFIX))
		return 0;

	*major = ma;
	*minor = mi;
	return 1;
}
