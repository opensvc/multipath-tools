#ifndef _PRIO_H
#define _PRIO_H

/*
 * knowing about path struct gives flexibility to prioritizers
 */
#include "checkers.h"
#include "vector.h"

/* forward declaration to avoid circular dependency */
struct path;

#include "list.h"
#include "memory.h"
#include "defaults.h"

/*
 * Known prioritizers for use in hwtable.c
 */
#define PRIO_ALUA		"alua"
#define PRIO_CONST		"const"
#define PRIO_DATACORE		"datacore"
#define PRIO_EMC		"emc"
#define PRIO_HDS		"hds"
#define PRIO_HP_SW		"hp_sw"
#define PRIO_IET		"iet"
#define PRIO_ONTAP		"ontap"
#define PRIO_RANDOM		"random"
#define PRIO_RDAC		"rdac"
#define PRIO_WEIGHTED_PATH	"weightedpath"
#define PRIO_SYSFS		"sysfs"
#define PRIO_PATH_LATENCY	"path_latency"
#define PRIO_ANA		"ana"

/*
 * Value used to mark the fact prio was not defined
 */
#define PRIO_UNDEF -1

/*
 * strings lengths
 */
#define LIB_PRIO_NAMELEN 255
#define PRIO_NAME_LEN 16
#define PRIO_ARGS_LEN 255

struct prio {
	void *handle;
	int refcount;
	struct list_head node;
	char name[PRIO_NAME_LEN];
	char args[PRIO_ARGS_LEN];
	int (*getprio)(struct path *, char *, unsigned int);
};

unsigned int get_prio_timeout(unsigned int checker_timeout,
			      unsigned int default_timeout);
int init_prio (char *);
void cleanup_prio (void);
struct prio * add_prio (char *, char *);
int prio_getprio (struct prio *, struct path *, unsigned int);
void prio_get (char *, struct prio *, char *, char *);
void prio_put (struct prio *);
int prio_selected (const struct prio *);
const char * prio_name (const struct prio *);
const char * prio_args (const struct prio *);
int prio_set_args (struct prio *, const char *);

/* The only function exported by prioritizer dynamic libraries (.so) */
int getprio(struct path *, char *, unsigned int);

#endif /* _PRIO_H */
