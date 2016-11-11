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

void dm_init(int verbosity);
int dm_prereq (void);
int dm_drv_version (unsigned int * version, char * str);
int dm_simplecmd_flush (int, const char *, uint16_t);
int dm_simplecmd_noflush (int, const char *, uint16_t);
int dm_addmap_create (struct multipath *mpp, char *params);
int dm_addmap_reload (struct multipath *mpp, char *params, int flush);
int dm_map_present (const char *);
int dm_get_map(const char *, unsigned long long *, char *);
int dm_get_status(char *, char *);
int dm_type(const char *, char *);
int dm_is_mpath(const char *);
int _dm_flush_map (const char *, int, int);
int dm_flush_map_nopaths(const char * mapname, int deferred_remove);
#define dm_flush_map(mapname) _dm_flush_map(mapname, 1, 0)
#define dm_flush_map_nosync(mapname) _dm_flush_map(mapname, 0, 0)
int dm_cancel_deferred_remove(struct multipath *mpp);
int dm_suspend_and_flush_map(const char * mapname, int retries);
int dm_flush_maps (int retries);
int dm_fail_path(char * mapname, char * path);
int dm_reinstate_path(char * mapname, char * path);
int dm_queue_if_no_path(char *mapname, int enable);
int dm_switchgroup(char * mapname, int index);
int dm_enablegroup(char * mapname, int index);
int dm_disablegroup(char * mapname, int index);
int dm_get_maps (vector mp);
int dm_geteventnr (char *name);
int dm_get_major (char *name);
int dm_get_minor (char *name);
char * dm_mapname(int major, int minor);
int dm_remove_partmaps (const char * mapname, int need_sync,
			int deferred_remove);
int dm_get_uuid(char *name, char *uuid);
int dm_get_info (char * mapname, struct dm_info ** dmi);
int dm_rename (const char * old, char * new, char * delim, int skip_kpartx);
int dm_reassign(const char * mapname);
int dm_reassign_table(const char *name, char *old, char *new);
int dm_setgeometry(struct multipath *mpp);

#define VERSION_GE(v, minv) ( \
	(v[0] > minv[0]) || \
	((v[0] == minv[0]) && (v[1] > minv[1])) || \
	((v[0] == minv[0]) && (v[1] == minv[1]) && (v[2] >= minv[2])) \
)

#endif /* _DEVMAPPER_H */
