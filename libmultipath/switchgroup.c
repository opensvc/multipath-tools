#include "vector.h"
#include "structs.h"
#include "switchgroup.h"
#include "../libcheckers/path_state.h"

extern void
select_path_group (struct multipath * mpp)
{
	int i, j;
	int highest = 0;
	struct pathgroup * pgp;
	struct path * pp;
	
	vector_foreach_slot (mpp->pg, pgp, i) {
		vector_foreach_slot (pgp->paths, pp, j) {
			if (pp->state != PATH_DOWN)
				pgp->priority += pp->priority;
		}
		if (pgp->priority > highest) {
			highest = pgp->priority;
			mpp->nextpg = i + 1;
		}
	}
}
