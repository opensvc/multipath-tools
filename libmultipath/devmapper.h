#ifndef _DEVMAPPER_H
#define _DEVMAPPER_H

#include "structs.h"

#define TGT_MPATH	"multipath"
#define TGT_PART	"linear"

void dm_init(void);
int dm_prereq (void);
int dm_drv_version (unsigned int * version, char * str);
int dm_simplecmd_flush (int, const char *, int);
int dm_simplecmd_noflush (int, const char *);
int dm_addmap_create (struct multipath *mpp, char *params);
int dm_addmap_reload (struct multipath *mpp, char *params);
int dm_map_present (const char *);
int dm_get_map(const char *, unsigned long long *, char *);
int dm_get_status(char *, char *);
int dm_type(const char *, char *);
int _dm_flush_map (const char *, int);
#define dm_flush_map(mapname) _dm_flush_map(mapname, 1)
#define dm_flush_map_nosync(mapname) _dm_flush_map(mapname, 0)
int dm_suspend_and_flush_map(const char * mapname);
int dm_flush_maps (void);
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
int dm_remove_partmaps (const char * mapname, int need_sync);
int dm_get_uuid(char *name, char *uuid);
int dm_get_info (char * mapname, struct dm_info ** dmi);
int dm_rename (char * old, char * new);
int dm_reassign(const char * mapname);
int dm_reassign_table(const char *name, char *old, char *new);
int dm_setgeometry(struct multipath *mpp);
void udev_wait(unsigned int c);
void udev_set_sync_support(int c);

#define VERSION_GE(v, minv) ( \
 (v[0] > minv[0]) || \
 ((v[0] == minv[0]) && (v[1] > minv[1])) || \
 ((v[0] == minv[0]) && (v[1] == minv[1]) && (v[2] >= minv[2])) \
)

#endif /* _DEVMAPPER_H */
