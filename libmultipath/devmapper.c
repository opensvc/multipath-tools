/*
 * snippets copied from device-mapper dmsetup.c
 * Copyright (c) 2004, 2005 Christophe Varoqui
 * Copyright (c) 2005 Kiyoshi Ueda, NEC
 * Copyright (c) 2005 Patrick Caulfield, Redhat
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libdevmapper.h>
#include <ctype.h>
#include <linux/kdev_t.h>
#include <unistd.h>
#include <errno.h>

#include <checkers.h>

#include "vector.h"
#include "structs.h"
#include "debug.h"
#include "memory.h"
#include "devmapper.h"

#define MAX_WAIT 5
#define LOOPS_PER_SEC 5

#define UUID_PREFIX "mpath-"
#define UUID_PREFIX_LEN 6

static void
dm_dummy_log (int level, const char *file, int line, const char *f, ...)
{
	return;
}

void
dm_restore_log (void)
{
	dm_log_init(NULL);
}

void
dm_shut_log (void)
{
	dm_log_init(&dm_dummy_log);
}

static int
dm_libprereq (void)
{
	char version[64];
	int v[3];
	int minv[3] = {1, 2, 11};

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
dm_prereq (char * str)
{
	if (dm_libprereq())
		return 1;
	return dm_drvprereq(str);
}

extern int
dm_simplecmd (int task, const char *name) {
	int r = 0;
	struct dm_task *dmt;

	if (!(dmt = dm_task_create (task)))
		return 0;

	if (!dm_task_set_name (dmt, name))
		goto out;

	dm_task_no_open_count(dmt);
	dm_task_skip_lockfs(dmt);       /* for DM_DEVICE_RESUME */

	r = dm_task_run (dmt);

	out:
	dm_task_destroy (dmt);
	return r;
}

extern int
dm_addmap (int task, const char *name, const char *target,
	   const char *params, unsigned long long size, const char *uuid) {
	int r = 0;
	struct dm_task *dmt;
	char *prefixed_uuid = NULL;

	if (!(dmt = dm_task_create (task)))
		return 0;

	if (!dm_task_set_name (dmt, name))
		goto addout;

	if (!dm_task_add_target (dmt, 0, size, target, params))
		goto addout;

	if (uuid){
		prefixed_uuid = MALLOC(UUID_PREFIX_LEN + strlen(uuid) + 1);
		if (!prefixed_uuid) {
			condlog(0, "cannot create prefixed uuid : %s\n",
				strerror(errno));
			goto addout;
		}
		sprintf(prefixed_uuid, UUID_PREFIX "%s", uuid);
		if (!dm_task_set_uuid(dmt, prefixed_uuid))
			goto freeout;
	}

	dm_task_no_open_count(dmt);

	r = dm_task_run (dmt);

	freeout:
	if (prefixed_uuid)
		free(prefixed_uuid);

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
dm_type(char * name, char * type)
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
dm_dev_t (char * mapname, char * dev_t, int len)
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
dm_get_opencount (char * mapname)
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
dm_flush_map (char * mapname, char * type)
{
	int r;

	if (!dm_map_present(mapname))
		return 0;

	if (dm_type(mapname, type) <= 0)
		return 1;

	if (dm_remove_partmaps(mapname))
		return 1;

	if (dm_get_opencount(mapname)) {
		condlog(2, "%s: map in use", mapname);
		return 1;
	}	

	r = dm_simplecmd(DM_DEVICE_REMOVE, mapname);

	if (r) {
		condlog(4, "multipath map %s removed", mapname);
		return 0;
	}
	return 1;
}

extern int
dm_flush_maps (char * type)
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
		r += dm_flush_map(names->name, type);
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
dm_get_maps (vector mp, char * type)
{
	struct multipath * mpp;
	int r = 1;
	int info;
	struct dm_task *dmt;
	struct dm_names *names;
	unsigned next = 0;

	if (!type || !mp)
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
		info = dm_type(names->name, type);

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

extern int
dm_get_name(char *uuid, char *type, char *name)
{
	vector vec;
	struct multipath *mpp;
	int i;

	vec = vector_alloc();

	if (!vec)
		return 0;

	if (dm_get_maps(vec, type)) {
		vector_free(vec);
		return 0;
	}

	vector_foreach_slot(vec, mpp, i) {
		if (!strcmp(uuid, mpp->wwid)) {
			vector_free(vec);
			strcpy(name, mpp->alias);
			return 1;
		}
	}

	vector_free(vec);
	return 0;
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
		dm_shut_log();
		r = dm_task_run(dmt);
		dm_restore_log();

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
dm_remove_partmaps (char * mapname)
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
		    (dm_type(names->name, "linear") > 0) &&

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
				dm_simplecmd(DM_DEVICE_REMOVE, names->name);
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
	if (r)
		memset(*dmi, 0, sizeof(struct dm_info));

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
		    (dm_type(names->name, "linear") > 0) &&

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

	if (!dm_task_run(dmt))
		goto out;

	r = 1;
out:
	dm_task_destroy(dmt);
	return r;
}
