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
 * %c : claimed
 * 
 * map format magics :
 * 
 * %w : multipath uid
 * %d : DM device name
 * %F : failback countdown
 * %C : checker countdown
 */
#define PRINT_PATH_LONG      "%w %i %d %D %t%T%c %s\n"
#define PRINT_PATH_INDENT    " \\_ %i %d %D %t%T%c\n"
#define PRINT_PATH_CHECKER   "%i %d %D %t%T %C\n"
#define PRINT_MAP_FAILBACK   "%w %d %F\n"

#define MAX_LINE_LEN 80

struct path_layout {
	int hbtl_len;
	int dev_len;
	int dev_t_len;
};

struct map_layout {
	int mapname_len;
	int mapdev_len;
};


void get_path_layout (struct path_layout * pl, vector pathvec);
void get_map_layout (struct map_layout * pl, vector mpvec);
int snprint_path (char *, int, char *, struct path *, struct path_layout *);
int snprint_map (char *, int, char *,struct multipath *, struct map_layout *);
