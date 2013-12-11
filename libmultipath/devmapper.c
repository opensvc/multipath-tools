/*
 * snippets copied from device-mapper dmsetup.c
 * Copyright (c) 2004, 2005 Christophe Varoqui
 * Copyright (c) 2005 Kiyoshi Ueda, NEC
 * Copyright (c) 2005 Patrick Caulfield, Redhat
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <libdevmapper.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>

#include "checkers.h"
#include "vector.h"
#include "structs.h"
#include "debug.h"
#include "memory.h"
#include "devmapper.h"
#include "config.h"
#include "sysfs.h"

#include "log_pthread.h"
#include <sys/types.h>
#include <time.h>

#define MAX_WAIT 5
#define LOOPS_PER_SEC 5

#define UUID_PREFIX "mpath-"
#define UUID_PREFIX_LEN 6

#ifndef LIBDM_API_COOKIE
static inline int dm_task_set_cookie(struct dm_task *dmt, uint32_t *c, int a)
{
	return 1;
}

void udev_wait(unsigned int c)
{
}

void udev_set_sync_support(int c)
{
}
#else
void udev_wait(unsigned int c)
{
	dm_udev_wait(c);
}

void udev_set_sync_support(int c)
{
	dm_udev_set_sync_support(c);
}
#endif

static void
dm_write_log (int level, const char *file, int line, const char *f, ...)
{
	va_list ap;
	int thres;

	if (level > 6)
		level = 6;

	thres = (conf) ? conf->verbosity : 0;
	if (thres <= 3 || level > thres)
		return;

	va_start(ap, f);
	if (logsink < 1) {
		if (logsink == 0) {
			time_t t = time(NULL);
			struct tm *tb = localtime(&t);
			char buff[16];

			strftime(buff, sizeof(buff), "%b %d %H:%M:%S", tb);
			buff[sizeof(buff)-1] = '\0';

			fprintf(stdout, "%s | ", buff);
		}
		fprintf(stdout, "libdevmapper: %s(%i): ", file, line);
		vfprintf(stdout, f, ap);
		fprintf(stdout, "\n");
	} else {
		condlog(level, "libdevmapper: %s(%i): ", file, line);
		log_safe(level + 3, f, ap);
	}
	va_end(ap);

	return;
}

extern void
dm_init(void) {
	dm_log_init(&dm_write_log);
	dm_log_init_verbose(conf ? conf->verbosity + 3 : 0);
}

static int
dm_lib_prereq (void)
{
	char version[64];
	int v[3];
#ifdef LIBDM_API_COOKIE
	int minv[3] = {1, 2, 38};
#else
	int minv[3] = {1, 2, 8};
#endif

	dm_get_library_version(version, sizeof(version));
	condlog(3, "libdevmapper version %s", version);
	sscanf(version, "%d.%d.%d ", &v[0], &v[1], &v[2]);

	if VERSION_GE(v, minv)
		return 0;
	condlog(0, "libdevmapper version must be >= %d.%.2d.%.2d",
		minv[0], minv[1], minv[2]);
	return 1;
}

int
dm_drv_version (unsigned int * version, char * str)
{
	int r = 2;
	struct dm_task *dmt;
	struct dm_versions *target;
	struct dm_versions *last_target;
	unsigned int *v;

	version[0] = 0;
	version[1] = 0;
	version[2] = 0;

	if (!(dmt = dm_task_create(DM_DEVICE_LIST_VERSIONS)))
		return 1;

	dm_task_no_open_count(dmt);

	if (!dm_task_run(dmt)) {
		condlog(0, "Can not communicate with kernel DM");
		goto out;
	}
	target = dm_task_get_versions(dmt);

	do {
		last_target = target;
		if (!strncmp(str, target->name, strlen(str))) {
			r = 1;
			break;
		}
		target = (void *) target + target->next;
	} while (last_target != target);

	if (r == 2) {
		condlog(0, "DM %s kernel driver not loaded", str);
		goto out;
	}
	v = target->version;
        version[0] = v[0];
        version[1] = v[1];
        version[2] = v[2];
	r = 0;
out:
	dm_task_destroy(dmt);
	return r;
}

static int
dm_drv_prereq (void)
{
	unsigned int minv[3] = {1, 0, 3};
	unsigned int version[3] = {0, 0, 0};
        unsigned int * v = version;

	if (dm_drv_version(v, TGT_MPATH)) {
		/* in doubt return not capable */
		return 1;
	}

	/* test request based multipath capability */
	condlog(3, "DM multipath kernel driver v%u.%u.%u",
		v[0], v[1], v[2]);

	if VERSION_GE(v, minv)
		return 0;

	condlog(0, "DM multipath kernel driver must be >= v%u.%u.%u",
		minv[0], minv[1], minv[2]);
	return 1;
}

extern int
dm_prereq (void)
{
	if (dm_lib_prereq())
		return 1;
	return dm_drv_prereq();
}

static int
dm_simplecmd (int task, const char *name, int no_flush, int need_sync) {
	int r = 0;
	int udev_wait_flag = (need_sync && (task == DM_DEVICE_RESUME ||
					    task == DM_DEVICE_REMOVE));
	struct dm_task *dmt;

	if (!(dmt = dm_task_create (task)))
		return 0;

	if (!dm_task_set_name (dmt, name))
		goto out;

	dm_task_no_open_count(dmt);
	dm_task_skip_lockfs(dmt);	/* for DM_DEVICE_RESUME */
#ifdef LIBDM_API_FLUSH
	if (no_flush)
		dm_task_no_flush(dmt);		/* for DM_DEVICE_SUSPEND/RESUME */
#endif

	if (udev_wait_flag && !dm_task_set_cookie(dmt, &conf->cookie, (conf->daemon)? DM_UDEV_DISABLE_LIBRARY_FALLBACK : 0))
		goto out;
	r = dm_task_run (dmt);

	out:
	dm_task_destroy (dmt);
	return r;
}

extern int
dm_simplecmd_flush (int task, const char *name, int needsync) {
	return dm_simplecmd(task, name, 0, needsync);
}

extern int
dm_simplecmd_noflush (int task, const char *name) {
	return dm_simplecmd(task, name, 1, 1);
}

extern int
dm_addmap (int task, const char *target, struct multipath *mpp, char * params,
	   int use_uuid, int ro) {
	int r = 0;
	struct dm_task *dmt;
	char *prefixed_uuid = NULL;

	if (!(dmt = dm_task_create (task)))
		return 0;

	if (!dm_task_set_name (dmt, mpp->alias))
		goto addout;

	if (!dm_task_add_target (dmt, 0, mpp->size, target, params))
		goto addout;

	if (ro)
		dm_task_set_ro(dmt);

	if (use_uuid && strlen(mpp->wwid) > 0){
		prefixed_uuid = MALLOC(UUID_PREFIX_LEN + strlen(mpp->wwid) + 1);
		if (!prefixed_uuid) {
			condlog(0, "cannot create prefixed uuid : %s",
				strerror(errno));
			goto addout;
		}
		sprintf(prefixed_uuid, UUID_PREFIX "%s", mpp->wwid);
		if (!dm_task_set_uuid(dmt, prefixed_uuid))
			goto freeout;
	}

	if (mpp->attribute_flags & (1 << ATTR_MODE) &&
	    !dm_task_set_mode(dmt, mpp->mode))
		goto freeout;
	if (mpp->attribute_flags & (1 << ATTR_UID) &&
	    !dm_task_set_uid(dmt, mpp->uid))
		goto freeout;
	if (mpp->attribute_flags & (1 << ATTR_GID) &&
	    !dm_task_set_gid(dmt, mpp->gid))
		goto freeout;
	condlog(4, "%s: addmap [0 %llu %s %s]", mpp->alias, mpp->size,
		target, params);

	dm_task_no_open_count(dmt);

	if (task == DM_DEVICE_CREATE &&
	    !dm_task_set_cookie(dmt, &conf->cookie, (conf->daemon)? DM_UDEV_DISABLE_LIBRARY_FALLBACK : 0))
		goto freeout;
	r = dm_task_run (dmt);

	freeout:
	if (prefixed_uuid)
		FREE(prefixed_uuid);

	addout:
	dm_task_destroy (dmt);

	return r;
}

extern int
dm_addmap_create (struct multipath *mpp, char * params) {
	int ro;

	for (ro = 0; ro <= 1; ro++) {
		int err;

		if (dm_addmap(DM_DEVICE_CREATE, TGT_MPATH, mpp, params, 1, ro))
			return 1;
		/*
		 * DM_DEVICE_CREATE is actually DM_DEV_CREATE + DM_TABLE_LOAD.
		 * Failing the second part leaves an empty map. Clean it up.
		 */
		err = errno;
		if (dm_map_present(mpp->alias)) {
			condlog(3, "%s: failed to load map (a path might be in use)", mpp->alias);
			dm_flush_map_nosync(mpp->alias);
		}
		if (err != EROFS)
			break;
	}
	return 0;
}

#define ADDMAP_RW 0
#define ADDMAP_RO 1

extern int
dm_addmap_reload (struct multipath *mpp, char *params) {
	if (dm_addmap(DM_DEVICE_RELOAD, TGT_MPATH, mpp, params, 0, ADDMAP_RW))
		return 1;
	if (errno != EROFS)
		return 0;
	return dm_addmap(DM_DEVICE_RELOAD, TGT_MPATH, mpp, params, 0, ADDMAP_RO);
}

extern int
dm_map_present (const char * str)
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

extern int
dm_get_map(const char * name, unsigned long long * size, char * outparams)
{
	int r = 1;
	struct dm_task *dmt;
	void *next = NULL;
	uint64_t start, length;
	char *target_type = NULL;
	char *params = NULL;

	if (!(dmt = dm_task_create(DM_DEVICE_TABLE)))
		return 1;

	if (!dm_task_set_name(dmt, name))
		goto out;

	dm_task_no_open_count(dmt);

	if (!dm_task_run(dmt))
		goto out;

	/* Fetch 1st target */
	next = dm_get_next_target(dmt, next, &start, &length,
				  &target_type, &params);

	if (size)
		*size = length;

	if (!outparams) {
		r = 0;
		goto out;
	}
	if (snprintf(outparams, PARAMS_SIZE, "%s", params) <= PARAMS_SIZE)
		r = 0;
out:
	dm_task_destroy(dmt);
	return r;
}

static int
dm_get_prefixed_uuid(const char *name, char *uuid)
{
	struct dm_task *dmt;
	const char *uuidtmp;
	int r = 1;

	dmt = dm_task_create(DM_DEVICE_INFO);
	if (!dmt)
		return 1;

	if (!dm_task_set_name (dmt, name))
		goto uuidout;

	if (!dm_task_run(dmt))
		goto uuidout;

	uuidtmp = dm_task_get_uuid(dmt);
	if (uuidtmp)
		strcpy(uuid, uuidtmp);
	else
		uuid[0] = '\0';

	r = 0;
uuidout:
	dm_task_destroy(dmt);
	return r;
}

extern int
dm_get_uuid(char *name, char *uuid)
{
	char uuidtmp[WWID_SIZE];

	if (dm_get_prefixed_uuid(name, uuidtmp))
		return 1;

	if (!strncmp(uuidtmp, UUID_PREFIX, UUID_PREFIX_LEN))
		strcpy(uuid, uuidtmp + UUID_PREFIX_LEN);
	else
		strcpy(uuid, uuidtmp);

	return 0;
}

/*
 * returns:
 *    0 : if both uuids end with same suffix which starts with UUID_PREFIX
 *    1 : otherwise
 */
int
dm_compare_uuid(const char* mapname1, const char* mapname2)
{
	char *p1, *p2;
	char uuid1[WWID_SIZE], uuid2[WWID_SIZE];

	if (dm_get_prefixed_uuid(mapname1, uuid1))
		return 1;

	if (dm_get_prefixed_uuid(mapname2, uuid2))
		return 1;

	p1 = strstr(uuid1, UUID_PREFIX);
	p2 = strstr(uuid2, UUID_PREFIX);
	if (p1 && p2 && !strcmp(p1, p2))
		return 0;

	return 1;
}

extern int
dm_get_status(char * name, char * outstatus)
{
	int r = 1;
	struct dm_task *dmt;
	void *next = NULL;
	uint64_t start, length;
	char *target_type;
	char *status;

	if (!(dmt = dm_task_create(DM_DEVICE_STATUS)))
		return 1;

	if (!dm_task_set_name(dmt, name))
		goto out;

	dm_task_no_open_count(dmt);

	if (!dm_task_run(dmt))
		goto out;

	/* Fetch 1st target */
	next = dm_get_next_target(dmt, next, &start, &length,
				  &target_type, &status);

	if (snprintf(outstatus, PARAMS_SIZE, "%s", status) <= PARAMS_SIZE)
		r = 0;
out:
	if (r)
		condlog(0, "%s: error getting map status string", name);

	dm_task_destroy(dmt);
	return r;
}

/*
 * returns:
 *    1 : match
 *    0 : no match
 *   -1 : empty map
 */
extern int
dm_type(const char * name, char * type)
{
	int r = 0;
	struct dm_task *dmt;
	void *next = NULL;
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
	next = dm_get_next_target(dmt, next, &start, &length,
				  &target_type, &params);

	if (!target_type)
		r = -1;
	else if (!strcmp(target_type, type))
		r = 1;

out:
	dm_task_destroy(dmt);
	return r;
}

static int
dm_dev_t (const char * mapname, char * dev_t, int len)
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

	r = info.open_count;
	if (snprintf(dev_t, len, "%i:%i", info.major, info.minor) > len)
		    goto out;

	r = 0;
out:
	dm_task_destroy(dmt);
	return r;
}

int
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

	r = info.open_count;
out:
	dm_task_destroy(dmt);
	return r;
}

int
dm_get_major (char * mapname)
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

	r = info.major;
out:
	dm_task_destroy(dmt);
	return r;
}

int
dm_get_minor (char * mapname)
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

	r = info.minor;
out:
	dm_task_destroy(dmt);
	return r;
}

extern int
_dm_flush_map (const char * mapname, int need_sync)
{
	int r;

	if (!dm_map_present(mapname))
		return 0;

	if (dm_type(mapname, TGT_MPATH) <= 0)
		return 0; /* nothing to do */

	if (dm_remove_partmaps(mapname, need_sync))
		return 1;

	if (dm_get_opencount(mapname)) {
		condlog(2, "%s: map in use", mapname);
		return 1;
	}

	r = dm_simplecmd_flush(DM_DEVICE_REMOVE, mapname, need_sync);

	if (r) {
		condlog(4, "multipath map %s removed", mapname);
		return 0;
	}
	return 1;
}

extern int
dm_suspend_and_flush_map (const char * mapname)
{
	int s = 0, queue_if_no_path = 0;
	unsigned long long mapsize;
	char params[PARAMS_SIZE] = {0};

	if (!dm_map_present(mapname))
		return 0;

	if (dm_type(mapname, TGT_MPATH) <= 0)
		return 0; /* nothing to do */

	if (!dm_get_map(mapname, &mapsize, params)) {
		if (strstr(params, "queue_if_no_path"))
			queue_if_no_path = 1;
	}

	if (queue_if_no_path)
		s = dm_queue_if_no_path((char *)mapname, 0);
	/* Leave queue_if_no_path alone if unset failed */
	if (s)
		queue_if_no_path = 0;
	else
		s = dm_simplecmd_flush(DM_DEVICE_SUSPEND, mapname, 0);

	if (!dm_flush_map(mapname)) {
		condlog(4, "multipath map %s removed", mapname);
		return 0;
	}
	condlog(2, "failed to remove multipath map %s", mapname);
	dm_simplecmd_noflush(DM_DEVICE_RESUME, mapname);
	if (queue_if_no_path)
		s = dm_queue_if_no_path((char *)mapname, 1);
	return 1;
}

extern int
dm_flush_maps (void)
{
	int r = 0;
	struct dm_task *dmt;
	struct dm_names *names;
	unsigned next = 0;

	if (!(dmt = dm_task_create (DM_DEVICE_LIST)))
		return 0;

	dm_task_no_open_count(dmt);

	if (!dm_task_run (dmt))
		goto out;

	if (!(names = dm_task_get_names (dmt)))
		goto out;

	if (!names->dev)
		goto out;

	do {
		r |= dm_suspend_and_flush_map(names->name);
		next = names->next;
		names = (void *) names + next;
	} while (next);

	out:
	dm_task_destroy (dmt);
	return r;
}

int
dm_message(char * mapname, char * message)
{
	int r = 1;
	struct dm_task *dmt;

	if (!(dmt = dm_task_create(DM_DEVICE_TARGET_MSG)))
		return 1;

	if (!dm_task_set_name(dmt, mapname))
		goto out;

	if (!dm_task_set_sector(dmt, 0))
		goto out;

	if (!dm_task_set_message(dmt, message))
		goto out;

	dm_task_no_open_count(dmt);

	if (!dm_task_run(dmt))
		goto out;

	r = 0;
out:
	if (r)
		condlog(0, "DM message failed [%s]", message);

	dm_task_destroy(dmt);
	return r;
}

int
dm_fail_path(char * mapname, char * path)
{
	char message[32];

	if (snprintf(message, 32, "fail_path %s", path) > 32)
		return 1;

	return dm_message(mapname, message);
}

int
dm_reinstate_path(char * mapname, char * path)
{
	char message[32];

	if (snprintf(message, 32, "reinstate_path %s", path) > 32)
		return 1;

	return dm_message(mapname, message);
}

int
dm_queue_if_no_path(char *mapname, int enable)
{
	char *message;

	if (enable)
		message = "queue_if_no_path";
	else
		message = "fail_if_no_path";

	return dm_message(mapname, message);
}

static int
dm_groupmsg (char * msg, char * mapname, int index)
{
	char message[32];

	if (snprintf(message, 32, "%s_group %i", msg, index) > 32)
		return 1;

	return dm_message(mapname, message);
}

int
dm_switchgroup(char * mapname, int index)
{
	return dm_groupmsg("switch", mapname, index);
}

int
dm_enablegroup(char * mapname, int index)
{
	return dm_groupmsg("enable", mapname, index);
}

int
dm_disablegroup(char * mapname, int index)
{
	return dm_groupmsg("disable", mapname, index);
}

int
dm_get_maps (vector mp)
{
	struct multipath * mpp;
	int r = 1;
	int info;
	struct dm_task *dmt;
	struct dm_names *names;
	unsigned next = 0;

	if (!mp)
		return 1;

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

	do {
		info = dm_type(names->name, TGT_MPATH);

		if (info <= 0)
			goto next;

		mpp = alloc_multipath();

		if (!mpp)
			goto out;

		mpp->alias = STRDUP(names->name);

		if (!mpp->alias)
			goto out1;

		if (info > 0) {
			if (dm_get_map(names->name, &mpp->size, NULL))
				goto out1;

			dm_get_uuid(names->name, mpp->wwid);
			dm_get_info(names->name, &mpp->dmi);
		}

		if (!vector_alloc_slot(mp))
			goto out1;

		vector_set_slot(mp, mpp);
		mpp = NULL;
next:
		next = names->next;
		names = (void *) names + next;
	} while (next);

	r = 0;
	goto out;
out1:
	free_multipath(mpp, KEEP_PATHS);
out:
	dm_task_destroy (dmt);
	return r;
}

int
dm_geteventnr (char *name)
{
	struct dm_task *dmt;
	struct dm_info info;
	int event = -1;

	if (!(dmt = dm_task_create(DM_DEVICE_INFO)))
		return -1;

	if (!dm_task_set_name(dmt, name))
		goto out;

	dm_task_no_open_count(dmt);

	if (!dm_task_run(dmt))
		goto out;

	if (!dm_task_get_info(dmt, &info))
		goto out;

	if (info.exists)
		event = info.event_nr;

out:
	dm_task_destroy(dmt);

	return event;
}

char *
dm_mapname(int major, int minor)
{
	char * response = NULL;
	const char *map;
	struct dm_task *dmt;
	int r;
	int loop = MAX_WAIT * LOOPS_PER_SEC;

	if (!(dmt = dm_task_create(DM_DEVICE_STATUS)))
		return NULL;

	if (!dm_task_set_major(dmt, major) ||
	    !dm_task_set_minor(dmt, minor))
		goto bad;

	dm_task_no_open_count(dmt);

	/*
	 * device map might not be ready when we get here from
	 * daemon uev_trigger -> uev_add_map
	 */
	while (--loop) {
		r = dm_task_run(dmt);

		if (r)
			break;

		usleep(1000 * 1000 / LOOPS_PER_SEC);
	}

	if (!r) {
		condlog(0, "%i:%i: timeout fetching map name", major, minor);
		goto bad;
	}

	map = dm_task_get_name(dmt);
	if (map && strlen(map))
		response = STRDUP((char *)dm_task_get_name(dmt));

	dm_task_destroy(dmt);
	return response;
bad:
	dm_task_destroy(dmt);
	condlog(0, "%i:%i: error fetching map name", major, minor);
	return NULL;
}

int
dm_remove_partmaps (const char * mapname, int need_sync)
{
	struct dm_task *dmt;
	struct dm_names *names;
	unsigned next = 0;
	char params[PARAMS_SIZE];
	unsigned long long size;
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

	if (dm_dev_t(mapname, &dev_t[0], 32))
		goto out;

	do {
		if (
		    /*
		     * if devmap target is "linear"
		     */
		    (dm_type(names->name, TGT_PART) > 0) &&

		    /*
		     * and both uuid end with same suffix starting
		     * at UUID_PREFIX
		     */
		    (!dm_compare_uuid(names->name, mapname)) &&

		    /*
		     * and we can fetch the map table from the kernel
		     */
		    !dm_get_map(names->name, &size, &params[0]) &&

		    /*
		     * and the table maps over the multipath map
		     */
		    strstr(params, dev_t)
		   ) {
			/*
			 * then it's a kpartx generated partition.
			 * remove it.
			 */
			/*
			 * if the opencount is 0 maybe some other
			 * partitions depend on it.
			 */
			if (dm_get_opencount(names->name)) {
				dm_remove_partmaps(names->name, need_sync);
				if (dm_get_opencount(names->name)) {
					condlog(2, "%s: map in use",
						names->name);
					goto out;
				}
			}
			condlog(4, "partition map %s removed",
				names->name);
			dm_simplecmd_flush(DM_DEVICE_REMOVE, names->name,
					   need_sync);
		}

		next = names->next;
		names = (void *) names + next;
	} while (next);

	r = 0;
out:
	dm_task_destroy (dmt);
	return r;
}

static struct dm_info *
alloc_dminfo (void)
{
	return MALLOC(sizeof(struct dm_info));
}

int
dm_get_info (char * mapname, struct dm_info ** dmi)
{
	int r = 1;
	struct dm_task *dmt = NULL;

	if (!mapname)
		return 1;

	if (!*dmi)
		*dmi = alloc_dminfo();

	if (!*dmi)
		return 1;

	if (!(dmt = dm_task_create(DM_DEVICE_INFO)))
		goto out;

	if (!dm_task_set_name(dmt, mapname))
		goto out;

	dm_task_no_open_count(dmt);

	if (!dm_task_run(dmt))
		goto out;

	if (!dm_task_get_info(dmt, *dmi))
		goto out;

	r = 0;
out:
	if (r) {
		memset(*dmi, 0, sizeof(struct dm_info));
		FREE(*dmi);
		*dmi = NULL;
	}

	if (dmt)
		dm_task_destroy(dmt);

	return r;
}

int
dm_rename_partmaps (char * old, char * new)
{
	struct dm_task *dmt;
	struct dm_names *names;
	unsigned next = 0;
	char buff[PARAMS_SIZE];
	unsigned long long size;
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

	if (dm_dev_t(old, &dev_t[0], 32))
		goto out;

	do {
		if (
		    /*
		     * if devmap target is "linear"
		     */
		    (dm_type(names->name, TGT_PART) > 0) &&

		    /*
		     * and the multipath mapname and the part mapname start
		     * the same
		     */
		    !strncmp(names->name, old, strlen(old)) &&

		    /*
		     * and we can fetch the map table from the kernel
		     */
		    !dm_get_map(names->name, &size, &buff[0]) &&

		    /*
		     * and the table maps over the multipath map
		     */
		    strstr(buff, dev_t)
		   ) {
				/*
				 * then it's a kpartx generated partition.
				 * Rename it.
				 */
				snprintf(buff, PARAMS_SIZE, "%s%s",
					 new, names->name + strlen(old));
				dm_rename(names->name, buff);
				condlog(4, "partition map %s renamed",
					names->name);
		   }

		next = names->next;
		names = (void *) names + next;
	} while (next);

	r = 0;
out:
	dm_task_destroy (dmt);
	return r;
}

int
dm_rename (char * old, char * new)
{
	int r = 0;
	struct dm_task *dmt;

	if (dm_rename_partmaps(old, new))
		return r;

	if (!(dmt = dm_task_create(DM_DEVICE_RENAME)))
		return r;

	if (!dm_task_set_name(dmt, old))
		goto out;

	if (!dm_task_set_newname(dmt, new))
		goto out;

	dm_task_no_open_count(dmt);

	if (!dm_task_set_cookie(dmt, &conf->cookie, (conf->daemon)? DM_UDEV_DISABLE_LIBRARY_FALLBACK : 0))
		goto out;
	if (!dm_task_run(dmt))
		goto out;

	r = 1;
out:
	dm_task_destroy(dmt);
	return r;
}

void dm_reassign_deps(char *table, char *dep, char *newdep)
{
	char *p, *n;
	char newtable[PARAMS_SIZE];

	strcpy(newtable, table);
	p = strstr(newtable, dep);
	n = table + (p - newtable);
	strcpy(n, newdep);
	n += strlen(newdep);
	p += strlen(dep);
	strcat(n, p);
}

int dm_reassign_table(const char *name, char *old, char *new)
{
	int r, modified = 0;
	uint64_t start, length;
	struct dm_task *dmt, *reload_dmt;
	char *target, *params = NULL;
	char buff[PARAMS_SIZE];
	void *next = NULL;

	if (!(dmt = dm_task_create(DM_DEVICE_TABLE)))
		return 0;

	if (!dm_task_set_name(dmt, name))
		goto out;

	dm_task_no_open_count(dmt);

	if (!dm_task_run(dmt))
		goto out;
	if (!(reload_dmt = dm_task_create(DM_DEVICE_RELOAD)))
		goto out;
	if (!dm_task_set_name(reload_dmt, name))
		goto out_reload;

	do {
		next = dm_get_next_target(dmt, next, &start, &length,
					  &target, &params);
		memset(buff, 0, PARAMS_SIZE);
		strcpy(buff, params);
		if (strcmp(target, TGT_MPATH) && strstr(params, old)) {
			condlog(3, "%s: replace target %s %s",
				name, target, buff);
			dm_reassign_deps(buff, old, new);
			condlog(3, "%s: with target %s %s",
				name, target, buff);
			modified++;
		}
		dm_task_add_target(reload_dmt, start, length, target, buff);
	} while (next);

	if (modified) {
		dm_task_no_open_count(reload_dmt);

		if (!dm_task_run(reload_dmt)) {
			condlog(3, "%s: failed to reassign targets", name);
			goto out_reload;
		}
		dm_simplecmd_noflush(DM_DEVICE_RESUME, name);
	}
	r = 1;

out_reload:
	dm_task_destroy(reload_dmt);
out:
	dm_task_destroy(dmt);
	return r;
}


/*
 * Reassign existing device-mapper table(s) to not use
 * the block devices but point to the multipathed
 * device instead
 */
int dm_reassign(const char *mapname)
{
	struct dm_deps *deps;
	struct dm_task *dmt;
	struct dm_info info;
	char dev_t[32], dm_dep[32];
	int r = 0, i;

	if (dm_dev_t(mapname, &dev_t[0], 32)) {
		condlog(3, "%s: failed to get device number", mapname);
		return 1;
	}

	if (!(dmt = dm_task_create(DM_DEVICE_DEPS))) {
		condlog(3, "%s: couldn't make dm task", mapname);
		return 0;
	}

	if (!dm_task_set_name(dmt, mapname))
		goto out;

	dm_task_no_open_count(dmt);

	if (!dm_task_run(dmt))
		goto out;

	if (!dm_task_get_info(dmt, &info))
		goto out;

	if (!(deps = dm_task_get_deps(dmt)))
		goto out;

	if (!info.exists)
		goto out;

	for (i = 0; i < deps->count; i++) {
		sprintf(dm_dep, "%d:%d",
			major(deps->device[i]),
			minor(deps->device[i]));
		sysfs_check_holders(dm_dep, dev_t);
	}

	dm_task_destroy (dmt);

	r = 1;
out:
	return r;
}

int dm_setgeometry(struct multipath *mpp)
{
	struct dm_task *dmt;
	struct path *pp;
	char heads[4], sectors[4];
	char cylinders[10], start[32];
	int r = 0;

	if (!mpp)
		return 1;

	pp = first_path(mpp);
	if (!pp) {
		condlog(3, "%s: no path for geometry", mpp->alias);
		return 1;
	}
	if (pp->geom.cylinders == 0 ||
	    pp->geom.heads == 0 ||
	    pp->geom.sectors == 0) {
		condlog(3, "%s: invalid geometry on %s", mpp->alias, pp->dev);
		return 1;
	}

	if (!(dmt = dm_task_create(DM_DEVICE_SET_GEOMETRY)))
		return 0;

	if (!dm_task_set_name(dmt, mpp->alias))
		goto out;

	dm_task_no_open_count(dmt);

	/* What a sick interface ... */
	snprintf(heads, 4, "%u", pp->geom.heads);
	snprintf(sectors, 4, "%u", pp->geom.sectors);
	snprintf(cylinders, 10, "%u", pp->geom.cylinders);
	snprintf(start, 32, "%lu", pp->geom.start);
	if (!dm_task_set_geometry(dmt, cylinders, heads, sectors, start)) {
		condlog(3, "%s: Failed to set geometry", mpp->alias);
		goto out;
	}

	r = dm_task_run(dmt);
out:
	dm_task_destroy(dmt);

	return r;
}
