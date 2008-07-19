#define TGT_MPATH	"multipath"
#define TGT_PART	"linear"

void dm_init(void);
int dm_prereq (void);
int dm_simplecmd (int, const char *);
int dm_addmap_create (const char *, const char *,
                      unsigned long long size, const char *uuid);
int dm_addmap_reload (const char *, const char *,
                      unsigned long long size, const char *uuid);
int dm_map_present (const char *);
int dm_get_map(char *, unsigned long long *, char *);
int dm_get_status(char *, char *);
int dm_type(const char *, char *);
int dm_flush_map (const char *);
int dm_flush_maps (void);
int dm_fail_path(char * mapname, char * path);
int dm_reinstate_path(char * mapname, char * path);
int dm_queue_if_no_path(char *mapname, int enable);
int dm_set_pg_timeout(char *mapname, int timeout_val);
int dm_switchgroup(char * mapname, int index);
int dm_enablegroup(char * mapname, int index);
int dm_disablegroup(char * mapname, int index);
int dm_get_maps (vector mp);
int dm_geteventnr (char *name);
int dm_get_minor (char *name);
char * dm_mapname(int major, int minor);
int dm_remove_partmaps (const char * mapname);
int dm_get_uuid(char *name, char *uuid);
int dm_get_info (char * mapname, struct dm_info ** dmi);
int dm_rename (char * old, char * new);
int dm_get_name(char * uuid, char * name);
