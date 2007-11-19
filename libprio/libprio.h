#ifndef _LIBPRIO_H
#define _LIBPRIO_H

/*
 * knowing about path struct gives flexibility to prioritizers
 */
#include "../libcheckers/checkers.h"
#include "../libmultipath/vector.h"
#include "../libmultipath/structs.h"

#include "const.h"
#include "random.h"
#include "hp_sw.h"
#include "alua.h"
#include "emc.h"
#include "netapp.h"
#include "hds.h"
#include "rdac.h"

#define DEFAULT_PRIO PRIO_CONST

/*
 * Value used to mark the fact prio was not defined
 */
#define PRIO_UNDEF -1

/*
 * strings lengths
 */
#define PRIO_NAME_LEN 16
#define PRIO_DEV_LEN 256

struct prio {
	char name[PRIO_NAME_LEN];
	int (*getprio)(struct path *);
};

struct prio * prio_lookup (char *);
int prio_getprio (struct prio *, struct path *);
char * prio_name (struct prio *);
struct prio * prio_default (void);

#endif /* _LIBPRIO_H */
