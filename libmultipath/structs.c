/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 * Copyright (c) 2004 Stefan Bader, IBM
 */
#include <stdio.h>
#include <unistd.h>
#include <libdevmapper.h>

#include "memory.h"
#include "vector.h"
#include "util.h"
#include "structs.h"
#include "config.h"
#include "debug.h"

struct path *
alloc_path (void)
{
	struct path * pp;
	
	pp = (struct path *)MALLOC(sizeof(struct path));

	if (pp) {
		pp->sg_id.host_no = -1;
		pp->sg_id.channel = -1;
		pp->sg_id.scsi_id = -1;
		pp->sg_id.lun = -1;
		pp->fd = -1;
	}
	return pp;
}

void
free_path (struct path * pp)
{
	if (!pp)
		return;

	if (pp->checker_context)
		free(pp->checker_context);

	if (pp->fd >= 0)
		close(pp->fd);

	FREE(pp);
}

void
free_pathvec (vector vec, int free_paths)
{
	int i;
	struct path * pp;

	if (!vec)
		return;

	if (free_paths)
		vector_foreach_slot(vec, pp, i)
			free_path(pp);

	vector_free(vec);
}

struct pathgroup *
alloc_pathgroup (void)
{
	struct pathgroup * pgp;

	pgp = (struct pathgroup *)MALLOC(sizeof(struct pathgroup));

	if (!pgp)
		return NULL;

	pgp->paths = vector_alloc();

	if (!pgp->paths)
		FREE(pgp);

	return pgp;
}

void
free_pathgroup (struct pathgroup * pgp, int free_paths)
{
	if (!pgp)
		return;

	free_pathvec(pgp->paths, free_paths);
	FREE(pgp);
}

void
free_pgvec (vector pgvec, int free_paths)
{
	int i;
	struct pathgroup * pgp;

	if (!pgvec)
		return;

	vector_foreach_slot(pgvec, pgp, i)
		free_pathgroup(pgp, free_paths);

	vector_free(pgvec);
}

struct multipath *
alloc_multipath (void)
{
	struct multipath * mpp;

	mpp = (struct multipath *)MALLOC(sizeof(struct multipath));

	if (mpp)
		mpp->bestpg = 1;

	return mpp;
}

extern void
free_multipath_attributes (struct multipath * mpp)
{
	if (!mpp)
		return;

	if (mpp->selector &&
	    mpp->selector != conf->selector &&
	    (!mpp->mpe || (mpp->mpe && mpp->selector != mpp->mpe->selector)) &&
	    (!mpp->hwe || (mpp->hwe && mpp->selector != mpp->hwe->selector))) {
		FREE(mpp->selector);
		mpp->selector = NULL;
	}

	if (mpp->features &&
	    mpp->features != conf->features &&
	    (!mpp->hwe || (mpp->hwe && mpp->features != mpp->hwe->features))) {
		FREE(mpp->features);
		mpp->features = NULL;
	}

	if (mpp->hwhandler &&
	    mpp->hwhandler != conf->default_hwhandler &&
	    (!mpp->hwe || (mpp->hwe && mpp->hwhandler != mpp->hwe->hwhandler))) {
		FREE(mpp->hwhandler);
		mpp->hwhandler = NULL;
	}
}

void
free_multipath (struct multipath * mpp, int free_paths)
{
	if (!mpp)
		return;

	free_multipath_attributes(mpp);

	if (mpp->alias &&
	    (!mpp->mpe || (mpp->mpe && mpp->alias != mpp->mpe->alias)) &&
	    (mpp->wwid && mpp->alias != mpp->wwid)) {
		FREE(mpp->alias);
		mpp->alias = NULL;
	}

	if (mpp->dmi)
		FREE(mpp->dmi);
	
	free_pathvec(mpp->paths, free_paths);
	free_pgvec(mpp->pg, free_paths);
	FREE(mpp);
}

void
drop_multipath (vector mpvec, char * wwid, int free_paths)
{
	int i;
	struct multipath * mpp;

	if (!mpvec)
		return;

	vector_foreach_slot (mpvec, mpp, i) {
		if (!strncmp(mpp->wwid, wwid, WWID_SIZE)) {
			free_multipath(mpp, free_paths);
			vector_del_slot(mpvec, i);
			return;
		}
	}
}

void
free_multipathvec (vector mpvec, int free_paths)
{
	int i;
	struct multipath * mpp;

	if (!mpvec)
		return;

	vector_foreach_slot (mpvec, mpp, i)
		free_multipath(mpp, free_paths);

	vector_free(mpvec);
}

int
store_path (vector pathvec, struct path * pp)
{
	if (!vector_alloc_slot(pathvec))
		return 1;

	vector_set_slot(pathvec, pp);

	return 0;
}

int
store_pathgroup (vector pgvec, struct pathgroup * pgp)
{
	if (!vector_alloc_slot(pgvec))
		return 1;

	vector_set_slot(pgvec, pgp);

	return 0;
}

struct multipath *
find_mp_by_minor (vector mp, int minor)
{
	int i;
	struct multipath * mpp;
	
	vector_foreach_slot (mp, mpp, i) {
		if (!mpp->dmi)
			continue;

		if (mpp->dmi->minor == minor)
			return mpp;
	}
	return NULL;
}

struct multipath *
find_mp_by_wwid (vector mp, char * wwid)
{
	int i;
	struct multipath * mpp;
	
	vector_foreach_slot (mp, mpp, i)
		if (!strncmp(mpp->wwid, wwid, WWID_SIZE))
			return mpp;

	return NULL;
}

struct multipath *
find_mp_by_alias (vector mp, char * alias)
{
	int i;
	int len;
	struct multipath * mpp;
	
	len = strlen(alias);

	if (!len)
		return NULL;
	
	vector_foreach_slot (mp, mpp, i) {
		if (strlen(mpp->alias) == len &&
		    !strncmp(mpp->alias, alias, len))
			return mpp;
	}
	return NULL;
}

struct path *
find_path_by_dev (vector pathvec, char * dev)
{
	int i;
	struct path * pp;
	
	vector_foreach_slot (pathvec, pp, i)
		if (!strcmp_chomp(pp->dev, dev))
			return pp;

	condlog(3, "path %s not found in pathvec\n", dev);
	return NULL;
}

struct path *
find_path_by_devt (vector pathvec, char * dev_t)
{
	int i;
	struct path * pp;

	vector_foreach_slot (pathvec, pp, i)
		if (!strcmp_chomp(pp->dev_t, dev_t))
			return pp;

	condlog(3, "path %s not found in pathvec\n", dev_t);
	return NULL;
}

extern int
pathcount (struct multipath * mpp, int state)
{
	struct pathgroup *pgp;
	struct path *pp;
	int i, j;
	int count = 0;

	vector_foreach_slot (mpp->pg, pgp, i)
		vector_foreach_slot (pgp->paths, pp, j)
			if (pp->state == state)
				count++;

	return count;
}

