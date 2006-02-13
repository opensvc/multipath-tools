#ifndef _CHECKERS_H
#define _CHECKERS_H

/*
 * path states
 */
#define PATH_WILD	-1
#define PATH_UNCHECKED	0
#define PATH_DOWN	1
#define PATH_UP		2
#define PATH_SHAKY	3
#define PATH_GHOST	4

#define DIRECTIO     "directio"
#define TUR          "tur"
#define HP_SW        "hp_sw"
#define EMC_CLARIION "emc_clariion"
#define READSECTOR0  "readsector0"

#define DEFAULT_CHECKER READSECTOR0

/*
 * strings lengths
 */
#define CHECKER_NAME_LEN 16
#define CHECKER_MSG_LEN 256
#define CHECKER_DEV_LEN 256

struct checker {
	int fd;
	char name[CHECKER_NAME_LEN];
	char message[CHECKER_MSG_LEN];       /* comm with callers */
	void * context;                      /* store for persistent data */
	int (*check)(struct checker *);
	int (*init)(struct checker *);       /* to allocate the context */
	void (*free)(struct checker *);      /* to free the context */
};

#define MSG(c, a) snprintf((c)->message, CHECKER_MSG_LEN, a);

int checker_init (struct checker *);
void checker_put (struct checker *);
void checker_reset (struct checker * c);
void checker_set_fd (struct checker *, int);
struct checker * checker_lookup (char *);
int checker_check (struct checker *);
int checker_selected (struct checker *);
char * checker_name (struct checker *);
char * checker_message (struct checker *);
struct checker * checker_default (void);
void checker_get (struct checker *, struct checker *);

#endif /* _CHECKERS_H */
