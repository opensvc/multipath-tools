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
 */
#define PRINT_PATH_LONG      "%w %i %d %D %p %t%T %s"
#define PRINT_PATH_INDENT    " \\_ %i %d %D %t%T"
#define PRINT_PATH_CHECKER   "%i %d %D %p %t%T %C"
#define PRINT_MAP_FAILBACK   "%w %d %F %Q %n"

#define MAX_LINE_LEN 80
#define PROGRESS_LEN 10

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


void get_path_layout (struct path_layout * pl, vector pathvec);
void get_map_layout (struct map_layout * pl, vector mpvec);
int snprint_path_header (char *, int, char *, struct path_layout *);
int snprint_map_header (char *, int, char *, struct map_layout *);
int snprint_path (char *, int, char *, struct path *, struct path_layout *);
int snprint_map (char *, int, char *,struct multipath *, struct map_layout *);
