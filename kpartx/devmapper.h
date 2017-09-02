#ifndef _KPARTX_DEVMAPPER_H
#define _KPARTX_DEVMAPPER_H

#ifdef DM_SUBSYSTEM_UDEV_FLAG0
#define MPATH_UDEV_RELOAD_FLAG DM_SUBSYSTEM_UDEV_FLAG0
#else
#define MPATH_UDEV_RELOAD_FLAG 0
#endif

extern int udev_sync;

int dm_prereq (char *, int, int, int);
int dm_simplecmd (int, const char *, int, uint16_t);
int dm_addmap (int, const char *, const char *, const char *, uint64_t,
	       int, const char *, int, mode_t, uid_t, gid_t);
char * dm_mapname(int major, int minor);
dev_t dm_get_first_dep(char *devname);
char * dm_mapuuid(const char *mapname);
int dm_devn (const char * mapname, int *major, int *minor);
int dm_remove_partmaps (char * mapname, char *uuid, dev_t devt, int verbose);
int dm_find_part(const char *parent, const char *delim, int part,
		 const char *parent_uuid,
		 char *name, size_t namesiz, char **part_uuid, int verbose);

/*
 * UUID format for partitions created on non-DM devices
 * ${UUID_PREFIX}devnode_${MAJOR}:${MINOR}_${NONDM_UUID_SUFFIX}"
 * where ${UUID_PREFIX} is "part${PARTNO}-" (see devmapper.c).
 *
 * The suffix should be sufficiently unique to avoid incidental conflicts;
 * the value below is a base64-encoded random number.
 * The UUID format shouldn't be changed between kpartx releases.
 */
#define NONDM_UUID_PREFIX "devnode"
#define NONDM_UUID_SUFFIX "Wh5pYvM"
char *nondm_create_uuid(dev_t devt);
int nondm_parse_uuid(const char *uuid, int *major, int *minor);
#endif /* _KPARTX_DEVMAPPER_H */
