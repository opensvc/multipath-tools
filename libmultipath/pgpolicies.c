/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "checkers.h"
#include "util.h"
#include "memory.h"
#include "vector.h"
#include "structs.h"
#include "pgpolicies.h"
#include "switchgroup.h"

extern int
get_pgpolicy_id (char * str)
{
	if (0 == strncmp(str, "failover", 8))
		return FAILOVER;
	if (0 == strncmp(str, "multibus", 8))
		return MULTIBUS;
	if (0 == strncmp(str, "group_by_serial", 15))
		return GROUP_BY_SERIAL;
	if (0 == strncmp(str, "group_by_prio", 13))
		return GROUP_BY_PRIO;
	if (0 == strncmp(str, "group_by_node_name", 18))
		return GROUP_BY_NODE_NAME;

	return -1;
}

extern int
get_pgpolicy_name (char * buff, int len, int id)
{
	char * s;

	switch (id) {
	case FAILOVER:
		s = "failover";
		break;
	case MULTIBUS:
		s = "multibus";
		break;
	case GROUP_BY_SERIAL:
		s = "group_by_serial";
		break;
	case GROUP_BY_PRIO:
		s = "group_by_prio";
		break;
	case GROUP_BY_NODE_NAME:
		s = "group_by_node_name";
		break;
	default:
		s = "undefined";
		break;
	}
	return snprintf(buff, POLICY_NAME_SIZE, "%s", s);
}


void
sort_pathgroups (struct multipath *mp) {
	int i, j;
	struct pathgroup * pgp1, * pgp2;

	if (!mp->pg)
		return;

	vector_foreach_slot(mp->pg, pgp1, i) {
		path_group_prio_update(pgp1);
		for (j = i - 1; j >= 0; j--) {
			pgp2 = VECTOR_SLOT(mp->pg, j);
			if (!pgp2)
				continue;
			if (pgp2->priority > pgp1->priority ||
			    (pgp2->priority == pgp1->priority &&
			     pgp2->enabled_paths >= pgp1->enabled_paths)) {
				vector_move_up(mp->pg, i, j + 1);
				break;
			}
		}
		if (j < 0 && i != 0)
		vector_move_up(mp->pg, i, 0);
	}
}


/*
 * One path group per unique tgt_node_name present in the path vector
 */
extern int
group_by_node_name (struct multipath * mp) {
	int i, j;
	int * bitmap;
	struct path * pp;
	struct pathgroup * pgp;
	struct path * pp2;

	if (!mp->pg)
		mp->pg = vector_alloc();

	if (!mp->pg)
		return 1;

	/* init the bitmap */
	bitmap = (int *)MALLOC(VECTOR_SIZE(mp->paths) * sizeof (int));

	if (!bitmap)
		goto out;

	for (i = 0; i < VECTOR_SIZE(mp->paths); i++) {

		if (bitmap[i])
			continue;

		pp = VECTOR_SLOT(mp->paths, i);

		/* here, we really got a new pg */
		pgp = alloc_pathgroup();

		if (!pgp)
			goto out1;

		if (store_pathgroup(mp->pg, pgp))
			goto out1;

		/* feed the first path */
		if (store_path(pgp->paths, pp))
			goto out1;

		bitmap[i] = 1;

		for (j = i + 1; j < VECTOR_SIZE(mp->paths); j++) {

			if (bitmap[j])
				continue;

			pp2 = VECTOR_SLOT(mp->paths, j);

			if (!strncmp(pp->tgt_node_name, pp2->tgt_node_name,
					NODE_NAME_SIZE)) {
				if (store_path(pgp->paths, pp2))
					goto out1;

				bitmap[j] = 1;
			}
		}
	}
	FREE(bitmap);
	sort_pathgroups(mp);
	free_pathvec(mp->paths, KEEP_PATHS);
	mp->paths = NULL;
	return 0;
out1:
	FREE(bitmap);
out:
	free_pgvec(mp->pg, KEEP_PATHS);
	mp->pg = NULL;
	return 1;
}

/*
 * One path group per unique serial number present in the path vector
 */
extern int
group_by_serial (struct multipath * mp) {
	int i, j;
	int * bitmap;
	struct path * pp;
	struct pathgroup * pgp;
	struct path * pp2;

	if (!mp->pg)
		mp->pg = vector_alloc();

	if (!mp->pg)
		return 1;

	/* init the bitmap */
	bitmap = (int *)MALLOC(VECTOR_SIZE(mp->paths) * sizeof (int));

	if (!bitmap)
		goto out;

	for (i = 0; i < VECTOR_SIZE(mp->paths); i++) {

		if (bitmap[i])
			continue;

		pp = VECTOR_SLOT(mp->paths, i);

		/* here, we really got a new pg */
		pgp = alloc_pathgroup();

		if (!pgp)
			goto out1;

		if (store_pathgroup(mp->pg, pgp))
			goto out1;

		/* feed the first path */
		if (store_path(pgp->paths, pp))
			goto out1;

		bitmap[i] = 1;

		for (j = i + 1; j < VECTOR_SIZE(mp->paths); j++) {

			if (bitmap[j])
				continue;

			pp2 = VECTOR_SLOT(mp->paths, j);

			if (0 == strcmp(pp->serial, pp2->serial)) {
				if (store_path(pgp->paths, pp2))
					goto out1;

				bitmap[j] = 1;
			}
		}
	}
	FREE(bitmap);
	sort_pathgroups(mp);
	free_pathvec(mp->paths, KEEP_PATHS);
	mp->paths = NULL;
	return 0;
out1:
	FREE(bitmap);
out:
	free_pgvec(mp->pg, KEEP_PATHS);
	mp->pg = NULL;
	return 1;
}

extern int
one_path_per_group (struct multipath * mp)
{
	int i;
	struct path * pp;
	struct pathgroup * pgp;

	if (!mp->pg)
		mp->pg = vector_alloc();

	if (!mp->pg)
		return 1;

	for (i = 0; i < VECTOR_SIZE(mp->paths); i++) {
		pp = VECTOR_SLOT(mp->paths, i);
		pgp = alloc_pathgroup();

		if (!pgp)
			goto out;

		if (store_pathgroup(mp->pg, pgp))
			goto out;

		if (store_path(pgp->paths, pp))
			goto out;
	}
	sort_pathgroups(mp);
	free_pathvec(mp->paths, KEEP_PATHS);
	mp->paths = NULL;
	return 0;
out:
	free_pgvec(mp->pg, KEEP_PATHS);
	mp->pg = NULL;
	return 1;
}

extern int
one_group (struct multipath * mp)	/* aka multibus */
{
	struct pathgroup * pgp;

	if (VECTOR_SIZE(mp->paths) < 0)
		return 0;

	if (!mp->pg)
		mp->pg = vector_alloc();

	if (!mp->pg)
		return 1;

	if (VECTOR_SIZE(mp->paths) > 0) {
		pgp = alloc_pathgroup();

		if (!pgp)
			goto out;

		vector_free(pgp->paths);
		pgp->paths = mp->paths;
		mp->paths = NULL;

		if (store_pathgroup(mp->pg, pgp))
			goto out;
	}

	return 0;
out:
	free_pgvec(mp->pg, KEEP_PATHS);
	mp->pg = NULL;
	return 1;
}

extern int
group_by_prio (struct multipath * mp)
{
	int i;
	unsigned int prio;
	struct path * pp;
	struct pathgroup * pgp;

	if (!mp->pg)
		mp->pg = vector_alloc();

	if (!mp->pg)
		return 1;

	while (VECTOR_SIZE(mp->paths) > 0) {
		pp = VECTOR_SLOT(mp->paths, 0);
		prio = pp->priority;

		/*
		 * Find the position to insert the new path group. All groups
		 * are ordered by the priority value (higher value first).
		 */
		vector_foreach_slot(mp->pg, pgp, i) {
			pp  = VECTOR_SLOT(pgp->paths, 0);

			if (prio > pp->priority)
				break;
		}

		/*
		 * Initialize the new path group.
		 */
		pgp = alloc_pathgroup();

		if (!pgp)
			goto out;

		if (store_path(pgp->paths, VECTOR_SLOT(mp->paths, 0)))
				goto out;

		vector_del_slot(mp->paths, 0);

		/*
		 * Store the new path group into the vector.
		 */
		if (i < VECTOR_SIZE(mp->pg)) {
			if (!vector_insert_slot(mp->pg, i, pgp))
				goto out;
		} else {
			if (store_pathgroup(mp->pg, pgp))
				goto out;
		}

		/*
		 * add the other paths with the same prio
		 */
		vector_foreach_slot(mp->paths, pp, i) {
			if (pp->priority == prio) {
				if (store_path(pgp->paths, pp))
					goto out;

				vector_del_slot(mp->paths, i);
				i--;
			}
		}
	}
	free_pathvec(mp->paths, KEEP_PATHS);
	mp->paths = NULL;
	return 0;
out:
	free_pgvec(mp->pg, KEEP_PATHS);
	mp->pg = NULL;
	return 1;

}
