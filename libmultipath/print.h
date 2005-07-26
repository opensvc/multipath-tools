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
int print_path_id (char *, int, struct path *, struct path_layout *);
int print_map_id (char *, int, struct multipath *, struct map_layout *);
