#ifndef DEVMAPPER_H_INCLUDED
#define DEVMAPPER_H_INCLUDED

#include <sys/sysmacros.h>
#include <linux/dm-ioctl.h>
#include "autoconfig.h"
#include "structs.h"

#define TGT_MPATH	"multipath"
#define TGT_PART	"linear"

#ifdef DM_SUBSYSTEM_UDEV_FLAG0
#define MPATH_UDEV_RELOAD_FLAG DM_SUBSYSTEM_UDEV_FLAG0
#else
#define MPATH_UDEV_RELOAD_FLAG 0
#endif

#ifdef DM_SUBSYSTEM_UDEV_FLAG1
#define MPATH_UDEV_NO_KPARTX_FLAG DM_SUBSYSTEM_UDEV_FLAG1
#else
#define MPATH_UDEV_NO_KPARTX_FLAG 0
#endif

#ifdef DM_SUBSYSTEM_UDEV_FLAG2
#define MPATH_UDEV_NO_PATHS_FLAG DM_SUBSYSTEM_UDEV_FLAG2
#else
#define MPATH_UDEV_NO_PATHS_FLAG 0
#endif

#define UUID_PREFIX "mpath-"
#define UUID_PREFIX_LEN (sizeof(UUID_PREFIX) - 1)

enum {
	DMP_ERR,
	DMP_OK,
	DMP_NOT_FOUND,
	DMP_NO_MATCH,
	DMP_EMPTY,
	DMP_LAST__,
};

const char* dmp_errstr(int rc);

/**
 * input flags for libmp_mapinfo()
 */
enum {
	/** DM_MAP_BY_NAME: identify map by device-mapper name from @name */
	DM_MAP_BY_NAME      = 0,
	/** DM_MAP_BY_UUID: identify map by device-mapper UUID from @uuid */
	DM_MAP_BY_UUID,
	/** DM_MAP_BY_DEV: identify map by major/minor number from @dmi */
	DM_MAP_BY_DEV,
	/** DM_MAP_BY_DEVT: identify map by a dev_t */
	DM_MAP_BY_DEVT,
	DM_MAP_BY_MASK__    = (1 << 8) - 1,
	/* Fail if target type is not multipath */
	MAPINFO_MPATH_ONLY  = (1 << 8),
	/* Fail if target type is not "partition" (linear) */
	MAPINFO_PART_ONLY   = (1 << 9),
	MAPINFO_TGT_TYPE__  = (MAPINFO_MPATH_ONLY | MAPINFO_PART_ONLY),
	/*
	 * Fail if the UUID doesn't match the expected UUID format
	 * If combined with MAPINFO_PART_ONLY, checks for partition UUID format
	 * ("part<N>-mpath-xyz").
	 * Otherwise (whether or not MAPINFO_MPATH_ONLY is set) checks for
	 * multipath UUID format ("mpath-xyz").
	 */
	MAPINFO_CHECK_UUID  = (1 << 10),
};

typedef union libmp_map_identifier {
	const char *str;
	struct {
		int major;
		int minor;
	} _u;
	dev_t devt;
} mapid_t;

typedef struct libmp_map_info {
	/** @name: name of the map.
	 * If non-NULL, it must point to an array of WWID_SIZE bytes
	 */
	char *name;
	/** @uuid: UUID of the map.
	 * If non-NULL it must point to an array of DM_UUID_LEN bytes
	 */
	char *uuid;
	/** @dmi: Basic info, must point to a valid dm_info buffer if non-NULL */
	struct dm_info *dmi;
	/** @target: target params, *@target will be allocated if @target is non-NULL*/
	char **target;
	/** @size: target size. */
	unsigned long long *size;
	/** @status: target status, *@status will be allocated if @status is non-NULL */
	char **status;
} mapinfo_t;

/**
 * libmp_mapinfo(): obtain information about a map from the kernel
 * @param flags: see enum values above.
 *     Exactly one of DM_MAP_BY_NAME, DM_MAP_BY_UUID, and DM_MAP_BY_DEV must be set.
 * @param id: string or major/minor to identify the map to query
 * @param info: output parameters, see above. Non-NULL elements will be filled in.
 * @returns:
 *     DMP_OK if successful.
 *     DMP_NOT_FOUND if the map wasn't found, or has no or multiple targets.
 *     DMP_NO_MATCH if the map didn't match @tgt_type (see above) or didn't
 *                  have a multipath uuid prefix.
 *     DMP_EMPTY if the map has no table. Note. The check for matching uuid
 *               prefix will happen first, but the check for matching
 *               tgt_type will happen afterwards.
 *     DMP_ERR if some other error occurred.
 *
 * This function obtains the requested information for the device-mapper map
 * identified by the input parameters.
 * If non-NULL, the name, uuid, and dmi output paramters may be filled in for
 * any return value besides DMP_NOT_FOUND and will always be filled in for
 * return values other than DMP_NOT_FOUND and DMP_ERR.
 * The other parameters are only filled in if the return value is DMP_OK.
 * For target / status / size information, the  map's table should contain
 * only one target (usually multipath or linear).
 */
int libmp_mapinfo(int flags, mapid_t id, mapinfo_t info);

static inline int dm_get_info(const char *mapname, struct dm_info *info)
{
	return libmp_mapinfo(DM_MAP_BY_NAME,
			     (mapid_t) { .str = mapname },
			     (mapinfo_t) { .dmi = info });
}

static inline int dm_map_present(const char *mapname)
{
	return libmp_mapinfo(DM_MAP_BY_NAME,
			     (mapid_t) { .str = mapname },
			     (mapinfo_t) { .name = NULL }) == DMP_OK;
}

int dm_prereq(unsigned int *v);
void skip_libmp_dm_init(void);
void libmp_dm_exit(void);
void libmp_udev_set_sync_support(int on);
struct dm_task *libmp_dm_task_create(int task);
int dm_simplecmd_flush (int task, const char *name, uint16_t udev_flags);
int dm_simplecmd_noflush (int task, const char *name, uint16_t udev_flags);
int dm_addmap_create (struct multipath *mpp, char *params);
int dm_addmap_reload (struct multipath *mpp, char *params, int flush);
int dm_find_map_by_wwid(const char *wwid, char *name, struct dm_info *dmi);

enum {
	DM_IS_MPATH_NO,
	DM_IS_MPATH_YES,
	DM_IS_MPATH_ERR,
};

int dm_is_mpath(const char *name);

enum {
	DM_FLUSH_OK = 0,
	DM_FLUSH_FAIL,
	DM_FLUSH_FAIL_CANT_RESTORE,
	DM_FLUSH_DEFERRED,
	DM_FLUSH_BUSY,
};

int mpath_in_use(const char *name);

enum {
	DMFL_NONE      = 0,
	DMFL_NEED_SYNC = 1 << 0,
	DMFL_DEFERRED  = 1 << 1,
	DMFL_SUSPEND   = 1 << 2,
	DMFL_NO_FLUSH  = 1 << 3,
};

int dm_flush_map__ (const char *mapname, int flags, int retries);
#define dm_flush_map(mapname) dm_flush_map__(mapname, DMFL_NEED_SYNC, 0)
#define dm_suspend_and_flush_map(mapname, retries) \
	dm_flush_map__(mapname, DMFL_NEED_SYNC|DMFL_SUSPEND, retries)
int dm_flush_map_nopaths(const char * mapname, int deferred_remove);
int dm_cancel_deferred_remove(struct multipath *mpp);
int dm_flush_maps (int retries);
int dm_fail_path(const char * mapname, char * path);
int dm_reinstate_path(const char * mapname, char * path);
int dm_queue_if_no_path(struct multipath *mpp, int enable);
int dm_switchgroup(const char * mapname, int index);
int dm_enablegroup(const char * mapname, int index);
int dm_disablegroup(const char * mapname, int index);
int dm_get_maps (vector mp);
int dm_geteventnr (const char *name);
int dm_is_suspended(const char *name);
int dm_get_major_minor (const char *name, int *major, int *minor);
char * dm_mapname(int major, int minor);
int dm_get_wwid(const char *name, char *uuid, int uuid_len);
bool has_dm_info(const struct multipath *mpp);
int dm_rename (const char * old, char * new, char * delim, int skip_kpartx);
int dm_reassign(const char * mapname);
int dm_reassign_table(const char *name, char *old, char *new);
int dm_setgeometry(struct multipath *mpp);

#define VERSION_GE(v, minv) ( \
	(v[0] > minv[0]) || \
	((v[0] == minv[0]) && (v[1] > minv[1])) || \
	((v[0] == minv[0]) && (v[1] == minv[1]) && (v[2] >= minv[2])) \
)

#ifndef LIBDM_API_GET_ERRNO
#include <errno.h>
#define dm_task_get_errno(x) errno
#endif
enum {
	DM_LIBRARY_VERSION,
	DM_KERNEL_VERSION,
	DM_MPATH_TARGET_VERSION,
	MULTIPATH_VERSION
};
int libmp_get_version(int which, unsigned int version[3]);
struct dm_task;
int libmp_dm_task_run(struct dm_task *dmt);

#define dm_log_error(lvl, cmd, dmt)			      \
	condlog(lvl, "%s: libdm task=%d error: %s", __func__, \
		cmd, strerror(dm_task_get_errno(dmt)))	      \

#endif /* DEVMAPPER_H_INCLUDED */
