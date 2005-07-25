#define MAX_LINE_LEN 80

struct path_layout {
	int hbtl_len;
	int dev_len;
	int dev_t_len;
};

void get_path_layout (struct path_layout * pl, vector pathvec);
int print_path_id (char *, int, struct path *, struct path_layout *);
