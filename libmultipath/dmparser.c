/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 * Copyright (c) 2005 Stefan Bader, IBM
 * Copyright (c) 2005 Edward Goggin, EMC
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "checkers.h"
#include "vector.h"
#include "memory.h"
#include "structs.h"
#include "util.h"
#include "debug.h"
#include "dmparser.h"
#include "strbuf.h"

#define WORD_SIZE 64

static int
merge_words(char **dst, const char *word)
{
	char * p = *dst;
	int len, dstlen;

	dstlen = strlen(*dst);
	len = dstlen + strlen(word) + 2;
	*dst = REALLOC(*dst, len);

	if (!*dst) {
		free(p);
		return 1;
	}

	p = *dst + dstlen;
	*p = ' ';
	++p;
	strncpy(p, word, len - dstlen - 1);

	return 0;
}

/*
 * Transforms the path group vector into a proper device map string
 */
int assemble_map(struct multipath *mp, char **params)
{
	static const char no_path_retry[] = "queue_if_no_path";
	static const char retain_hwhandler[] = "retain_attached_hw_handler";
	int i, j;
	int minio;
	int nr_priority_groups, initial_pg_nr;
	STRBUF_ON_STACK(buff);
	struct pathgroup * pgp;
	struct path * pp;

	minio = mp->minio;

	nr_priority_groups = VECTOR_SIZE(mp->pg);
	initial_pg_nr = (nr_priority_groups ? mp->bestpg : 0);

	if (mp->no_path_retry != NO_PATH_RETRY_UNDEF  &&
	    mp->no_path_retry != NO_PATH_RETRY_FAIL) {
		add_feature(&mp->features, no_path_retry);
	}
	if (mp->retain_hwhandler == RETAIN_HWHANDLER_ON &&
	    get_linux_version_code() < KERNEL_VERSION(4, 3, 0))
		add_feature(&mp->features, retain_hwhandler);

	if (print_strbuf(&buff, "%s %s %i %i", mp->features, mp->hwhandler,
			 nr_priority_groups, initial_pg_nr) < 0)
		goto err;

	vector_foreach_slot (mp->pg, pgp, i) {
		pgp = VECTOR_SLOT(mp->pg, i);
		if (print_strbuf(&buff, " %s %i 1", mp->selector,
				 VECTOR_SIZE(pgp->paths)) < 0)
			goto err;

		vector_foreach_slot (pgp->paths, pp, j) {
			int tmp_minio = minio;

			if (mp->rr_weight == RR_WEIGHT_PRIO
			    && pp->priority > 0)
				tmp_minio = minio * pp->priority;
			if (!strlen(pp->dev_t) ) {
				condlog(0, "dev_t not set for '%s'", pp->dev);
				goto err;
			}
			if (print_strbuf(&buff, " %s %d", pp->dev_t, tmp_minio) < 0)
				goto err;
		}
	}

	*params = steal_strbuf_str(&buff);
	condlog(4, "%s: assembled map [%s]", mp->alias, *params);
	return 0;

err:
	return 1;
}

/*
 * Caution callers: If this function encounters yet unkown path devices, it
 * adds them uninitialized to the mpp.
 * Call update_pathvec_from_dm() after this function to make sure
 * all data structures are in a sane state.
 */
int disassemble_map(const struct _vector *pathvec,
		    const char *params, struct multipath *mpp)
{
	char * word;
	const char *p;
	int i, j, k;
	int num_features = 0;
	int num_hwhandler = 0;
	int num_pg = 0;
	int num_pg_args = 0;
	int num_paths = 0;
	int num_paths_args = 0;
	int def_minio = 0;
	struct path * pp;
	struct pathgroup * pgp;

	assert(pathvec != NULL);
	p = params;

	condlog(4, "%s: disassemble map [%s]", mpp->alias, params);

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

		if (merge_words(&mpp->features, word)) {
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

		if (merge_words(&mpp->hwhandler, word)) {
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

	if (num_pg > 0) {
		if (!mpp->pg) {
			mpp->pg = vector_alloc();
			if (!mpp->pg)
				return 1;
		}
	} else {
		free_pgvec(mpp->pg, KEEP_PATHS);
		mpp->pg = NULL;
	}

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

			if (merge_words(&mpp->selector, word))
				goto out1;
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

		if (add_pathgroup(mpp, pgp)) {
			free_pathgroup(pgp, KEEP_PATHS);
			goto out;
		}

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

			pp = find_path_by_devt(pathvec, word);

			if (!pp) {
				pp = alloc_path();

				if (!pp)
					goto out1;

				strlcpy(pp->dev_t, word, BLK_DEV_SIZE);

				if (store_path(pgp->paths, pp)) {
					free_path(pp);
					goto out1;
				}
			} else if (store_path(pgp->paths, pp))
				goto out1;

			FREE(word);

			pgp->id ^= (long)pp;
			pp->pgindex = i + 1;

			for (k = 0; k < num_paths_args; k++)
				if (k == 0) {
					p += get_word(p, &word);
					def_minio = atoi(word);
					FREE(word);

					if (!strncmp(mpp->selector,
						     "round-robin", 11)) {

						if (mpp->rr_weight == RR_WEIGHT_PRIO
						    && pp->priority > 0)
							def_minio /= pp->priority;

					}

					if (def_minio != mpp->minio)
						mpp->minio = def_minio;
				}
				else
					p += get_word(p, NULL);

		}
	}
	return 0;
out1:
	FREE(word);
out:
	free_pgvec(mpp->pg, KEEP_PATHS);
	mpp->pg = NULL;
	return 1;
}

int disassemble_status(const char *params, struct multipath *mpp)
{
	char *word;
	const char *p;
	int i, j, k;
	int num_feature_args;
	int num_hwhandler_args;
	int num_pg;
	int num_pg_args;
	int num_paths;
	int def_minio = 0;
	struct path * pp;
	struct pathgroup * pgp;

	p = params;

	condlog(4, "%s: disassemble status [%s]", mpp->alias, params);

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

	if (num_pg == 0)
		return 0;

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
			pgp->status = PGSTATE_UNDEF;
			break;
		}
		FREE(word);

		/*
		 * PG Status (discarded, would be '0' anyway)
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

			/*
			 * selector args
			 */
			for (k = 0; k < num_pg_args; k++) {
				if (!strncmp(mpp->selector,
					     "least-pending", 13)) {
					p += get_word(p, &word);
					if (sscanf(word,"%d:*d",
						   &def_minio) == 1 &&
					    def_minio != mpp->minio)
							mpp->minio = def_minio;
					FREE(word);
				} else
					p += get_word(p, NULL);
			}
		}
	}
	return 0;
}
