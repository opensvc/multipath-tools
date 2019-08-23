/*
 * Copyright (c) 2005 Christophe Varoqui
 * Copyright (c) 2005 Edward Goggin, EMC
 */
#include "checkers.h"
#include "vector.h"
#include "structs.h"
#include "switchgroup.h"

void path_group_prio_update(struct pathgroup *pgp)
{
	int i;
	int priority = 0;
	int marginal = 0;
	struct path * pp;

	pgp->enabled_paths = 0;
	if (!pgp->paths) {
		pgp->priority = 0;
		return;
	}
	vector_foreach_slot (pgp->paths, pp, i) {
		if (pp->marginal)
			marginal++;
		if (pp->state == PATH_UP ||
		    pp->state == PATH_GHOST) {
			priority += pp->priority;
			pgp->enabled_paths++;
		}
	}
	if (pgp->enabled_paths)
		pgp->priority = priority / pgp->enabled_paths;
	else
		pgp->priority = 0;
	if (marginal && marginal == i)
		pgp->marginal = 1;
}

int select_path_group(struct multipath *mpp)
{
	int i;
	int normal_pgp = 0;
	int max_priority = 0;
	int bestpg = 1;
	int max_enabled_paths = 1;
	struct pathgroup * pgp;

	if (!mpp->pg)
		return 1;

	vector_foreach_slot (mpp->pg, pgp, i) {
		if (!pgp->paths)
			continue;

		path_group_prio_update(pgp);
		if (pgp->marginal && normal_pgp)
			continue;
		if (pgp->enabled_paths) {
			if (!pgp->marginal && !normal_pgp) {
				normal_pgp = 1;
				max_priority = pgp->priority;
				max_enabled_paths = pgp->enabled_paths;
				bestpg = i + 1;
			} else if (pgp->priority > max_priority) {
				max_priority = pgp->priority;
				max_enabled_paths = pgp->enabled_paths;
				bestpg = i + 1;
			} else if (pgp->priority == max_priority) {
				if (pgp->enabled_paths > max_enabled_paths) {
					max_enabled_paths = pgp->enabled_paths;
					bestpg = i + 1;
				}
			}
		}
	}
	return bestpg;
}
