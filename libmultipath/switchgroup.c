/*
 * Copyright (c) 2005 Christophe Varoqui
 * Copyright (c) 2005 Edward Goggin, EMC
 */
#include "vector.h"
#include "structs.h"
#include "switchgroup.h"
#include "../libcheckers/path_state.h"

extern int
select_path_group (struct multipath * mpp)
{
	int i, j;
	int highest = 0;
	int bestpg = 1;
	struct pathgroup * pgp;
	struct path * pp;
	int priority;

	if (!mpp->pg)
		return 1;
	
	vector_foreach_slot (mpp->pg, pgp, i) {
		if (!pgp->paths)
			continue;

		priority = 0;

		vector_foreach_slot (pgp->paths, pp, j) {
			if (pp->state != PATH_DOWN)
				priority += pp->priority;
		}
		pgp->priority = priority;

		if (pgp->priority > highest) {
			highest = pgp->priority;
			bestpg = i + 1;
		}
	}
	return bestpg;
}
