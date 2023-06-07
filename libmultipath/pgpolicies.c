/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "checkers.h"
#include "util.h"
#include "vector.h"
#include "structs.h"
#include "pgpolicies.h"
#include "switchgroup.h"

int get_pgpolicy_id(char * str)
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
	if (0 == strncmp(str, "group_by_tpg", 12))
		return GROUP_BY_TPG;

	return IOPOLICY_UNDEF;
}

const char *get_pgpolicy_name(int id)
{
	switch (id) {
	case FAILOVER:
		return "failover";
	case MULTIBUS:
		return "multibus";
	case GROUP_BY_SERIAL:
		return "group_by_serial";
	case GROUP_BY_PRIO:
		return "group_by_prio";
	case GROUP_BY_NODE_NAME:
		return "group_by_node_name";
	case GROUP_BY_TPG:
		return "group_by_tpg";
	}
	return "undefined"; /* IOPOLICY_UNDEF */
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
			if (pgp2->marginal < pgp1->marginal ||
			    (pgp2->marginal == pgp1->marginal &&
			     (pgp2->priority > pgp1->priority ||
			      (pgp2->priority == pgp1->priority &&
			       pgp2->enabled_paths >= pgp1->enabled_paths)))) {
				vector_move_up(mp->pg, i, j + 1);
				break;
			}
		}
		if (j < 0 && i != 0)
		vector_move_up(mp->pg, i, 0);
	}
}

static int
split_marginal_paths(vector paths, vector *normal_p, vector *marginal_p)
{
	int i;
	int has_marginal = 0;
	int has_normal = 0;
	struct path *pp;
	vector normal = NULL;
	vector marginal = NULL;

	*normal_p = *marginal_p = NULL;
	vector_foreach_slot(paths, pp, i) {
		if (pp->marginal)
			has_marginal = 1;
		else
			has_normal = 1;
	}

	if (!has_marginal || !has_normal)
		return -1;

	normal = vector_alloc();
	marginal = vector_alloc();
	if (!normal || !marginal)
		goto fail;

	vector_foreach_slot(paths, pp, i) {
		if (pp->marginal) {
			if (store_path(marginal, pp))
				goto fail;
		}
		else {
			if (store_path(normal, pp))
				goto fail;
		}
	}
	*normal_p = normal;
	*marginal_p = marginal;
	return 0;
fail:
	vector_free(normal);
	vector_free(marginal);
	return -1;
}

int group_paths(struct multipath *mp, int marginal_pathgroups)
{
	vector normal, marginal;

	if (!mp->pg)
		mp->pg = vector_alloc();
	if (!mp->pg)
		return 1;

	if (VECTOR_SIZE(mp->paths) == 0)
		goto out;
	if (!mp->pgpolicyfn)
		goto fail;

	if (!marginal_pathgroups ||
	    split_marginal_paths(mp->paths, &normal, &marginal) != 0) {
		if (mp->pgpolicyfn(mp, mp->paths) != 0)
			goto fail;
	} else {
		if (mp->pgpolicyfn(mp, normal) != 0)
			goto fail_marginal;
		if (mp->pgpolicyfn(mp, marginal) != 0)
			goto fail_marginal;
		vector_free(normal);
		vector_free(marginal);
	}
	sort_pathgroups(mp);
out:
	vector_free(mp->paths);
	mp->paths = NULL;
	return 0;
fail_marginal:
	vector_free(normal);
	vector_free(marginal);
fail:
	vector_free(mp->pg);
	mp->pg = NULL;
	return 1;
}

typedef bool (path_match_fn)(struct path *pp1, struct path *pp2);

bool
node_names_match(struct path *pp1, struct path *pp2)
{
	return (strncmp(pp1->tgt_node_name, pp2->tgt_node_name,
			NODE_NAME_SIZE) == 0);
}

bool
serials_match(struct path *pp1, struct path *pp2)
{
	return (strncmp(pp1->serial, pp2->serial, SERIAL_SIZE) == 0);
}

bool
prios_match(struct path *pp1, struct path *pp2)
{
	return (pp1->priority == pp2->priority);
}

bool
tpg_match(struct path *pp1, struct path *pp2)
{
	return (pp1->tpg_id == pp2->tpg_id);
}

int group_by_match(struct multipath * mp, vector paths,
		   bool (*path_match_fn)(struct path *, struct path *))
{
	int i, j;
	struct bitfield *bitmap;
	struct path * pp;
	struct pathgroup * pgp;
	struct path * pp2;

	/* init the bitmap */
	bitmap = alloc_bitfield(VECTOR_SIZE(paths));

	if (!bitmap)
		goto out;

	for (i = 0; i < VECTOR_SIZE(paths); i++) {

		if (is_bit_set_in_bitfield(i, bitmap))
			continue;

		pp = VECTOR_SLOT(paths, i);

		/* here, we really got a new pg */
		pgp = alloc_pathgroup();

		if (!pgp)
			goto out1;

		if (add_pathgroup(mp, pgp))
			goto out2;

		/* feed the first path */
		if (store_path(pgp->paths, pp))
			goto out1;

		set_bit_in_bitfield(i, bitmap);

		for (j = i + 1; j < VECTOR_SIZE(paths); j++) {

			if (is_bit_set_in_bitfield(j, bitmap))
				continue;

			pp2 = VECTOR_SLOT(paths, j);

			if (path_match_fn(pp, pp2)) {
				if (store_path(pgp->paths, pp2))
					goto out1;

				set_bit_in_bitfield(j, bitmap);
			}
		}
	}
	free(bitmap);
	return 0;
out2:
	free_pathgroup(pgp, KEEP_PATHS);
out1:
	free(bitmap);
out:
	free_pgvec(mp->pg, KEEP_PATHS);
	mp->pg = NULL;
	return 1;
}

/*
 * One path group per unique tgt_node_name present in the path vector
 */
int group_by_node_name(struct multipath * mp, vector paths)
{
	return group_by_match(mp, paths, node_names_match);
}

/*
 * One path group per unique serial number present in the path vector
 */
int group_by_serial(struct multipath * mp, vector paths)
{
	return group_by_match(mp, paths, serials_match);
}

/*
 * One path group per priority present in the path vector
 */
int group_by_prio(struct multipath *mp, vector paths)
{
	return group_by_match(mp, paths, prios_match);
}

/*
 * One path group per alua target port group present in the path vector
 */
int group_by_tpg(struct multipath *mp, vector paths)
{
	return group_by_match(mp, paths, tpg_match);
}

int one_path_per_group(struct multipath *mp, vector paths)
{
	int i;
	struct path * pp;
	struct pathgroup * pgp;

	for (i = 0; i < VECTOR_SIZE(paths); i++) {
		pp = VECTOR_SLOT(paths, i);
		pgp = alloc_pathgroup();

		if (!pgp)
			goto out;

		if (add_pathgroup(mp, pgp))
			goto out1;

		if (store_path(pgp->paths, pp))
			goto out;
	}
	return 0;
out1:
	free_pathgroup(pgp, KEEP_PATHS);
out:
	free_pgvec(mp->pg, KEEP_PATHS);
	mp->pg = NULL;
	return 1;
}

int one_group(struct multipath *mp, vector paths)	/* aka multibus */
{
	int i;
	struct path * pp;
	struct pathgroup * pgp;

	pgp = alloc_pathgroup();

	if (!pgp)
		goto out;

	if (add_pathgroup(mp, pgp))
		goto out1;

	for (i = 0; i < VECTOR_SIZE(paths); i++) {
		pp = VECTOR_SLOT(paths, i);

		if (store_path(pgp->paths, pp))
			goto out;
	}
	return 0;
out1:
	free_pathgroup(pgp, KEEP_PATHS);
out:
	free_pgvec(mp->pg, KEEP_PATHS);
	mp->pg = NULL;
	return 1;
}
