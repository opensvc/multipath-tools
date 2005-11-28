/*
 * path format magics :
 * 
 * %w : multipath uid
 * %i : scsi tuple
 * %d : device name
 * %D : device major:minor
 * %t : device mapper path status
 * %T : checker path status
 * %s : scsi strings
 * %p : priority
 * 
 * map format magics :
 * 
 * %w : multipath uid
 * %d : DM device name
 * %F : failback countdown
 * %C : checker countdown
 * %Q : queueing policy changer countdown (no_path_retry)
 * %n : number of active paths
 * %t : device mapper map status
 * %0 : stat_path_failures
 * %1 : stat_switchgroup
 * %2 : stat_map_loads
 * %3 : stat_total_queueing_time
 * %4 : stat_queueing_timeouts
 */
#define PRINT_PATH_LONG      "%w %i %d %D %p %t%T %s"
#define PRINT_PATH_INDENT    " \\_ %i %d %D %t%T"
#define PRINT_PATH_CHECKER   "%i %d %D %p %t%T %C"
#define PRINT_MAP_FAILBACK   "%w %d %F %Q %n %t"
#define PRINT_MAP_STATS      "%w %d %0 %1 %2 %3 %4"

#define MAX_LINE_LEN  80
#define MAX_FIELD_LEN 32
#define PROGRESS_LEN  10

struct path_layout {
	int uuid_len;
	int hbtl_len;
	int dev_len;
	int dev_t_len;
	int prio_len;
};

struct map_layout {
	int mapname_len;
	int mapdev_len;
	int failback_progress_len;
	int queueing_progress_len;
	int nr_active_len;
};

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

void get_path_layout (vector pathvec);
void get_multipath_layout (vector mpvec);
int snprint_path_header (char *, int, char *);
int snprint_multipath_header (char *, int, char *);
int snprint_path (char *, int, char *, struct path *);
int snprint_multipath (char *, int, char *,struct multipath *);

void print_mp (struct multipath * mpp, int verbosity);
void print_path (struct path * pp, char * style);
void print_multipath (struct multipath * mpp, char * style);
void print_map (struct multipath * mpp);
void print_all_paths (vector pathvec, int banner);
