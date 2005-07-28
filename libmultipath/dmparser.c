/*
 * Christophe Varoqui (2004)
 * This code is GPLv2, see license file
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vector.h"
#include "memory.h"
#include "structs.h"
#include "util.h"
#include "debug.h"

#define WORD_SIZE 64

static int
merge_words (char ** dst, char * word, int space)
{
	char * p;
	int len;

	len = strlen(*dst) + strlen(word) + space;
	*dst = REALLOC(*dst, len + 1);

	if (!*dst)
		return 1;

	p = *dst;

	while (*p != '\0')
		p++;

	while (space) {
		*p = ' ';
		p++;
		space--;
	}
	strncpy(p, word, strlen(word) + 1);

	return 0;
}

extern int
disassemble_map (vector pathvec, char * params, struct multipath * mpp)
{
	char * word;
	char * p;
	int i, j, k;
	int num_features = 0;
	int num_hwhandler = 0;
	int num_pg = 0;
	int num_pg_args = 0;
	int num_paths = 0;
	int num_paths_args = 0;
	struct path * pp;
	struct pathgroup * pgp;

	p = params;

	/*
	 * features
	 */
	p += get_word(p, &mpp->features);

	if (!mpp->features)
		return 1;

	num_features = atoi(mpp->features);

	for (i = 0; i < num_features; i++) {
		p += get_word(p, &word);

		if (!word)
			return 1;

		if (merge_words(&mpp->features, word, 1)) {
			FREE(word);
			return 1;
		}
		FREE(word);
	}

	/*
	 * hwhandler
	 */
	p += get_word(p, &mpp->hwhandler);

	if (!mpp->hwhandler)
		return 1;

	num_hwhandler = atoi(mpp->hwhandler);

	for (i = 0; i < num_hwhandler; i++) {
		p += get_word(p, &word);

		if (!word)
			return 1;

		if (merge_words(&mpp->hwhandler, word, 1)) {
			FREE(word);
			return 1;
		}
		FREE(word);
	}

	/*
	 * nb of path groups
	 */
	p += get_word(p, &word);

	if (!word)
		return 1;

	num_pg = atoi(word);
	FREE(word);

	if (num_pg > 0 && !mpp->pg)
		mpp->pg = vector_alloc();
	
	if (!mpp->pg)
		return 1;
	/*
	 * first pg to try
	 */
	p += get_word(p, &word);

	if (!word)
		goto out;

	mpp->nextpg = atoi(word);
	FREE(word);

	for (i = 0; i < num_pg; i++) {
		/*
		 * selector
		 */

		if (!mpp->selector) {
			p += get_word(p, &mpp->selector);

			if (!mpp->selector)
				goto out;

			/*
			 * selector args
			 */
			p += get_word(p, &word);

			if (!word)
				goto out;

			num_pg_args = atoi(word);
			
			if (merge_words(&mpp->selector, word, 1)) {
				FREE(word);
				goto out1;
			}
			FREE(word);
		} else {
			p += get_word(p, NULL);
			p += get_word(p, NULL);
		}

		for (j = 0; j < num_pg_args; j++)
			p += get_word(p, NULL);

		/*
		 * paths
		 */
		pgp = alloc_pathgroup();
		
		if (!pgp)
			goto out;

		if (store_pathgroup(mpp->pg, pgp))
			goto out;

		p += get_word(p, &word);

		if (!word)
			goto out;

		num_paths = atoi(word);
		FREE(word);

		p += get_word(p, &word);

		if (!word)
			goto out;

		num_paths_args = atoi(word);
		FREE(word);

		for (j = 0; j < num_paths; j++) {
			pp = NULL;
			p += get_word(p, &word);

			if (!word)
				goto out;

			if (pathvec)
				pp = find_path_by_devt(pathvec, word);

			if (!pp) {
				pp = alloc_path();

				if (!pp)
					goto out1;

				strncpy(pp->dev_t, word, BLK_DEV_SIZE);
			}
			FREE(word);

			if (store_path(pgp->paths, pp))
				goto out;

			/*
			 * Update wwid for multipaths which are not setup
			 * in the get_dm_mpvec() code path
			 */
			if (!strlen(mpp->wwid))
				strncpy(mpp->wwid, pp->wwid, WWID_SIZE);

			/*
			 * Update wwid for paths which may not have been
			 * active at the time the getuid callout was run
			 */
			else if (!strlen(pp->wwid))
				strncpy(pp->wwid, mpp->wwid, WWID_SIZE);

			pgp->id ^= (long)pp;
			pp->pgindex = i + 1;

			for (k = 0; k < num_paths_args; k++)
				p += get_word(p, NULL);
		}
	}
	return 0;
out1:
	FREE(word);
out:
	free_pgvec(mpp->pg, KEEP_PATHS);
	return 1;
}

extern int
disassemble_status (char * params, struct multipath * mpp)
{
	char * word;
	char * p;
	int i, j;
	int num_feature_args;
	int num_hwhandler_args;
	int num_pg;
	int num_pg_args;
	int num_paths;
	struct path * pp;
	struct pathgroup * pgp;

	p = params;

	/*
	 * features
	 */
	p += get_word(p, &word);

	if (!word)
		return 1;

	num_feature_args = atoi(word);
	FREE(word);

	for (i = 0; i < num_feature_args; i++) {
		if (i == 1) {
			p += get_word(p, &word);

			if (!word)
				return 1;

			mpp->queuedio = atoi(word);
			FREE(word);
			continue;
		}
		/* unknown */
		p += get_word(p, NULL);
	}
	/*
	 * hwhandler
	 */
	p += get_word(p, &word);

	if (!word)
		return 1;

	num_hwhandler_args = atoi(word);
	FREE(word);

	for (i = 0; i < num_hwhandler_args; i++)
		p += get_word(p, NULL);

	/*
	 * nb of path groups
	 */
	p += get_word(p, &word);

	if (!word)
		return 1;

	num_pg = atoi(word);
	FREE(word);

	/*
	 * next pg to try
	 */
	p += get_word(p, NULL);

	if (VECTOR_SIZE(mpp->pg) < num_pg)
		return 1;

	for (i = 0; i < num_pg; i++) {
		pgp = VECTOR_SLOT(mpp->pg, i);
		/*
		 * PG status
		 */
		p += get_word(p, &word);

		if (!word)
			return 1;

		switch (*word) {
		case 'D':
			pgp->status = PGSTATE_DISABLED;
			break;
		case 'A':
			pgp->status = PGSTATE_ACTIVE;
			break;
		case 'E':
			pgp->status = PGSTATE_ENABLED;
			break;
		default:
			pgp->status = PGSTATE_RESERVED;
			break;
		}
		FREE(word);

		/*
		 * undef ?
		 */
		p += get_word(p, NULL);

		p += get_word(p, &word);

		if (!word)
			return 1;

		num_paths = atoi(word);
		FREE(word);

		p += get_word(p, &word);

		if (!word)
			return 1;

		num_pg_args = atoi(word);
		FREE(word);

		if (VECTOR_SIZE(pgp->paths) < num_paths)
			return 1;

		for (j = 0; j < num_paths; j++) {
			pp = VECTOR_SLOT(pgp->paths, j);
			/*
			 * path
			 */
			p += get_word(p, NULL);

			/*
			 * path status
			 */
			p += get_word(p, &word);

			if (!word)
				return 1;

			switch (*word) {
			case 'F':
				pp->dmstate = PSTATE_FAILED;
				break;
			case 'A':
				pp->dmstate = PSTATE_ACTIVE;
				break;
			default:
				break;
			}
			FREE(word);
			/*
			 * fail count
			 */
			p += get_word(p, &word);

			if (!word)
				return 1;

			pp->failcount = atoi(word);
			FREE(word);
		}
		/*
		 * selector args
		 */
		for (j = 0; j < num_pg_args; j++)
			p += get_word(p, NULL);
	}
	return 0;
}
