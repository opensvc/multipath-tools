#define PRINT_PATH_LONG      "%w %i %d %D %p %t %T %s %o"
#define PRINT_PATH_INDENT    "%i %d %D %t %T %o"
#define PRINT_PATH_CHECKER   "%i %d %D %p %t %T %o %C"
#define PRINT_MAP_STATUS     "%n %F %Q %N %t %r"
#define PRINT_MAP_STATS      "%n %0 %1 %2 %3 %4"
#define PRINT_MAP_NAMES      "%n %d %w"
#define PRINT_MAP_PROPS      "size=%S features='%f' hwhandler='%h' wp=%r"
#define PRINT_PG_INDENT      "policy='%s' prio=%p status=%t"

#define PRINT_JSON_MULTIPLIER     5
#define PRINT_JSON_MAJOR_VERSION  0
#define PRINT_JSON_MINOR_VERSION  1
#define PRINT_JSON_START_VERSION  "   \"major_version\": %d,\n" \
                                  "   \"minor_version\": %d,\n"
#define PRINT_JSON_START_ELEM     "{\n"
#define PRINT_JSON_START_MAP      "   \"map\":"
#define PRINT_JSON_START_MAPS     "\"maps\": ["
#define PRINT_JSON_START_PATHS    "\"paths\": ["
#define PRINT_JSON_START_GROUPS   "\"path_groups\": ["
#define PRINT_JSON_END_ELEM       "},"
#define PRINT_JSON_END_LAST_ELEM  "}"
#define PRINT_JSON_END_LAST       "}\n"
#define PRINT_JSON_END_ARRAY      "]\n"
#define PRINT_JSON_INDENT    "   "
#define PRINT_JSON_MAP       "{\n" \
                             "      \"name\" : \"%n\",\n" \
                             "      \"uuid\" : \"%w\",\n" \
                             "      \"sysfs\" : \"%d\",\n" \
                             "      \"failback\" : \"%F\",\n" \
                             "      \"queueing\" : \"%Q\",\n" \
                             "      \"paths\" : %N,\n" \
                             "      \"write_prot\" : \"%r\",\n" \
                             "      \"dm_st\" : \"%t\",\n" \
                             "      \"features\" : \"%f\",\n" \
                             "      \"hwhandler\" : \"%h\",\n" \
                             "      \"action\" : \"%A\",\n" \
                             "      \"path_faults\" : %0,\n" \
                             "      \"vend\" : \"%v\",\n" \
                             "      \"prod\" : \"%p\",\n" \
                             "      \"rev\" : \"%e\",\n" \
                             "      \"switch_grp\" : %1,\n" \
                             "      \"map_loads\" : %2,\n" \
                             "      \"total_q_time\" : %3,\n" \
                             "      \"q_timeouts\" : %4,"

#define PRINT_JSON_GROUP     "{\n" \
                             "         \"selector\" : \"%s\",\n" \
                             "         \"pri\" : %p,\n" \
                             "         \"dm_st\" : \"%t\","

#define PRINT_JSON_GROUP_NUM "         \"group\" : %d,\n"

#define PRINT_JSON_PATH      "{\n" \
                             "            \"dev\" : \"%d\",\n"\
                             "            \"dev_t\" : \"%D\",\n" \
                             "            \"dm_st\" : \"%t\",\n" \
                             "            \"dev_st\" : \"%o\",\n" \
                             "            \"chk_st\" : \"%T\",\n" \
                             "            \"checker\" : \"%c\",\n" \
                             "            \"pri\" : %p,\n" \
                             "            \"host_wwnn\" : \"%N\",\n" \
                             "            \"target_wwnn\" : \"%n\",\n" \
                             "            \"host_wwpn\" : \"%R\",\n" \
                             "            \"target_wwpn\" : \"%r\",\n" \
                             "            \"host_adapter\" : \"%a\""

#define MAX_LINE_LEN  80
#define MAX_LINES     64
#define MAX_FIELD_LEN 64
#define PROGRESS_LEN  10

struct path_data {
	char wildcard;
	char * header;
	int width;
	int (*snprint)(char * buff, size_t len, struct path * pp);
};

struct multipath_data {
	char wildcard;
	char * header;
	int width;
	int (*snprint)(char * buff, size_t len, struct multipath * mpp);
};

struct pathgroup_data {
	char wildcard;
	char * header;
	int width;
	int (*snprint)(char * buff, size_t len, struct pathgroup * pgp);
};

void get_path_layout (vector pathvec, int header);
void get_multipath_layout (vector mpvec, int header);
int snprint_path_header (char *, int, char *);
int snprint_multipath_header (char *, int, char *);
int snprint_path (char *, int, char *, struct path *, int);
int snprint_multipath (char *, int, char *, struct multipath *, int);
int snprint_multipath_topology (char *, int, struct multipath * mpp,
				int verbosity);
int snprint_multipath_topology_json (char * buff, int len,
				struct vectors * vecs);
int snprint_multipath_map_json (char * buff, int len,
				struct multipath * mpp, int last);
int snprint_defaults (char *, int);
int snprint_blacklist (char *, int);
int snprint_blacklist_except (char *, int);
int snprint_blacklist_report (char *, int);
int snprint_wildcards (char *, int);
int snprint_status (char *, int, struct vectors *);
int snprint_devices (char *, int, struct vectors *);
int snprint_hwtable (char *, int, vector);
int snprint_mptable (char *, int, vector);
int snprint_overrides (char *, int, struct hwentry *);
int snprint_host_wwnn (char *, size_t, struct path *);
int snprint_host_wwpn (char *, size_t, struct path *);
int snprint_tgt_wwnn (char *, size_t, struct path *);
int snprint_tgt_wwpn (char *, size_t, struct path *);

void print_multipath_topology (struct multipath * mpp, int verbosity);
void print_path (struct path * pp, char * style);
void print_multipath (struct multipath * mpp, char * style);
void print_pathgroup (struct pathgroup * pgp, char * style);
void print_map (struct multipath * mpp, char * params);
void print_all_paths (vector pathvec, int banner);
void print_all_paths_custo (vector pathvec, int banner, char *fmt);
void print_hwtable (vector hwtable);

