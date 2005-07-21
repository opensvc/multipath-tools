#define LIST	1
#define ADD	(1 << 1)
#define DEL	(1 << 2)
#define SWITCH	(1 << 3)
#define PATHS	(1 << 4)
#define MAPS	(1 << 5)
#define PATH	(1 << 6)
#define MAP	(1 << 7)
#define GROUP	(1 << 8)
#define DUMP	(1 << 9)
#define PATHVEC	(1 << 10)

#define MAX_REPLY_LEN 1000

struct key {
	char * str;
	char * param;
	int code;
	int has_param;
};

struct handler {
	int fingerprint;
	int (*fn)(void *, char **, int *, void *);
};

vector keys;
vector handlers;

int alloc_handlers (void);
int add_handler (int fp, int (*fn)(void *, char **, int *, void *));
int parse_cmd (char * cmd, char ** reply, int * len, void *);
int load_keys (void);
char * get_keyparam (vector v, int code);
void free_keys (vector vec);
void free_handlers (vector vec);
