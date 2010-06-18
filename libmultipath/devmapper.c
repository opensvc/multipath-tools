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

#include "log_pthread.h"
#include <sys/types.h>
#include <time.h>

#define MAX_WAIT 5
#define LOOPS_PER_SEC 5

#define UUID_PREFIX "mpath-"
#define UUID_PREFIX_LEN 6

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
	if (!logsink) {
		time_t t = time(NULL);
		struct tm *tb = localtime(&t);
		char buff[16];

		strftime(buff, sizeof(buff), "%b %d %H:%M:%S", tb);
		buff[sizeof(buff)-1] = '\0';

		fprintf(stdout, "%s | ", buff);
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
dm_libprereq (void)
{
	char version[64];
	int v[3];
	int minv[3] = {1, 2, 38};

	dm_get_library_version(version, sizeof(version));
	condlog(3, "libdevmapper version %s", version);
	sscanf(version, "%d.%d.%d ", &v[0], &v[1], &v[2]);

	if ((v[0] > minv[0]) ||
	    ((v[0] ==  minv[0]) && (v[1] > minv[1])) ||
	    ((v[0] == minv[0]) && (v[1] == minv[1]) && (v[2] >= minv[2])))
		return 0;
	condlog(0, "libdevmapper version must be >= %d.%.2d.%.2d",
		minv[0], minv[1], minv[2]);
	return 1;
}

static int
dm_drvprereq (char * str)
{
	int r = 2;
	struct dm_task *dmt;
	struct dm_versions *target;
	struct dm_versions *last_target;
	int minv[3] = {1, 0, 3};
	unsigned int *v;

	if (!(dmt = dm_task_create(DM_DEVICE_LIST_VERSIONS)))
		return 3;

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
		condlog(0, "DM multipath kernel driver not loaded");
		goto out;
	}
	v = target->version;
	if ((v[0] > minv[0]) ||
	    ((v[0] == minv[0]) && (v[1] > minv[1])) ||
	    ((v[0] == minv[0]) && (v[1] == minv[1]) && (v[2] >= minv[2]))) {
		r = 0;
		goto out;
	}
	condlog(0, "DM multipath kernel driver must be >= %u.%.2u.%.2u",
		minv[0], minv[1], minv[2]);
out:
	dm_task_destroy(dmt);
	return r;
}

extern int
dm_prereq (void)
{
	if (dm_libprereq())
		return 1;
	return dm_drvprereq(TGT_MPATH);
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

	if (udev_wait_flag && !dm_task_set_cookie(dmt, &conf->cookie, 0))
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
dm_addmap (int task, const char *target, struct multipath *mpp, int use_uuid,
	   int ro) {
	int r = 0;
	struct dm_task *dmt;
	char *prefixed_uuid = NULL;

	if (!(dmt = dm_task_create (task)))
		return 0;

	if (!dm_task_set_name (dmt, mpp->alias))
		goto addout;

	if (!dm_task_add_target (dmt, 0, mpp->size, target, mpp->params))
		goto addout;

	if (ro)
		dm_task_set_ro(dmt);

	if (use_uuid && mpp->wwid){
		prefixed_uuid = MALLOC(UUID_PREFIX_LEN + strlen(mpp->wwid) + 1);
		if (!prefixed_uuid) {
			condlog(0, "cannot create prefixed uuid : %s\n",
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

	dm_task_no_open_count(dmt);

	if (task == DM_DEVICE_CREATE &&
	    !dm_task_set_cookie(dmt, &conf->cookie, 0))
		goto freeout;
	r = dm_task_run (dmt);

	freeout:
	if (prefixed_uuid)
		FREE(prefixed_uuid);

	addout:
	dm_task_destroy (dmt);

	return r;
}

static int
_dm_addmap_create (struct multipath *mpp, int ro) {
	int r;
	r = dm_addmap(DM_DEVICE_CREATE, TGT_MPATH, mpp, 1, ro);
	/*
	 * DM_DEVICE_CREATE is actually DM_DEV_CREATE + DM_TABLE_LOAD.
	 * Failing the second part leaves an empty map. Clean it up.
	 */
	if (!r && dm_map_present(mpp->alias)) {
		condlog(3, "%s: failed to load map (a path might be in use)",
			mpp->alias);
		dm_flush_map_nosync(mpp->alias);
	}
	return r;
}

#define ADDMAP_RW 0
#define ADDMAP_RO 1

extern int
dm_addmap_create (struct multipath *mpp) {
	return _dm_addmap_create(mpp, ADDMAP_RW);
}

extern int
dm_addmap_create_ro (struct multipath *mpp) {
	return _dm_addmap_create(mpp, ADDMAP_RO);
}

extern int
dm_addmap_reload (struct multipath *mpp) {
	return dm_addmap(DM_DEVICE_RELOAD, TGT_MPATH, mpp, 0, ADDMAP_RW);
}

extern int
dm_addmap_reload_ro (struct multipath *mpp) {
	return dm_addmap(DM_DEVICE_RELOAD, TGT_MPATH, mpp, 0, ADDMAP_RO);
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
dm_get_map(char * name, unsigned long long * size, char * outparams)
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

	if (snprintf(outparams, PARAMS_SIZE, "%s", params) <= PARAMS_SIZE)
		r = 0;
out:
	dm_task_destroy(dmt);
	return r;
}

extern int
dm_get_uuid(char *name, char *uuid)
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
	if (uuidtmp) {
		if (!strncmp(uuidtmp, UUID_PREFIX, UUID_PREFIX_LEN))
			strcpy(uuid, uuidtmp + UUID_PREFIX_LEN);
		else
			strcpy(uuid, uuidtmp);
	}
	else
		uuid[0] = '\0';

	r = 0;
uuidout:
	dm_task_destroy(dmt);
	return r;
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
		r |= dm_flush_map(names->name);
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

	if (snprintf(message, 32, "fail_path %s\n", path) > 32)
		return 1;

	return dm_message(mapname, message);
}

int
dm_reinstate_path(char * mapname, char * path)
{
	char message[32];

	if (snprintf(message, 32, "reinstate_path %s\n", path) > 32)
		return 1;

	return dm_message(mapname, message);
}

int
dm_queue_if_no_path(char *mapname, int enable)
{
	char *message;

	if (enable)
		message = "queue_if_no_path\n";
	else
		message = "fail_if_no_path\n";

	return dm_message(mapname, message);
}

int
dm_set_pg_timeout(char *mapname, int timeout_val)
{
	char message[24];

	if (snprintf(message, 24, "set_pg_timeout %d", timeout_val) >= 24)
		return 1;
	return dm_message(mapname, message);
}

static int
dm_groupmsg (char * msg, char * mapname, int index)
{
	char message[32];

	if (snprintf(message, 32, "%s_group %i\n", msg, index) > 32)
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
			if (dm_get_map(names->name, &mpp->size, mpp->params))
				goto out1;

			if (dm_get_status(names->name, mpp->status))
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

extern char *
dm_get_name(char *uuid)
{
	struct dm_task *dmt;
	struct dm_info info;
	char *prefixed_uuid, *name = NULL;
	const char *nametmp;

	dmt = dm_task_create(DM_DEVICE_INFO);
	if (!dmt)
		return NULL;

	prefixed_uuid = MALLOC(UUID_PREFIX_LEN + strlen(uuid) + 1);
	if (!prefixed_uuid) {
		condlog(0, "cannot create prefixed uuid : %s\n",
			strerror(errno));
		goto freeout;
	}
	sprintf(prefixed_uuid, UUID_PREFIX "%s", uuid);
	if (!dm_task_set_uuid(dmt, prefixed_uuid))
		goto freeout;

	if (!dm_task_run(dmt))
		goto freeout;

	if (!dm_task_get_info(dmt, &info) || !info.exists)
		goto freeout;

	nametmp = dm_task_get_name(dmt);
	if (nametmp && strlen(nametmp)) {
		name = MALLOC(strlen(nametmp) + 1);
		if (name)
			strcpy(name, nametmp);
	} else {
		condlog(2, "%s: no device-mapper name found", uuid);
	}

freeout:
	if (prefixed_uuid)
		FREE(prefixed_uuid);
	dm_task_destroy(dmt);

	return name;
}

int
dm_geteventnr (char *name)
{
	struct dm_task *dmt;
	struct dm_info info;

	if (!(dmt = dm_task_create(DM_DEVICE_INFO)))
		return 0;

	if (!dm_task_set_name(dmt, name))
		goto out;

	dm_task_no_open_count(dmt);

	if (!dm_task_run(dmt))
		goto out;

	if (!dm_task_get_info(dmt, &info)) {
		info.event_nr = 0;
		goto out;
	}

	if (!info.exists) {
		info.event_nr = 0;
		goto out;
	}

out:
	dm_task_destroy(dmt);

	return info.event_nr;
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
		     * and the multipath mapname and the part mapname start
		     * the same
		     */
		    !strncmp(names->name, mapname, strlen(mapname)) &&

		    /*
		     * and the opencount is 0 for us to allow removal
		     */
		    !dm_get_opencount(names->name) &&

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
				condlog(4, "partition map %s removed",
					names->name);
				dm_simplecmd_flush(DM_DEVICE_REMOVE, names->name, need_sync);
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

	if (!dm_task_set_cookie(dmt, &conf->cookie, 0))
		goto out;
	if (!dm_task_run(dmt))
		goto out;

	r = 1;
out:
	dm_task_destroy(dmt);
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
