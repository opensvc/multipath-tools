/*
 * Copyright (c) 2005 Christophe Varoqui
 * Copyright (c) 2005 Edward Goggin, EMC
 */
#include "checkers.h"
#include "vector.h"
#include "structs.h"
#include "switchgroup.h"

extern void
path_group_prio_update (struct pathgroup * pgp)
{
	int i;
	int priority = 0;
	struct path * pp;

	if (!pgp->paths) {
		pgp->priority = 0;
		return;
	}
	vector_foreach_slot (pgp->paths, pp, i) {
		if (pp->state != PATH_DOWN)
			priority += pp->priority;
	}
	pgp->priority = priority;
}

extern int
select_path_group (struct multipath * mpp)
{
	int i;
	int highest = 0;
	int bestpg = 1;
	struct pathgroup * pgp;

	if (!mpp->pg)
		return 1;

	vector_foreach_slot (mpp->pg, pgp, i) {
		if (!pgp->paths)
			continue;

		path_group_prio_update(pgp);
		if (pgp->priority > highest) {
			highest = pgp->priority;
			bestpg = i + 1;
		}
	}
	return bestpg;
}
