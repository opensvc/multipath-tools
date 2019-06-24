#ifndef _DEVMAPPER_H
#define _DEVMAPPER_H

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

void dm_init(int verbosity);
void libmp_dm_init(void);
void libmp_udev_set_sync_support(int on);
struct dm_task *libmp_dm_task_create(int task);
int dm_drv_version (unsigned int * version);
int dm_tgt_version (unsigned int * version, char * str);
int dm_simplecmd_flush (int, const char *, uint16_t);
int dm_simplecmd_noflush (int, const char *, uint16_t);
int dm_addmap_create (struct multipath *mpp, char *params);
int dm_addmap_reload (struct multipath *mpp, char *params, int flush);
int dm_map_present (const char *);
int dm_get_map(const char *, unsigned long long *, char *);
int dm_get_status(const char *, char *);
int dm_type(const char *, char *);
int dm_is_mpath(const char *);
int _dm_flush_map (const char *, int, int, int, int);
int dm_flush_map_nopaths(const char * mapname, int deferred_remove);
#define dm_flush_map(mapname) _dm_flush_map(mapname, 1, 0, 0, 0)
#define dm_flush_map_nosync(mapname) _dm_flush_map(mapname, 0, 0, 0, 0)
#define dm_suspend_and_flush_map(mapname, retries) \
	_dm_flush_map(mapname, 1, 0, 1, retries)
int dm_cancel_deferred_remove(struct multipath *mpp);
int dm_flush_maps (int retries);
int dm_fail_path(const char * mapname, char * path);
int dm_reinstate_path(const char * mapname, char * path);
int dm_queue_if_no_path(const char *mapname, int enable);
int dm_switchgroup(const char * mapname, int index);
int dm_enablegroup(const char * mapname, int index);
int dm_disablegroup(const char * mapname, int index);
int dm_get_maps (vector mp);
int dm_geteventnr (const char *name);
int dm_is_suspended(const char *name);
int dm_get_major_minor (const char *name, int *major, int *minor);
char * dm_mapname(int major, int minor);
int dm_remove_partmaps (const char * mapname, int need_sync,
			int deferred_remove);
int dm_get_uuid(const char *name, char *uuid, int uuid_len);
int dm_get_info (const char * mapname, struct dm_info ** dmi);
int dm_rename (const char * old, char * new, char * delim, int skip_kpartx);
int dm_reassign(const char * mapname);
int dm_reassign_table(const char *name, char *old, char *new);
int dm_setgeometry(struct multipath *mpp);
struct multipath *dm_get_multipath(const char *name);

#define VERSION_GE(v, minv) ( \
	(v[0] > minv[0]) || \
	((v[0] == minv[0]) && (v[1] > minv[1])) || \
	((v[0] == minv[0]) && (v[1] == minv[1]) && (v[2] >= minv[2])) \
)

#endif /* _DEVMAPPER_H */
