#define PRINT_PATH_LONG      "%w %i %d %D %p %t %T %s %o"
#define PRINT_PATH_INDENT    "%i %d %D %t %T %o"
#define PRINT_PATH_CHECKER   "%i %d %D %p %t %T %o %C"
#define PRINT_MAP_STATUS     "%n %F %Q %N %t %r"
#define PRINT_MAP_STATS      "%n %0 %1 %2 %3 %4"
#define PRINT_MAP_NAMES      "%n %d %w"
#define PRINT_MAP_PROPS      "size=%S features='%f' hwhandler='%h' wp=%r"
#define PRINT_PG_INDENT      "policy='%s' prio=%p status=%t"

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
int snprint_path (char *, int, char *, struct path *);
int snprint_multipath (char *, int, char *, struct multipath *);
int snprint_multipath_topology (char *, int, struct multipath * mpp,
				int verbosity);
int snprint_defaults (char *, int);
int snprint_blacklist (char *, int);
int snprint_blacklist_except (char *, int);
int snprint_blacklist_report (char *, int);
int snprint_wildcards (char *, int);
int snprint_status (char *, int, struct vectors *);
int snprint_devices (char *, int, struct vectors *);
int snprint_hwtable (char *, int, vector);
int snprint_mptable (char *, int, vector);

void print_multipath_topology (struct multipath * mpp, int verbosity);
void print_path (struct path * pp, char * style);
void print_multipath (struct multipath * mpp, char * style);
void print_pathgroup (struct pathgroup * pgp, char * style);
void print_map (struct multipath * mpp, char * params);
void print_all_paths (vector pathvec, int banner);
void print_all_paths_custo (vector pathvec, int banner, char *fmt);
void print_hwtable (vector hwtable);

