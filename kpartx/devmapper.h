int dm_prereq (char *, int, int, int);
int dm_simplecmd (int, const char *, int);
int dm_addmap (int, const char *, const char *, const char *, uint64_t,
	       const char *, int);
int dm_map_present (char *);
char * dm_mapname(int major, int minor);
dev_t dm_get_first_dep(char *devname);
char * dm_mapuuid(int major, int minor);
int dm_devn (char * mapname, int *major, int *minor);
