int dm_prereq (char *, int, int, int);
int dm_simplecmd (int, const char *);
int dm_addmap (int, const char *, const char *, const char *, unsigned long);
int dm_map_present (char *);
const char * dm_mapname(int major, int minor);
dev_t dm_get_first_dep(char *devname);
