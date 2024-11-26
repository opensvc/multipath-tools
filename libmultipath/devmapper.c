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
#include <syslog.h>
#include <sys/sysmacros.h>

#include "util.h"
#include "vector.h"
#include "structs.h"
#include "debug.h"
#include "devmapper.h"
#include "sysfs.h"
#include "wwids.h"
#include "version.h"
#include "time-util.h"

#include "log_pthread.h"
#include <sys/types.h>
#include <time.h>

#define MAX_WAIT 5
#define LOOPS_PER_SEC 5

#define INVALID_VERSION ~0U
static unsigned int dm_library_version[3] = { INVALID_VERSION, };
static unsigned int dm_kernel_version[3] = { INVALID_VERSION, };
static unsigned int dm_mpath_target_version[3] = { INVALID_VERSION, };

static pthread_once_t dm_initialized = PTHREAD_ONCE_INIT;
static pthread_once_t versions_initialized = PTHREAD_ONCE_INIT;
static pthread_mutex_t libmp_dm_lock = PTHREAD_MUTEX_INITIALIZER;

static int dm_conf_verbosity;

#ifdef LIBDM_API_DEFERRED
static int dm_cancel_remove_partmaps(const char * mapname);
#define DR_UNUSED__ /* empty */
#else
#define DR_UNUSED__ __attribute__((unused))
#endif

static int dm_remove_partmaps (const char *mapname, int flags);
static int do_foreach_partmaps(const char *mapname,
			       int (*partmap_func)(const char *, void *),
			       void *data);
static int _dm_queue_if_no_path(const char *mapname, int enable);

#ifndef LIBDM_API_COOKIE
static inline int dm_task_set_cookie(struct dm_task *dmt, uint32_t *c, int a)
{
	return 1;
}

static void libmp_udev_wait(unsigned int c)
{
}

static void dm_udev_set_sync_support(int c)
{
}
#else
static void libmp_udev_wait(unsigned int c)
{
	pthread_mutex_lock(&libmp_dm_lock);
	pthread_cleanup_push(cleanup_mutex, &libmp_dm_lock);
	dm_udev_wait(c);
	pthread_cleanup_pop(1);
}
#endif

const char *dmp_errstr(int rc)
{
	static const char *str[] = {
		[DMP_ERR] = "generic error",
		[DMP_OK] = "success",
		[DMP_NOT_FOUND] = "not found",
		[DMP_NO_MATCH] = "target type mismatch",
		[DMP_EMPTY] = "no target",
		[DMP_LAST__] = "**invalid**",
	};
	if (rc < 0 || rc > DMP_LAST__)
		rc = DMP_LAST__;
	return str[rc];
}

int libmp_dm_task_run(struct dm_task *dmt)
{
	int r;

	pthread_mutex_lock(&libmp_dm_lock);
	pthread_cleanup_push(cleanup_mutex, &libmp_dm_lock);
	r = dm_task_run(dmt);
	pthread_cleanup_pop(1);
	return r;
}

static void cleanup_dm_task(struct dm_task **pdmt)
{
	if (*pdmt)
		dm_task_destroy(*pdmt);
}

__attribute__((format(printf, 4, 5))) static void
dm_write_log (int level, const char *file, int line, const char *f, ...)
{
	va_list ap;

	/*
	 * libdm uses the same log levels as syslog,
	 * except that EMERG/ALERT are not used
	 */
	if (level > LOG_DEBUG)
		level = LOG_DEBUG;

	if (level > dm_conf_verbosity)
		return;

	va_start(ap, f);
	if (logsink != LOGSINK_SYSLOG) {
		if (logsink == LOGSINK_STDERR_WITH_TIME) {
			struct timespec ts;
			char buff[32];

			get_monotonic_time(&ts);
			safe_sprintf(buff, "%ld.%06ld",
				     (long)ts.tv_sec, ts.tv_nsec/1000);
			fprintf(stderr, "%s | ", buff);
		}
		fprintf(stderr, "libdevmapper: %s(%i): ", file, line);
		vfprintf(stderr, f, ap);
		fprintf(stderr, "\n");
	} else {
		condlog(level >= LOG_ERR ? level - LOG_ERR : 0,
			"libdevmapper: %s(%i): ", file, line);
		log_safe(level, f, ap);
	}
	va_end(ap);

	return;
}

static void dm_init(int v)
{
	/*
	 * This maps libdm's standard loglevel _LOG_WARN (= 4), which is rather
	 * quiet in practice, to multipathd's default verbosity 2
	 */
	dm_conf_verbosity = v + 2;
	dm_log_init(&dm_write_log);
}

static void init_dm_library_version(void)
{
	char version[64];
	unsigned int v[3];

	dm_get_library_version(version, sizeof(version));
	if (sscanf(version, "%u.%u.%u ", &v[0], &v[1], &v[2]) != 3) {
		condlog(0, "invalid libdevmapper version %s", version);
		return;
	}
	memcpy(dm_library_version, v, sizeof(dm_library_version));
	condlog(3, "libdevmapper version %u.%.2u.%.2u",
		dm_library_version[0], dm_library_version[1],
		dm_library_version[2]);
}

static int
dm_lib_prereq (void)
{

#if defined(LIBDM_API_HOLD_CONTROL)
	unsigned int minv[3] = {1, 2, 111};
#elif defined(LIBDM_API_GET_ERRNO)
	unsigned int minv[3] = {1, 2, 99};
#elif defined(LIBDM_API_DEFERRED)
	unsigned int minv[3] = {1, 2, 89};
#elif defined(DM_SUBSYSTEM_UDEV_FLAG0)
	unsigned int minv[3] = {1, 2, 82};
#elif defined(LIBDM_API_COOKIE)
	unsigned int minv[3] = {1, 2, 38};
#else
	unsigned int minv[3] = {1, 2, 8};
#endif

	if (VERSION_GE(dm_library_version, minv))
		return 0;
	condlog(0, "libdevmapper version must be >= %u.%.2u.%.2u",
		minv[0], minv[1], minv[2]);
	return 1;
}

static void init_dm_drv_version(void)
{
	char buff[64];
	unsigned int v[3];

	if (!dm_driver_version(buff, sizeof(buff))) {
		condlog(0, "cannot get kernel dm version");
		return;
	}
	if (sscanf(buff, "%u.%u.%u ", &v[0], &v[1], &v[2]) != 3) {
		condlog(0, "invalid kernel dm version '%s'", buff);
		return;
	}
	memcpy(dm_kernel_version, v, sizeof(dm_library_version));
	condlog(3, "kernel device mapper v%u.%u.%u",
		dm_kernel_version[0],
		dm_kernel_version[1],
		dm_kernel_version[2]);
}

static int dm_tgt_version (unsigned int *version, char *str)
{
	bool found = false;
	struct dm_task __attribute__((cleanup(cleanup_dm_task))) *dmt = NULL;
	struct dm_versions *target;
	struct dm_versions *last_target;
	unsigned int *v;

	/*
	 * We have to call dm_task_create() and not libmp_dm_task_create()
	 * here to avoid a recursive invocation of
	 * pthread_once(&dm_initialized), which would cause a deadlock.
	 */
	if (!(dmt = dm_task_create(DM_DEVICE_LIST_VERSIONS)))
		return 1;

	if (!libmp_dm_task_run(dmt)) {
		dm_log_error(2, DM_DEVICE_LIST_VERSIONS, dmt);
		condlog(0, "Cannot communicate with kernel DM");
		return 1;
	}
	target = dm_task_get_versions(dmt);

	do {
		last_target = target;
		if (!strncmp(str, target->name, strlen(str))) {
			found = true;
			break;
		}
		target = (void *) target + target->next;
	} while (last_target != target);

	if (!found) {
		condlog(0, "DM %s kernel driver not loaded", str);
		return 1;
	}
	v = target->version;
	version[0] = v[0];
	version[1] = v[1];
	version[2] = v[2];
	return 0;
}

static void init_dm_mpath_version(void)
{
	if (!dm_tgt_version(dm_mpath_target_version, TGT_MPATH))
		condlog(3, "DM multipath kernel driver v%u.%u.%u",
			dm_mpath_target_version[0],
			dm_mpath_target_version[1],
			dm_mpath_target_version[2]);
}

static int dm_tgt_prereq (unsigned int *ver)
{
	unsigned int minv[3] = {1, 0, 3};

	if (VERSION_GE(dm_mpath_target_version, minv)) {
		if (ver) {
			ver[0] = dm_mpath_target_version[0];
			ver[1] = dm_mpath_target_version[1];
			ver[2] = dm_mpath_target_version[2];
		}
		return 0;
	}

	condlog(0, "DM multipath kernel driver must be >= v%u.%u.%u",
		minv[0], minv[1], minv[2]);
	return 1;
}

static void _init_versions(void)
{
	/* Can't use condlog here because of how VERSION_STRING is defined */
	if (3 <= libmp_verbosity)
		dlog(3, VERSION_STRING);
	init_dm_library_version();
	init_dm_drv_version();
	init_dm_mpath_version();
}

static int init_versions(void) {
	pthread_once(&versions_initialized, _init_versions);
	return (dm_library_version[0] == INVALID_VERSION ||
		dm_kernel_version[0] == INVALID_VERSION ||
		dm_mpath_target_version[0] == INVALID_VERSION);
}

int dm_prereq(unsigned int *v)
{
	if (init_versions())
		return 1;
	if (dm_lib_prereq())
		return 1;
	return dm_tgt_prereq(v);
}

int libmp_get_version(int which, unsigned int version[3])
{
	unsigned int *src_version;

	init_versions();
	switch (which) {
	case DM_LIBRARY_VERSION:
		src_version = dm_library_version;
		break;
	case DM_KERNEL_VERSION:
		src_version = dm_kernel_version;
		break;
	case DM_MPATH_TARGET_VERSION:
		src_version = dm_mpath_target_version;
		break;
	case MULTIPATH_VERSION:
		version[0] = (VERSION_CODE >> 16) & 0xff;
		version[1] = (VERSION_CODE >> 8) & 0xff;
		version[2] = VERSION_CODE & 0xff;
		return 0;
	default:
		condlog(0, "%s: invalid value for 'which'", __func__);
		return 1;
	}
	if (src_version[0] == INVALID_VERSION)
		return 1;
	memcpy(version, src_version, 3 * sizeof(*version));
	return 0;
}

static int libmp_dm_udev_sync = 0;

void libmp_udev_set_sync_support(int on)
{
	libmp_dm_udev_sync = !!on;
}

static bool libmp_dm_init_called;
void libmp_dm_exit(void)
{
	if (!libmp_dm_init_called)
		return;

	/* switch back to default libdm logging */
	dm_log_init(NULL);
#ifdef LIBDM_API_HOLD_CONTROL
	/* make sure control fd is closed in dm_lib_release() */
	dm_hold_control_dev(0);
#endif
}

static void libmp_dm_init(void)
{
	unsigned int version[3];

	if (dm_prereq(version))
		exit(1);
	dm_init(libmp_verbosity);
#ifdef LIBDM_API_HOLD_CONTROL
	dm_hold_control_dev(1);
#endif
	dm_udev_set_sync_support(libmp_dm_udev_sync);
	libmp_dm_init_called = true;
}

static void _do_skip_libmp_dm_init(void)
{
}

void skip_libmp_dm_init(void)
{
	pthread_once(&dm_initialized, _do_skip_libmp_dm_init);
}

struct dm_task*
libmp_dm_task_create(int task)
{
	pthread_once(&dm_initialized, libmp_dm_init);
	return dm_task_create(task);
}

static int
dm_simplecmd (int task, const char *name, int flags, uint16_t udev_flags) {
	int r;
	int udev_wait_flag = (((flags & DMFL_NEED_SYNC) || udev_flags) &&
			      (task == DM_DEVICE_RESUME ||
			       task == DM_DEVICE_REMOVE));
	uint32_t cookie = 0;
	struct dm_task __attribute__((cleanup(cleanup_dm_task))) *dmt = NULL;

	if (!(dmt = libmp_dm_task_create (task)))
		return 0;

	if (!dm_task_set_name (dmt, name))
		return 0;

	dm_task_skip_lockfs(dmt);	/* for DM_DEVICE_RESUME */
#ifdef LIBDM_API_FLUSH
	if (flags & DMFL_NO_FLUSH)
		dm_task_no_flush(dmt);		/* for DM_DEVICE_SUSPEND/RESUME */
#endif
#ifdef LIBDM_API_DEFERRED
	if (flags & DMFL_DEFERRED)
		dm_task_deferred_remove(dmt);
#endif
	if (udev_wait_flag &&
	    !dm_task_set_cookie(dmt, &cookie,
				DM_UDEV_DISABLE_LIBRARY_FALLBACK | udev_flags))
		return 0;

	r = libmp_dm_task_run (dmt);
	if (!r)
		dm_log_error(2, task, dmt);

	if (udev_wait_flag)
			libmp_udev_wait(cookie);
	return r;
}

int dm_simplecmd_flush (int task, const char *name, uint16_t udev_flags)
{
	return dm_simplecmd(task, name, DMFL_NEED_SYNC, udev_flags);
}

int dm_simplecmd_noflush (int task, const char *name, uint16_t udev_flags)
{
	return dm_simplecmd(task, name, DMFL_NO_FLUSH|DMFL_NEED_SYNC, udev_flags);
}

static int
dm_device_remove (const char *name, int flags) {
	return dm_simplecmd(DM_DEVICE_REMOVE, name, flags, 0);
}

static int
dm_addmap (int task, const char *target, struct multipath *mpp,
	   char * params, int ro, uint16_t udev_flags) {
	int r = 0;
	struct dm_task __attribute__((cleanup(cleanup_dm_task))) *dmt = NULL;
	char __attribute__((cleanup(cleanup_charp))) *prefixed_uuid = NULL;

	uint32_t cookie = 0;

	if (task == DM_DEVICE_CREATE && strlen(mpp->wwid) == 0) {
		condlog(1, "%s: refusing to create map with empty WWID",
			mpp->alias);
		return 0;
	}

	/* Need to add this here to allow 0 to be passed in udev_flags */
	udev_flags |= DM_UDEV_DISABLE_LIBRARY_FALLBACK;

	if (!(dmt = libmp_dm_task_create (task)))
		return 0;

	if (!dm_task_set_name (dmt, mpp->alias))
		return 0;

	if (!dm_task_add_target (dmt, 0, mpp->size, target, params))
		return 0;

	if (ro)
		dm_task_set_ro(dmt);

	if (task == DM_DEVICE_CREATE) {
		if (asprintf(&prefixed_uuid, UUID_PREFIX "%s", mpp->wwid) < 0) {
			condlog(0, "cannot create prefixed uuid : %s",
				strerror(errno));
			return 0;
		}
		if (!dm_task_set_uuid(dmt, prefixed_uuid))
			return 0;
		dm_task_skip_lockfs(dmt);
#ifdef LIBDM_API_FLUSH
		dm_task_no_flush(dmt);
#endif
	}

	if (mpp->attribute_flags & (1 << ATTR_MODE) &&
	    !dm_task_set_mode(dmt, mpp->mode))
		return 0;
	if (mpp->attribute_flags & (1 << ATTR_UID) &&
	    !dm_task_set_uid(dmt, mpp->uid))
		return 0;
	if (mpp->attribute_flags & (1 << ATTR_GID) &&
	    !dm_task_set_gid(dmt, mpp->gid))
		return 0;

	condlog(2, "%s: %s [0 %llu %s %s]", mpp->alias,
		task == DM_DEVICE_RELOAD ? "reload" : "addmap", mpp->size,
		target, params);

	if (task == DM_DEVICE_CREATE &&
	    !dm_task_set_cookie(dmt, &cookie, udev_flags))
		return 0;

	r = libmp_dm_task_run (dmt);
	if (!r)
		dm_log_error(2, task, dmt);

	if (task == DM_DEVICE_CREATE)
		libmp_udev_wait(cookie);

	if (r)
		mpp->need_reload = false;
	return r;
}

static uint16_t build_udev_flags(const struct multipath *mpp, int reload)
{
	/* DM_UDEV_DISABLE_LIBRARY_FALLBACK is added in dm_addmap */
	return	(mpp->skip_kpartx == SKIP_KPARTX_ON ?
		 MPATH_UDEV_NO_KPARTX_FLAG : 0) |
		((count_active_pending_paths(mpp) == 0 ||
		  mpp->ghost_delay_tick > 0) ?
		 MPATH_UDEV_NO_PATHS_FLAG : 0) |
		(reload && !mpp->force_udev_reload ?
		 MPATH_UDEV_RELOAD_FLAG : 0);
}

int dm_addmap_create (struct multipath *mpp, char * params)
{
	int ro;
	uint16_t udev_flags = build_udev_flags(mpp, 0);

	for (ro = mpp->force_readonly ? 1 : 0; ro <= 1; ro++) {
		int err;

		if (dm_addmap(DM_DEVICE_CREATE, TGT_MPATH, mpp, params, ro,
			      udev_flags)) {
			if (unmark_failed_wwid(mpp->wwid) ==
			    WWID_FAILED_CHANGED)
				mpp->needs_paths_uevent = 1;
			return 1;
		}
		/*
		 * DM_DEVICE_CREATE is actually DM_DEV_CREATE + DM_TABLE_LOAD.
		 * Failing the second part leaves an empty map. Clean it up.
		 */
		err = errno;
		if (libmp_mapinfo(DM_MAP_BY_NAME | MAPINFO_MPATH_ONLY |
				  MAPINFO_CHECK_UUID,
				(mapid_t) { .str = mpp->alias },
				(mapinfo_t) { .uuid = NULL }) == DMP_EMPTY) {
			condlog(3, "%s: failed to load map (a path might be in use)", mpp->alias);
			dm_device_remove(mpp->alias, 0);
		}
		if (err != EROFS) {
			condlog(3, "%s: failed to load map, error %d",
				mpp->alias, err);
			break;
		}
	}
	if (mark_failed_wwid(mpp->wwid) == WWID_FAILED_CHANGED)
		mpp->needs_paths_uevent = 1;
	return 0;
}

#define ADDMAP_RW 0
#define ADDMAP_RO 1

int dm_addmap_reload(struct multipath *mpp, char *params, int flush)
{
	int r = 0;
	uint16_t udev_flags = build_udev_flags(mpp, 1);

	/*
	 * DM_DEVICE_RELOAD cannot wait on a cookie, as
	 * the cookie will only ever be released after an
	 * DM_DEVICE_RESUME. So call DM_DEVICE_RESUME
	 * after each successful call to DM_DEVICE_RELOAD.
	 */
	if (!mpp->force_readonly)
		r = dm_addmap(DM_DEVICE_RELOAD, TGT_MPATH, mpp, params,
			      ADDMAP_RW, 0);
	if (!r) {
		if (!mpp->force_readonly && errno != EROFS)
			return 0;
		r = dm_addmap(DM_DEVICE_RELOAD, TGT_MPATH, mpp,
			      params, ADDMAP_RO, 0);
	}
	if (r)
		r = dm_simplecmd(DM_DEVICE_RESUME, mpp->alias,
				 DMFL_NEED_SYNC | (flush ? 0 : DMFL_NO_FLUSH),
				 udev_flags);
	if (r)
		return r;

	/* If the resume failed, dm will leave the device suspended, and
	 * drop the new table, so doing a second resume will try using
	 * the original table */
	if (dm_is_suspended(mpp->alias))
		dm_simplecmd(DM_DEVICE_RESUME, mpp->alias,
			     DMFL_NEED_SYNC | (flush ? 0 : DMFL_NO_FLUSH),
			     udev_flags);
	return 0;
}

static bool is_mpath_uuid(const char uuid[DM_UUID_LEN])
{
	return !strncmp(uuid, UUID_PREFIX, UUID_PREFIX_LEN);
}

static bool is_mpath_part_uuid(const char part_uuid[DM_UUID_LEN],
			       const char map_uuid[DM_UUID_LEN])
{
	char c;
	int np, nc;

	if (2 != sscanf(part_uuid, "part%d-%n" UUID_PREFIX "%c", &np, &nc, &c)
	    || np <= 0)
		return false;
	return map_uuid == NULL || !strcmp(part_uuid + nc, map_uuid);
}

bool
has_dm_info(const struct multipath *mpp)
{
	return (mpp && mpp->dmi.exists != 0);
}

static int libmp_set_map_identifier(int flags, mapid_t id, struct dm_task *dmt)
{
	switch (flags & DM_MAP_BY_MASK__) {
	case DM_MAP_BY_UUID:
		if (!id.str || !(*id.str))
			return 0;
		return dm_task_set_uuid(dmt, id.str);
	case DM_MAP_BY_NAME:
		if (!id.str || !(*id.str))
			return 0;
		return dm_task_set_name(dmt, id.str);
	case DM_MAP_BY_DEV:
		if (!dm_task_set_major(dmt, id._u.major))
			return 0;
		return dm_task_set_minor(dmt, id._u.minor);
	case DM_MAP_BY_DEVT:
		if (!dm_task_set_major(dmt, major(id.devt)))
			return 0;
		return dm_task_set_minor(dmt, minor(id.devt));
	default:
		condlog(0, "%s: invalid by_id", __func__);
		return 0;
	}
}

static int libmp_mapinfo__(int flags, mapid_t id, mapinfo_t info, const char *map_id)
{
	/* avoid libmp_mapinfo__ in log messages */
	static const char fname__[] = "libmp_mapinfo";
	struct dm_task __attribute__((cleanup(cleanup_dm_task))) *dmt = NULL;
	struct dm_info dmi;
	int rc, ioctl_nr;
	uint64_t start, length = 0;
	char *target_type = NULL, *params = NULL;
	const char *name = NULL, *uuid = NULL;
	char __attribute__((cleanup(cleanup_charp))) *tmp_target = NULL;
	char __attribute__((cleanup(cleanup_charp))) *tmp_status = NULL;
	bool tgt_set = false;

	/*
	 * If both info.target and info.status are set, we need two
	 * ioctls. Call this function recursively.
	 * If successful, tmp_target will be non-NULL.
	 */
	if (info.target && info.status) {
		rc = libmp_mapinfo__(flags, id,
				     (mapinfo_t) { .target = &tmp_target },
				     map_id);
		if (rc != DMP_OK)
			return rc;
		tgt_set = true;
	}

	/*
	 * The DM_DEVICE_TABLE and DM_DEVICE_STATUS ioctls both fetch the basic
	 * information from DM_DEVICE_INFO, too.
	 * Choose the most lightweight ioctl to fetch all requested info.
	 */
	if (info.target && !info.status)
		ioctl_nr = DM_DEVICE_TABLE;
	else if (info.status || info.size || flags & MAPINFO_TGT_TYPE__)
		ioctl_nr = DM_DEVICE_STATUS;
	else
		ioctl_nr = DM_DEVICE_INFO;

	if (!(dmt = libmp_dm_task_create(ioctl_nr)))
		return DMP_ERR;

	if (!libmp_set_map_identifier(flags, id, dmt)) {
		condlog(2, "%s: failed to set map identifier to %s", fname__, map_id);
		return DMP_ERR;
	}

	if (!libmp_dm_task_run(dmt)) {
		dm_log_error(3, ioctl_nr, dmt);
		if (dm_task_get_errno(dmt) == ENXIO) {
			condlog(2, "%s: map %s not found", fname__, map_id);
			return DMP_NOT_FOUND;
		} else
			return DMP_ERR;
	}

	condlog(4, "%s: DM ioctl %d succeeded for %s",
		fname__, ioctl_nr, map_id);

	if (!dm_task_get_info(dmt, &dmi)) {
		condlog(2, "%s: dm_task_get_info() failed for %s ", fname__, map_id);
		return DMP_ERR;
	} else if(!dmi.exists) {
		condlog(2, "%s: map %s doesn't exist", fname__, map_id);
		return DMP_NOT_FOUND;
	}

	if ((info.name && !(name = dm_task_get_name(dmt)))
	    || ((info.uuid || flags & MAPINFO_CHECK_UUID)
		&& !(uuid = dm_task_get_uuid(dmt))))
		return DMP_ERR;

	if (info.name) {
		strlcpy(info.name, name, WWID_SIZE);
		condlog(4, "%s: %s: name: \"%s\"", fname__, map_id, info.name);
	}
	if (info.uuid) {
		strlcpy(info.uuid, uuid, DM_UUID_LEN);
		condlog(4, "%s: %s: uuid: \"%s\"", fname__, map_id, info.uuid);
	}

	if (info.dmi) {
		memcpy(info.dmi, &dmi, sizeof(*info.dmi));
		condlog(4, "%s: %s %d:%d, %d targets, %s table, %s, %s, %d opened, %u events",
			fname__, map_id,
			info.dmi->major, info.dmi->minor,
			info.dmi->target_count,
			info.dmi->live_table ? "live" :
				info.dmi->inactive_table ? "inactive" : "no",
			info.dmi->suspended ? "suspended" : "active",
			info.dmi->read_only ? "ro" : "rw",
			info.dmi->open_count,
			info.dmi->event_nr);
	}

	if (flags & MAPINFO_CHECK_UUID &&
	    ((flags & MAPINFO_PART_ONLY && !is_mpath_part_uuid(uuid, NULL)) ||
	     (!(flags & MAPINFO_PART_ONLY) && !is_mpath_uuid(uuid)))) {
		condlog(4, "%s: UUID mismatch: %s", fname__, uuid);
		return DMP_NO_MATCH;
	}

	if (info.target || info.status || info.size || flags & MAPINFO_TGT_TYPE__) {
		int lvl = MAPINFO_CHECK_UUID ? 2 : 4;

		if (dm_get_next_target(dmt, NULL, &start, &length,
				       &target_type, &params) != NULL) {
			condlog(lvl, "%s: map %s has multiple targets", fname__, map_id);
			return DMP_NO_MATCH;
		}
		if (!params || !target_type) {
			condlog(lvl, "%s: map %s has no targets", fname__, map_id);
			return DMP_EMPTY;
		}
		if (flags & MAPINFO_TGT_TYPE__) {
			const char *tgt_type = flags & MAPINFO_MPATH_ONLY ? TGT_MPATH : TGT_PART;

			if (strcmp(target_type, tgt_type)) {
				condlog(lvl, "%s: target type mismatch: \"%s\" != \"%s\"",
					fname__, tgt_type, target_type);
				return DMP_NO_MATCH;
			}
		}
	}

	/*
	 * Check possible error conditions.
	 */
	if ((info.status && !(tmp_status = strdup(params)))
	    || (info.target && !tmp_target && !(tmp_target = strdup(params))))
		return DMP_ERR;

	if (info.size) {
		*info.size = length;
		condlog(4, "%s: %s: size: %lld", fname__, map_id, *info.size);
	}

	if (info.target) {
		*info.target = steal_ptr(tmp_target);
		if (!tgt_set)
			condlog(4, "%s: %s: target: \"%s\"", fname__, map_id, *info.target);
	}

	if (info.status) {
		*info.status = steal_ptr(tmp_status);
		condlog(4, "%s: %s: status: \"%s\"", fname__, map_id, *info.status);
	}

	return DMP_OK;
}

/* Helper: format a string describing the map for log messages */
static const char* libmp_map_identifier(int flags, mapid_t id, char buf[BLK_DEV_SIZE])
{
	switch (flags & DM_MAP_BY_MASK__) {
	case DM_MAP_BY_NAME:
	case DM_MAP_BY_UUID:
		return id.str;
	case DM_MAP_BY_DEV:
		safe_snprintf(buf, BLK_DEV_SIZE, "%d:%d", id._u.major, id._u.minor);
		return buf;
	case DM_MAP_BY_DEVT:
		safe_snprintf(buf, BLK_DEV_SIZE, "%d:%d", major(id.devt), minor(id.devt));
		return buf;
	default:
		safe_snprintf(buf, BLK_DEV_SIZE, "*invalid*");
		return buf;
	}
}

int libmp_mapinfo(int flags, mapid_t id, mapinfo_t info)
{
	char idbuf[BLK_DEV_SIZE];

	return libmp_mapinfo__(flags, id, info,
			       libmp_map_identifier(flags, id, idbuf));
}

/**
 * dm_get_wwid(): return WWID for a multipath map
 * @returns:
 *    DMP_OK if successful
 *    DMP_NOT_FOUND if the map doesn't exist
 *    DMP_NO_MATCH if the map exists but is not a multipath map
 *    DMP_ERR for other errors
 * Caller may access uuid if and only if DMP_OK is returned.
 */
int dm_get_wwid(const char *name, char *uuid, int uuid_len)
{
	char tmp[DM_UUID_LEN];
	int rc = libmp_mapinfo(DM_MAP_BY_NAME | MAPINFO_CHECK_UUID,
			       (mapid_t) { .str = name },
			       (mapinfo_t) { .uuid = tmp });

	if (rc != DMP_OK)
		return rc;

	strlcpy(uuid, tmp + UUID_PREFIX_LEN, uuid_len);
	return DMP_OK;
}

int dm_is_mpath(const char *name)
{
	int rc = libmp_mapinfo(DM_MAP_BY_NAME | MAPINFO_MPATH_ONLY | MAPINFO_CHECK_UUID,
			       (mapid_t) { .str = name },
			       (mapinfo_t) { .uuid = NULL });

	switch (rc) {
	case DMP_OK:
		return DM_IS_MPATH_YES;
	case DMP_NOT_FOUND:
	case DMP_NO_MATCH:
	case DMP_EMPTY:
		return DM_IS_MPATH_NO;
	case DMP_ERR:
	default:
		return DM_IS_MPATH_ERR;
	}
}

/* if name is non-NULL, it must point to an array of WWID_SIZE bytes */
int dm_find_map_by_wwid(const char *wwid, char *name, struct dm_info *dmi)
{
	char tmp[DM_UUID_LEN];

	if (safe_sprintf(tmp, UUID_PREFIX "%s", wwid))
		return DMP_ERR;

	return libmp_mapinfo(DM_MAP_BY_UUID | MAPINFO_MPATH_ONLY,
			     (mapid_t) { .str = tmp },
			     (mapinfo_t) { .name = name, .dmi = dmi });
}

static int dm_dev_t (const char *mapname, char *dev_t, int len)
{
	struct dm_info info;

	if (dm_get_info(mapname, &info) != DMP_OK)
		return 1;

	if (safe_snprintf(dev_t, len, "%i:%i", info.major, info.minor))
		return 1;

	return 0;
}

int dm_get_opencount (const char *mapname)
{
	struct dm_info info;

	if (dm_get_info(mapname, &info) != DMP_OK)
		return -1;

	return info.open_count;
}

int
dm_get_major_minor(const char *name, int *major, int *minor)
{
	struct dm_info info;

	if (dm_get_info(name, &info) != DMP_OK)
		return -1;

	*major = info.major;
	*minor = info.minor;
	return 0;
}

static int
has_partmap(const char *name __attribute__((unused)),
	    void *data __attribute__((unused)))
{
	return 1;
}

/*
 * This will be called from mpath_in_use, for each partition.
 * If the partition itself in use, returns 1 immediately, causing
 * do_foreach_partmaps() to stop iterating and return 1.
 * Otherwise, increases the partition count.
 */
static int count_partitions(const char *name, void *data)
{
	int *ret_count = (int *)data;
	int open_count = dm_get_opencount(name);

	if (open_count)
		return 1;
	(*ret_count)++;
	return 0;
}

int mpath_in_use(const char *name)
{
	int open_count = dm_get_opencount(name);

	if (open_count) {
		int part_count = 0;

		if (do_foreach_partmaps(name, count_partitions, &part_count)) {
			condlog(4, "%s: %s has open partitions", __func__, name);
			return 1;
		}
		condlog(4, "%s: %s: %d openers, %d partitions", __func__, name,
			open_count, part_count);
		return open_count > part_count;
	}
	return 0;
}

int dm_flush_map__ (const char *mapname, int flags, int retries)
{
	int r;
	int queue_if_no_path = 0;
	int udev_flags = 0;
	char *params __attribute__((cleanup(cleanup_charp))) = NULL;

	r = libmp_mapinfo(DM_MAP_BY_NAME | MAPINFO_MPATH_ONLY | MAPINFO_CHECK_UUID,
			  (mapid_t) { .str = mapname },
			  (mapinfo_t) { .target = &params });
	if (r != DMP_OK && r != DMP_EMPTY)
		return DM_FLUSH_OK; /* nothing to do */

	/* device mapper will not let you resume an empty device */
	if (r == DMP_EMPTY)
		flags &= ~DMFL_SUSPEND;

	/* if the device currently has no partitions, do not
	   run kpartx on it if you fail to delete it */
	if (do_foreach_partmaps(mapname, has_partmap, NULL) == 0)
		udev_flags |= MPATH_UDEV_NO_KPARTX_FLAG;

	/* If you aren't doing a deferred remove, make sure that no
	 * devices are in use */
	if (!(flags & DMFL_DEFERRED) && mpath_in_use(mapname))
			return DM_FLUSH_BUSY;

	if ((flags & DMFL_SUSPEND) &&
	    strstr(params, "queue_if_no_path")) {
		if (!_dm_queue_if_no_path(mapname, 0))
			queue_if_no_path = 1;
		else
			/* Leave queue_if_no_path alone if unset failed */
			queue_if_no_path = -1;
	}

	if ((r = dm_remove_partmaps(mapname, flags)))
		return r;

	if (!(flags & DMFL_DEFERRED) && dm_get_opencount(mapname)) {
		condlog(2, "%s: map in use", mapname);
		return DM_FLUSH_BUSY;
	}

	do {
		if ((flags & DMFL_SUSPEND) && queue_if_no_path != -1)
			dm_simplecmd_flush(DM_DEVICE_SUSPEND, mapname, 0);

		r = dm_device_remove(mapname, flags);

		if (r) {
			if ((flags & DMFL_DEFERRED) && dm_map_present(mapname)) {
				condlog(4, "multipath map %s remove deferred",
					mapname);
				return DM_FLUSH_DEFERRED;
			}
			condlog(4, "multipath map %s removed", mapname);
			return DM_FLUSH_OK;
		} else if (dm_is_mpath(mapname) != DM_IS_MPATH_YES) {
			condlog(4, "multipath map %s removed externally",
				mapname);
			return DM_FLUSH_OK; /* raced. someone else removed it */
		} else {
			condlog(2, "failed to remove multipath map %s",
				mapname);
			if ((flags & DMFL_SUSPEND) && queue_if_no_path != -1) {
				dm_simplecmd_noflush(DM_DEVICE_RESUME,
						     mapname, udev_flags);
			}
		}
		if (retries)
			sleep(1);
	} while (retries-- > 0);

	if (queue_if_no_path == 1 && _dm_queue_if_no_path(mapname, 1) != 0)
		return DM_FLUSH_FAIL_CANT_RESTORE;

	return DM_FLUSH_FAIL;
}

int
dm_flush_map_nopaths(const char *mapname, int deferred_remove DR_UNUSED__)
{
	int flags = DMFL_NEED_SYNC;

#ifdef LIBDM_API_DEFERRED
	flags |= ((deferred_remove == DEFERRED_REMOVE_ON ||
		   deferred_remove == DEFERRED_REMOVE_IN_PROGRESS) ?
		  DMFL_DEFERRED : 0);
#endif
	return dm_flush_map__(mapname, flags, 0);
}

int dm_flush_maps(int retries)
{
	int r = DM_FLUSH_FAIL;
	struct dm_task __attribute__((cleanup(cleanup_dm_task))) *dmt = NULL;
	struct dm_names *names;
	unsigned next = 0;

	if (!(dmt = libmp_dm_task_create (DM_DEVICE_LIST)))
		return r;

	if (!libmp_dm_task_run (dmt)) {
		dm_log_error(3, DM_DEVICE_LIST, dmt);
		return r;
	}

	if (!(names = dm_task_get_names (dmt)))
		return r;

	r = DM_FLUSH_OK;
	if (!names->dev)
		return r;

	do {
		int ret;
		ret = dm_suspend_and_flush_map(names->name, retries);
		if (ret == DM_FLUSH_FAIL ||
		    (r != DM_FLUSH_FAIL && ret == DM_FLUSH_BUSY))
			r = ret;
		next = names->next;
		names = (void *) names + next;
	} while (next);

	return r;
}

int
dm_message(const char * mapname, char * message)
{
	struct dm_task __attribute__((cleanup(cleanup_dm_task))) *dmt = NULL;

	if (!(dmt = libmp_dm_task_create(DM_DEVICE_TARGET_MSG)))
		return 1;

	if (!dm_task_set_name(dmt, mapname))
		goto out;

	if (!dm_task_set_sector(dmt, 0))
		goto out;

	if (!dm_task_set_message(dmt, message))
		goto out;

	if (!libmp_dm_task_run(dmt)) {
		dm_log_error(2, DM_DEVICE_TARGET_MSG, dmt);
		goto out;
	}

	return 0;
out:
	condlog(0, "DM message failed [%s]", message);
	return 1;
}

int
dm_fail_path(const char * mapname, char * path)
{
	char message[32];

	if (snprintf(message, 32, "fail_path %s", path) > 32)
		return 1;

	return dm_message(mapname, message);
}

int
dm_reinstate_path(const char * mapname, char * path)
{
	char message[32];

	if (snprintf(message, 32, "reinstate_path %s", path) > 32)
		return 1;

	return dm_message(mapname, message);
}

static int
_dm_queue_if_no_path(const char *mapname, int enable)
{
	char *message;

	if (enable)
		message = "queue_if_no_path";
	else
		message = "fail_if_no_path";

	return dm_message(mapname, message);
}

int dm_queue_if_no_path(struct multipath *mpp, int enable)
{
	int r;
	static const char no_path_retry[] = "queue_if_no_path";

	if ((r = _dm_queue_if_no_path(mpp->alias, enable)) == 0) {
		if (enable)
			add_feature(&mpp->features, no_path_retry);
		else
			remove_feature(&mpp->features, no_path_retry);
	}
	return r;
}

static int
dm_groupmsg (const char * msg, const char * mapname, int index)
{
	char message[32];

	if (snprintf(message, 32, "%s_group %i", msg, index) > 32)
		return 1;

	return dm_message(mapname, message);
}

int
dm_switchgroup(const char * mapname, int index)
{
	return dm_groupmsg("switch", mapname, index);
}

int
dm_enablegroup(const char * mapname, int index)
{
	return dm_groupmsg("enable", mapname, index);
}

int
dm_disablegroup(const char * mapname, int index)
{
	return dm_groupmsg("disable", mapname, index);
}

static int dm_get_multipath(const char *name, struct multipath **pmpp)
{
	struct multipath __attribute__((cleanup(cleanup_multipath))) *mpp = NULL;
	char uuid[DM_UUID_LEN];
	int rc;

	mpp = alloc_multipath();
	if (!mpp)
		return DMP_ERR;

	mpp->alias = strdup(name);

	if (!mpp->alias)
		return DMP_ERR;

	if ((rc = libmp_mapinfo(DM_MAP_BY_NAME | MAPINFO_CHECK_UUID |
				MAPINFO_MPATH_ONLY,
			  (mapid_t) { .str = name },
			  (mapinfo_t) {
				  .size = &mpp->size,
				  .uuid = uuid,
				  .dmi = &mpp->dmi,
			  })) != DMP_OK)
		return rc;

	strlcpy(mpp->wwid, uuid + UUID_PREFIX_LEN, sizeof(mpp->wwid));
	*pmpp = steal_ptr(mpp);

	return DMP_OK;
}

int dm_get_maps(vector mp)
{
	struct multipath *mpp = NULL;
	struct dm_task __attribute__((cleanup(cleanup_dm_task))) *dmt = NULL;
	struct dm_names *names;
	unsigned next = 0;

	if (!mp)
		return 1;

	if (!(dmt = libmp_dm_task_create(DM_DEVICE_LIST)))
		return 1;

	if (!libmp_dm_task_run(dmt)) {
		dm_log_error(3, DM_DEVICE_LIST, dmt);
		return 1;
	}

	if (!(names = dm_task_get_names(dmt)))
		return 1;

	if (!names->dev) {
		/* this is perfectly valid */
		return 0;
	}

	do {
		switch (dm_get_multipath(names->name, &mpp)) {
		case DMP_OK:
			if (!vector_alloc_slot(mp)) {
				free_multipath(mpp, KEEP_PATHS);
				return 1;
			}
			vector_set_slot(mp, mpp);
			break;
		default:
			break;
		}
		next = names->next;
		names = (void *) names + next;
	} while (next);

	return 0;
}

int
dm_geteventnr (const char *name)
{
	struct dm_info info;

	if (dm_get_info(name, &info) != DMP_OK)
		return -1;

	return info.event_nr;
}

int
dm_is_suspended(const char *name)
{
	struct dm_info info;

	if (dm_get_info(name, &info) != DMP_OK)
		return -1;

	return info.suspended;
}

char *dm_mapname(int major, int minor)
{
	char name[WWID_SIZE];

	if (libmp_mapinfo(DM_MAP_BY_DEV,
			  (mapid_t) { ._u = { major, minor } },
			  (mapinfo_t) { .name = name }) != DMP_OK)
		return NULL;
	return strdup(name);
}

static bool
is_valid_partmap(const char *name, const char *map_dev_t,
		 const char *map_uuid) {
	int r;
	char __attribute__((cleanup(cleanup_charp))) *params = NULL;
	char *p;
	char part_uuid[DM_UUID_LEN];

	r = libmp_mapinfo(DM_MAP_BY_NAME | MAPINFO_PART_ONLY | MAPINFO_CHECK_UUID,
			  (mapid_t) { .str = name },
			  (mapinfo_t) { .uuid = part_uuid, .target = &params});

	/* There must be a single linear target */
	if (r != DMP_OK)
		return false;

	/*
	 * and the uuid of the target must be a partition of the uuid of the
	 * multipath device
	 */
	if (!is_mpath_part_uuid(part_uuid, map_uuid))
		return false;

	/* and the table must map over the multipath map */
	return ((p = strstr(params, map_dev_t)) &&
		!isdigit(*(p + strlen(map_dev_t))));
}

static int
do_foreach_partmaps (const char *mapname,
		     int (*partmap_func)(const char *, void *),
		     void *data)
{
	struct dm_task __attribute__((cleanup(cleanup_dm_task))) *dmt = NULL;
	struct dm_names *names;
	unsigned next = 0;
	char dev_t[BLK_DEV_SIZE];
	char map_uuid[DM_UUID_LEN];
	struct dm_info info;

	if (libmp_mapinfo(DM_MAP_BY_NAME | MAPINFO_CHECK_UUID,
			  (mapid_t) { .str = mapname },
			  (mapinfo_t) { .uuid = map_uuid, .dmi = &info }) != DMP_OK)
		return 1;

	if (safe_sprintf(dev_t, "%i:%i", info.major, info.minor))
		return 1;

	if (!(dmt = libmp_dm_task_create(DM_DEVICE_LIST)))
		return 1;

	if (!libmp_dm_task_run(dmt))
		return 1;

	if (!(names = dm_task_get_names(dmt)))
		return 1;

	if (!names->dev)
		/* this is perfectly valid */
		return 0;

	do {
		if (is_valid_partmap(names->name, dev_t, map_uuid) &&
		    (partmap_func(names->name, data) != 0))
			return 1;

		next = names->next;
		names = (void*) names + next;
	} while (next);

	return 0;
}

struct remove_data {
	int flags;
};

static int
remove_partmap(const char *name, void *data)
{
	struct remove_data *rd = (struct remove_data *)data;

	if (!(rd->flags & DMFL_DEFERRED) && dm_get_opencount(name)) {
		condlog(2, "%s: map in use", name);
		return DM_FLUSH_BUSY;
	}
	condlog(3, "partition map %s removed", name);
	dm_device_remove(name, rd->flags);
	return DM_FLUSH_OK;
}

static int
dm_remove_partmaps (const char * mapname, int flags)
{
	struct remove_data rd = { flags };
	return do_foreach_partmaps(mapname, remove_partmap, &rd);
}

#ifdef LIBDM_API_DEFERRED

static int
cancel_remove_partmap (const char *name, void *unused __attribute__((unused)))
{
	if (dm_get_opencount(name))
		dm_cancel_remove_partmaps(name);
	if (dm_message(name, "@cancel_deferred_remove") != 0)
		condlog(0, "%s: can't cancel deferred remove: %s", name,
			strerror(errno));
	return 0;
}

static int
dm_get_deferred_remove (const char * mapname)
{
	struct dm_info info;

	if (dm_get_info(mapname, &info) != DMP_OK)
		return -1;

	return info.deferred_remove;
}

static int
dm_cancel_remove_partmaps(const char * mapname) {
	return do_foreach_partmaps(mapname, cancel_remove_partmap, NULL);
}

int
dm_cancel_deferred_remove (struct multipath *mpp)
{
	int r = 0;

	if (!dm_get_deferred_remove(mpp->alias))
		return 0;
	if (mpp->deferred_remove == DEFERRED_REMOVE_IN_PROGRESS)
		mpp->deferred_remove = DEFERRED_REMOVE_ON;

	dm_cancel_remove_partmaps(mpp->alias);
	r = dm_message(mpp->alias, "@cancel_deferred_remove");
	if (r)
		condlog(0, "%s: can't cancel deferred remove: %s", mpp->alias,
				strerror(errno));
	else
		condlog(2, "%s: canceled deferred remove", mpp->alias);
	return r;
}

#else

int
dm_cancel_deferred_remove (struct multipath *mpp __attribute__((unused)))
{
	return 0;
}

#endif

struct rename_data {
	const char *old;
	char *new;
	char *delim;
};

static int
rename_partmap (const char *name, void *data)
{
	char *buff = NULL;
	int offset;
	struct rename_data *rd = (struct rename_data *)data;

	if (strncmp(name, rd->old, strlen(rd->old)) != 0)
		return 0;
	for (offset = strlen(rd->old); name[offset] && !(isdigit(name[offset])); offset++); /* do nothing */
	if (asprintf(&buff, "%s%s%s", rd->new, rd->delim, name + offset) >= 0) {
		dm_rename(name, buff, rd->delim, SKIP_KPARTX_OFF);
		free(buff);
		condlog(4, "partition map %s renamed", name);
	} else
		condlog(1, "failed to rename partition map %s", name);
	return 0;
}

int
dm_rename_partmaps (const char * old, char * new, char *delim)
{
	struct rename_data rd;

	rd.old = old;
	rd.new = new;

	if (delim)
		rd.delim = delim;
	else {
		if (isdigit(new[strlen(new)-1]))
			rd.delim = "p";
		else
			rd.delim = "";
	}
	return do_foreach_partmaps(old, rename_partmap, &rd);
}

int
dm_rename (const char * old, char * new, char *delim, int skip_kpartx)
{
	int r = 0;
	struct dm_task __attribute__((cleanup(cleanup_dm_task))) *dmt = NULL;
	uint32_t cookie = 0;
	uint16_t udev_flags = DM_UDEV_DISABLE_LIBRARY_FALLBACK | ((skip_kpartx == SKIP_KPARTX_ON)? MPATH_UDEV_NO_KPARTX_FLAG : 0);

	if (dm_rename_partmaps(old, new, delim))
		return r;

	if (!(dmt = libmp_dm_task_create(DM_DEVICE_RENAME)))
		return r;

	if (!dm_task_set_name(dmt, old))
		return r;

	if (!dm_task_set_newname(dmt, new))
		return r;

	if (!dm_task_set_cookie(dmt, &cookie, udev_flags))
		return r;

	r = libmp_dm_task_run(dmt);
	if (!r)
		dm_log_error(2, DM_DEVICE_RENAME, dmt);

	libmp_udev_wait(cookie);
	return r;
}

void dm_reassign_deps(char *table, const char *dep, const char *newdep)
{
	char *n, *newtable;
	const char *p;

	newtable = strdup(table);
	if (!newtable)
		return;
	p = strstr(newtable, dep);
	n = table + (p - newtable);
	strcpy(n, newdep);
	n += strlen(newdep);
	p += strlen(dep);
	strcat(n, p);
	free(newtable);
}

int dm_reassign_table(const char *name, char *old, char *new)
{
	int modified = 0;
	uint64_t start, length;
	struct dm_task __attribute__((cleanup(cleanup_dm_task))) *dmt = NULL;
	struct dm_task __attribute__((cleanup(cleanup_dm_task))) *reload_dmt = NULL;
	char *target, *params = NULL;
	char *buff;
	void *next = NULL;

	if (!(dmt = libmp_dm_task_create(DM_DEVICE_TABLE)))
		return 0;

	if (!dm_task_set_name(dmt, name))
		return 0;

	if (!libmp_dm_task_run(dmt)) {
		dm_log_error(3, DM_DEVICE_TABLE, dmt);
		return 0;
	}
	if (!(reload_dmt = libmp_dm_task_create(DM_DEVICE_RELOAD)))
		return 0;
	if (!dm_task_set_name(reload_dmt, name))
		return 0;

	do {
		next = dm_get_next_target(dmt, next, &start, &length,
					  &target, &params);
		if (!target || !params) {
			/*
			 * We can't call dm_task_add_target() with
			 * invalid parameters. But simply dropping this
			 * target feels wrong, too. Abort and warn.
			 */
			condlog(1, "%s: invalid target found in map %s",
				__func__, name);
			return 0;
		}
		buff = strdup(params);
		if (!buff) {
			condlog(3, "%s: failed to replace target %s, "
				"out of memory", name, target);
			return 0;
		}
		if (strcmp(target, TGT_MPATH) && strstr(params, old)) {
			condlog(3, "%s: replace target %s %s",
				name, target, buff);
			dm_reassign_deps(buff, old, new);
			condlog(3, "%s: with target %s %s",
				name, target, buff);
			modified++;
		}
		dm_task_add_target(reload_dmt, start, length, target, buff);
		free(buff);
	} while (next);

	if (modified) {
		if (!libmp_dm_task_run(reload_dmt)) {
			dm_log_error(3, DM_DEVICE_RELOAD, reload_dmt);
			condlog(3, "%s: failed to reassign targets", name);
			return 0;
		}
		dm_simplecmd_noflush(DM_DEVICE_RESUME, name,
				     MPATH_UDEV_RELOAD_FLAG);
	}
	return 1;
}


/*
 * Reassign existing device-mapper table(s) to not use
 * the block devices but point to the multipathed
 * device instead
 */
int dm_reassign(const char *mapname)
{
	struct dm_deps *deps;
	struct dm_task __attribute__((cleanup(cleanup_dm_task))) *dmt = NULL;
	struct dm_info info;
	char dev_t[32], dm_dep[32];
	unsigned int i;

	if (dm_dev_t(mapname, &dev_t[0], 32)) {
		condlog(3, "%s: failed to get device number", mapname);
		return 1;
	}

	if (!(dmt = libmp_dm_task_create(DM_DEVICE_DEPS))) {
		condlog(3, "%s: couldn't make dm task", mapname);
		return 0;
	}

	if (!dm_task_set_name(dmt, mapname))
		return 0;

	if (!libmp_dm_task_run(dmt)) {
		dm_log_error(3, DM_DEVICE_DEPS, dmt);
		return 0;
	}

	if (!dm_task_get_info(dmt, &info))
		return 0;

	if (!(deps = dm_task_get_deps(dmt)))
		return 0;

	if (!info.exists)
		return 0;

	for (i = 0; i < deps->count; i++) {
		sprintf(dm_dep, "%d:%d",
			major(deps->device[i]),
			minor(deps->device[i]));
		sysfs_check_holders(dm_dep, dev_t);
	}

	return 1;
}

int dm_setgeometry(struct multipath *mpp)
{
	struct dm_task __attribute__((cleanup(cleanup_dm_task))) *dmt = NULL;
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

	if (!(dmt = libmp_dm_task_create(DM_DEVICE_SET_GEOMETRY)))
		return 0;

	if (!dm_task_set_name(dmt, mpp->alias))
		return 0;

	/* What a sick interface ... */
	snprintf(heads, 4, "%u", pp->geom.heads);
	snprintf(sectors, 4, "%u", pp->geom.sectors);
	snprintf(cylinders, 10, "%u", pp->geom.cylinders);
	snprintf(start, 32, "%lu", pp->geom.start);
	if (!dm_task_set_geometry(dmt, cylinders, heads, sectors, start)) {
		condlog(3, "%s: Failed to set geometry", mpp->alias);
		return 0;
	}

	r = libmp_dm_task_run(dmt);
	if (!r)
		dm_log_error(3, DM_DEVICE_SET_GEOMETRY, dmt);

	return r;
}
