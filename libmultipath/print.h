#define PRINT_PATH_LONG      "%w %i %d %D %p %t%T %s"
#define PRINT_PATH_INDENT    " \\_ %i %d %D %t%T"
#define PRINT_PATH_CHECKER   "%i %d %D %p %t%T %C"
#define PRINT_MAP_STATUS     "%n %F %Q %N %t"
#define PRINT_MAP_STATS      "%n %0 %1 %2 %3 %4"
#define PRINT_MAP_NAMES      "%n %d %w"
#define PRINT_PG_INDENT      "\\_ %s [prio=%p]%t"

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

void get_path_layout (vector pathvec);
void get_multipath_layout (vector mpvec);
int snprint_path_header (char *, int, char *);
int snprint_multipath_header (char *, int, char *);
int snprint_path (char *, int, char *, struct path *);
int snprint_multipath (char *, int, char *, struct multipath *);
int snprint_multipath_topology (char *, int, struct multipath * mpp,
				int verbosity);

void print_multipath_topology (struct multipath * mpp, int verbosity);
void print_path (struct path * pp, char * style);
void print_multipath (struct multipath * mpp, char * style);
void print_pathgroup (struct pathgroup * pgp, char * style);
void print_map (struct multipath * mpp);
void print_all_paths (vector pathvec, int banner);
