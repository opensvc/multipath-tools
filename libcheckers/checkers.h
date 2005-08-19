#ifndef _CHECKERS_H
#define _CHECKERS_H

#define CHECKER_NAME_SIZE 16
#define DEVNODE_SIZE 256
#define MAX_CHECKER_MSG_SIZE 256

enum checkers {
	CHECKER_UNDEF,
	TUR,
	READSECTOR0,
	EMC_CLARIION,
	HP_SW
};

#define MSG(a) if (msg != NULL) \
		snprintf(msg, MAX_CHECKER_MSG_SIZE, "%s", a);

int get_checker_id (char *);
void *get_checker_addr (int);
int get_checker_name (char *, int);

int emc_clariion (int fd, char * msg, void ** ctxt);
int readsector0 (int fd, char * msg, void ** ctxt);
int tur (int fd, char * msg, void ** ctxt);
int hp_sw (int fd, char * msg, void ** ctxt);

#endif /* _CHECKERS_H */
