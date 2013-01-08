/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 * Copyright (c) 2004 Stefan Bader, IBM
 */
#include <stdio.h>
#include <unistd.h>
#include <libdevmapper.h>
#include <libudev.h>

#include "checkers.h"
#include "memory.h"
#include "vector.h"
#include "util.h"
#include "structs.h"
#include "config.h"
#include "debug.h"
#include "structs_vec.h"
#include "blacklist.h"
#include "prio.h"

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
		pp->priority = PRIO_UNDEF;
	}
	return pp;
}

void
free_path (struct path * pp)
{
	if (!pp)
		return;

	if (checker_selected(&pp->checker))
		checker_put(&pp->checker);

	if (prio_selected(&pp->prio))
		prio_put(&pp->prio);

	if (pp->fd >= 0)
		close(pp->fd);

	if (pp->udev) {
		udev_device_unref(pp->udev);
		pp->udev = NULL;
	}

	FREE(pp);
}

void
free_pathvec (vector vec, enum free_path_mode free_paths)
{
	int i;
	struct path * pp;

	if (!vec)
		return;

	if (free_paths == FREE_PATHS)
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

	if (!pgp->paths) {
		FREE(pgp);
		pgp = NULL;
	}

	return pgp;
}

void
free_pathgroup (struct pathgroup * pgp, enum free_path_mode free_paths)
{
	if (!pgp)
		return;

	free_pathvec(pgp->paths, free_paths);
	FREE(pgp);
}

void
free_pgvec (vector pgvec, enum free_path_mode free_paths)
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

	if (mpp) {
		mpp->bestpg = 1;
		mpp->mpcontext = NULL;
		mpp->no_path_retry = NO_PATH_RETRY_UNDEF;
		mpp->fast_io_fail = MP_FAST_IO_FAIL_UNSET;
	}
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

	if (mpp->features) {
		FREE(mpp->features);
		mpp->features = NULL;
	}

	if (mpp->hwhandler &&
	    mpp->hwhandler != conf->hwhandler &&
	    (!mpp->hwe || (mpp->hwe && mpp->hwhandler != mpp->hwe->hwhandler))) {
		FREE(mpp->hwhandler);
		mpp->hwhandler = NULL;
	}
}

void
free_multipath (struct multipath * mpp, enum free_path_mode free_paths)
{
	if (!mpp)
		return;

	free_multipath_attributes(mpp);

	if (mpp->alias) {
		FREE(mpp->alias);
		mpp->alias = NULL;
	}

	if (mpp->dmi) {
		FREE(mpp->dmi);
		mpp->dmi = NULL;
	}

	free_pathvec(mpp->paths, free_paths);
	free_pgvec(mpp->pg, free_paths);
	FREE_PTR(mpp->mpcontext);
	FREE(mpp);
}

void
drop_multipath (vector mpvec, char * wwid, enum free_path_mode free_paths)
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
free_multipathvec (vector mpvec, enum free_path_mode free_paths)
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
find_mp_by_minor (vector mpvec, int minor)
{
	int i;
	struct multipath * mpp;

	if (!mpvec)
		return NULL;

	vector_foreach_slot (mpvec, mpp, i) {
		if (!mpp->dmi)
			continue;

		if (mpp->dmi->minor == minor)
			return mpp;
	}
	return NULL;
}

struct multipath *
find_mp_by_wwid (vector mpvec, char * wwid)
{
	int i;
	struct multipath * mpp;

	if (!mpvec)
		return NULL;

	vector_foreach_slot (mpvec, mpp, i)
		if (!strncmp(mpp->wwid, wwid, WWID_SIZE))
			return mpp;

	return NULL;
}

struct multipath *
find_mp_by_alias (vector mpvec, char * alias)
{
	int i;
	int len;
	struct multipath * mpp;

	if (!mpvec)
		return NULL;

	len = strlen(alias);

	if (!len)
		return NULL;

	vector_foreach_slot (mpvec, mpp, i) {
		if (strlen(mpp->alias) == len &&
		    !strncmp(mpp->alias, alias, len))
			return mpp;
	}
	return NULL;
}

struct multipath *
find_mp_by_str (vector mpvec, char * str)
{
	int minor;

	if (sscanf(str, "dm-%d", &minor) == 1)
		return find_mp_by_minor(mpvec, minor);
	else
		return find_mp_by_alias(mpvec, str);
}

struct path *
find_path_by_dev (vector pathvec, char * dev)
{
	int i;
	struct path * pp;

	if (!pathvec)
		return NULL;

	vector_foreach_slot (pathvec, pp, i)
		if (!strcmp(pp->dev, dev))
			return pp;

	condlog(3, "%s: not found in pathvec", dev);
	return NULL;
}

struct path *
find_path_by_devt (vector pathvec, char * dev_t)
{
	int i;
	struct path * pp;

	if (!pathvec)
		return NULL;

	vector_foreach_slot (pathvec, pp, i)
		if (!strcmp(pp->dev_t, dev_t))
			return pp;

	condlog(3, "%s: not found in pathvec", dev_t);
	return NULL;
}

extern int
pathcountgr (struct pathgroup * pgp, int state)
{
	struct path *pp;
	int count = 0;
	int i;

	vector_foreach_slot (pgp->paths, pp, i)
		if ((pp->state == state) || (state == PATH_WILD))
			count++;

	return count;
}

extern int
pathcount (struct multipath * mpp, int state)
{
	struct pathgroup *pgp;
	int count = 0;
	int i;

	if (mpp->pg) {
		vector_foreach_slot (mpp->pg, pgp, i)
			count += pathcountgr(pgp, state);
	}
	return count;
}

extern int
pathcmp (struct pathgroup *pgp, struct pathgroup *cpgp)
{
	int i, j;
	struct path *pp, *cpp;
	int pnum = 0, found = 0;

	vector_foreach_slot(pgp->paths, pp, i) {
		pnum++;
		vector_foreach_slot(cpgp->paths, cpp, j) {
			if ((long)pp == (long)cpp) {
				found++;
				break;
			}
		}
	}

	return pnum - found;
}

struct path *
first_path (struct multipath * mpp)
{
	struct pathgroup * pgp;
	if (!mpp->pg)
		return NULL;
	pgp = VECTOR_SLOT(mpp->pg, 0);

	return pgp?VECTOR_SLOT(pgp->paths, 0):NULL;
}

extern void
setup_feature(struct multipath * mpp, char *feature)
{
	if (!strncmp(feature, "queue_if_no_path", 16)) {
		if (mpp->no_path_retry <= NO_PATH_RETRY_UNDEF)
			mpp->no_path_retry = NO_PATH_RETRY_QUEUE;
	}
}

extern int
add_feature (char **f, char *n)
{
	int c = 0, d, l;
	char *e, *p, *t;

	if (!f)
		return 1;

	/* Nothing to do */
	if (!n || *n == '0')
		return 0;

	/* Check if feature is already present */
	if (strstr(*f, n))
		return 0;

	/* Get feature count */
	c = strtoul(*f, &e, 10);
	if (*f == e)
		/* parse error */
		return 1;

	/* Check if we need to increase feature count space */
	l = strlen(*f) + strlen(n) + 1;

	/* Count new features */
	if ((c % 10) == 9)
		l++;
	c++;
	p = n;
	while (*p != '\0') {
		if (*p == ' ' && p[1] != '\0' && p[1] != ' ') {
			if ((c % 10) == 9)
				l++;
			c++;
		}
		p++;
	}

	t = MALLOC(l + 1);
	if (!t)
		return 1;

	memset(t, 0, l + 1);

	/* Update feature count */
	d = c;
	l = 1;
	while (d > 9) {
		d /= 10;
		l++;
	}
	p = t;
	snprintf(p, l + 2, "%0d ", c);

	/* Copy the feature string */
	p = strchr(*f, ' ');
	if (p) {
		while (*p == ' ')
			p++;
		strcat(t, p);
		strcat(t, " ");
	} else {
		p = t + strlen(t);
	}
	strcat(t, n);

	FREE(*f);
	*f = t;

	return 0;
}

extern int
remove_feature(char **f, char *o)
{
	int c = 0, d, l;
	char *e, *p, *n;

	if (!f || !*f)
		return 1;

	/* Nothing to do */
	if (!o || *o == '\0')
		return 0;

	/* Check if not present */
	if (!strstr(*f, o))
		return 0;

	/* Get feature count */
	c = strtoul(*f, &e, 10);
	if (*f == e)
		/* parse error */
		return 1;

	/* Normalize features */
	while (*o == ' ') {
		o++;
	}
	/* Just spaces, return */
	if (*o == '\0')
		return 0;
	e = o + strlen(o);
	while (*e == ' ')
		e--;
	d = (int)(e - o);

	/* Update feature count */
	c--;
	p = o;
	while (p[0] != '\0') {
		if (p[0] == ' ' && p[1] != ' ' && p[1] != '\0')
			c--;
		p++;
	}

	/* Quick exit if all features have been removed */
	if (c == 0) {
		n = MALLOC(2);
		if (!n)
			return 1;
		strcpy(n, "0");
		goto out;
	}

	/* Search feature to be removed */
	e = strstr(*f, o);
	if (!e)
		/* Not found, return */
		return 0;

	/* Update feature count space */
	l = strlen(*f) - d;
	n =  MALLOC(l + 1);
	if (!n)
		return 1;

	/* Copy the feature count */
	sprintf(n, "%0d", c);
	/*
	 * Copy existing features up to the feature
	 * about to be removed
	 */
	p = strchr(*f, ' ');
	if (!p)
		/* Internal error, feature string inconsistent */
		return 1;
	while (*p == ' ')
		p++;
	p--;
	if (e != p) {
		do {
			e--;
			d++;
		} while (*e == ' ');
		e++; d--;
		strncat(n, p, (size_t)(e - p));
		p += (size_t)(e - p);
	}
	/* Skip feature to be removed */
	p += d;

	/* Copy remaining features */
	if (strlen(p)) {
		while (*p == ' ')
			p++;
		if (strlen(p)) {
			p--;
			strcat(n, p);
		}
	}

out:
	FREE(*f);
	*f = n;

	return 0;
}

