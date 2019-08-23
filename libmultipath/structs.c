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
#include "prioritizers/alua_spc3.h"
#include "dm-generic.h"

struct adapter_group *
alloc_adaptergroup(void)
{
	struct adapter_group *agp;

	agp = (struct adapter_group *)MALLOC(sizeof(struct adapter_group));

	if (!agp)
		return NULL;

	agp->host_groups = vector_alloc();
	if (!agp->host_groups) {
		FREE(agp);
		agp = NULL;
	}
	return agp;
}

void free_adaptergroup(vector adapters)
{
	int i;
	struct adapter_group *agp;

	vector_foreach_slot(adapters, agp, i) {
		free_hostgroup(agp->host_groups);
		FREE(agp);
	}
	vector_free(adapters);
}

void free_hostgroup(vector hostgroups)
{
	int i;
	struct host_group *hgp;

	if (!hostgroups)
		return;

	vector_foreach_slot(hostgroups, hgp, i) {
		vector_free(hgp->paths);
		FREE(hgp);
	}
	vector_free(hostgroups);
}

struct host_group *
alloc_hostgroup(void)
{
	struct host_group *hgp;

	hgp = (struct host_group *)MALLOC(sizeof(struct host_group));

	if (!hgp)
		return NULL;

	hgp->paths = vector_alloc();

	if (!hgp->paths) {
		FREE(hgp);
		hgp = NULL;
	}
	return hgp;
}

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
		pp->sg_id.proto_id = SCSI_PROTOCOL_UNSPEC;
		pp->fd = -1;
		pp->tpgs = TPGS_UNDEF;
		pp->priority = PRIO_UNDEF;
		pp->checkint = CHECKINT_UNDEF;
		checker_clear(&pp->checker);
		dm_path_to_gen(pp)->ops = &dm_gen_path_ops;
		pp->hwe = vector_alloc();
		if (pp->hwe == NULL) {
			free(pp);
			return NULL;
		}
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
	vector_free(pp->hwe);

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
		return NULL;
	}

	dm_pathgroup_to_gen(pgp)->ops = &dm_gen_pathgroup_ops;
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
		dm_multipath_to_gen(mpp)->ops = &dm_gen_multipath_ops;
	}
	return mpp;
}

void free_multipath_attributes(struct multipath *mpp)
{
	if (!mpp)
		return;

	if (mpp->selector) {
		FREE(mpp->selector);
		mpp->selector = NULL;
	}

	if (mpp->features) {
		FREE(mpp->features);
		mpp->features = NULL;
	}

	if (mpp->hwhandler) {
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
	int err = 0;

	if (!strlen(pp->dev_t)) {
		condlog(2, "%s: Empty device number", pp->dev);
		err++;
	}
	if (!strlen(pp->dev)) {
		condlog(2, "%s: Empty device name", pp->dev_t);
		err++;
	}

	if (err > 1)
		return 1;

	if (!vector_alloc_slot(pathvec))
		return 1;

	vector_set_slot(pathvec, pp);

	return 0;
}

int add_pathgroup(struct multipath *mpp, struct pathgroup *pgp)
{
	if (!vector_alloc_slot(mpp->pg))
		return 1;

	vector_set_slot(mpp->pg, pgp);

	pgp->mpp = mpp;
	return 0;
}

int
store_hostgroup(vector hostgroupvec, struct host_group * hgp)
{
	if (!vector_alloc_slot(hostgroupvec))
		return 1;

	vector_set_slot(hostgroupvec, hgp);
	return 0;
}

int
store_adaptergroup(vector adapters, struct adapter_group * agp)
{
	if (!vector_alloc_slot(adapters))
		return 1;

	vector_set_slot(adapters, agp);
	return 0;
}

struct multipath *
find_mp_by_minor (const struct _vector *mpvec, int minor)
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
find_mp_by_wwid (const struct _vector *mpvec, const char * wwid)
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
find_mp_by_alias (const struct _vector *mpvec, const char * alias)
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
find_mp_by_str (const struct _vector *mpvec, const char * str)
{
	int minor;

	if (sscanf(str, "dm-%d", &minor) == 1)
		return find_mp_by_minor(mpvec, minor);
	else
		return find_mp_by_alias(mpvec, str);
}

struct path *
find_path_by_dev (const struct _vector *pathvec, const char * dev)
{
	int i;
	struct path * pp;

	if (!pathvec)
		return NULL;

	vector_foreach_slot (pathvec, pp, i)
		if (!strcmp(pp->dev, dev))
			return pp;

	condlog(4, "%s: dev not found in pathvec", dev);
	return NULL;
}

struct path *
find_path_by_devt (const struct _vector *pathvec, const char * dev_t)
{
	int i;
	struct path * pp;

	if (!pathvec)
		return NULL;

	vector_foreach_slot (pathvec, pp, i)
		if (!strcmp(pp->dev_t, dev_t))
			return pp;

	condlog(4, "%s: dev_t not found in pathvec", dev_t);
	return NULL;
}

int pathcountgr(const struct pathgroup *pgp, int state)
{
	struct path *pp;
	int count = 0;
	int i;

	vector_foreach_slot (pgp->paths, pp, i)
		if ((pp->state == state) || (state == PATH_WILD))
			count++;

	return count;
}

int pathcount(const struct multipath *mpp, int state)
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

int pathcmp(const struct pathgroup *pgp, const struct pathgroup *cpgp)
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
first_path (const struct multipath * mpp)
{
	struct pathgroup * pgp;
	if (!mpp->pg)
		return NULL;
	pgp = VECTOR_SLOT(mpp->pg, 0);

	return pgp?VECTOR_SLOT(pgp->paths, 0):NULL;
}

int add_feature(char **f, const char *n)
{
	int c = 0, d, l;
	char *e, *t;

	if (!f)
		return 1;

	/* Nothing to do */
	if (!n || *n == '0')
		return 0;

	if (strchr(n, ' ') != NULL) {
		condlog(0, "internal error: feature \"%s\" contains spaces", n);
		return 1;
	}

	/* default feature is null */
	if(!*f)
	{
		l = asprintf(&t, "1 %s", n);
		if(l == -1)
			return 1;

		*f = t;
		return 0;
	}

	/* Check if feature is already present */
	if (strstr(*f, n))
		return 0;

	/* Get feature count */
	c = strtoul(*f, &e, 10);
	if (*f == e || (*e != ' ' && *e != '\0')) {
		condlog(0, "parse error in feature string \"%s\"", *f);
		return 1;
	}

	/* Add 1 digit and 1 space */
	l = strlen(e) + strlen(n) + 2;

	c++;
	/* Check if we need more digits for feature count */
	for (d = c; d >= 10; d /= 10)
		l++;

	t = MALLOC(l + 1);
	if (!t)
		return 1;

	/* e: old feature string with leading space, or "" */
	if (*e == ' ')
		while (*(e + 1) == ' ')
			e++;

	snprintf(t, l + 1, "%0d%s %s", c, e, n);

	FREE(*f);
	*f = t;

	return 0;
}

int remove_feature(char **f, const char *o)
{
	int c = 0, d, l;
	char *e, *p, *n;
	const char *q;

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
	q = o + strlen(o);
	while (*q == ' ')
		q--;
	d = (int)(q - o);

	/* Update feature count */
	c--;
	q = o;
	while (q[0] != '\0') {
		if (q[0] == ' ' && q[1] != ' ' && q[1] != '\0')
			c--;
		q++;
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
	if (!p) {
		/* Internal error, feature string inconsistent */
		FREE(n);
		return 1;
	}
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
