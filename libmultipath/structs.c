/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 * Copyright (c) 2004 Stefan Bader, IBM
 */
#include <stdio.h>
#include <unistd.h>
#include <libdevmapper.h>
#include "mt-udev-wrap.h"
#include <ctype.h>

#include "checkers.h"
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
#include "devmapper.h"

const char * const protocol_name[LAST_BUS_PROTOCOL_ID + 1] = {
	[SYSFS_BUS_UNDEF] = "undef",
	[SYSFS_BUS_CCW] = "ccw",
	[SYSFS_BUS_CCISS] = "cciss",
	[SYSFS_BUS_SCSI + SCSI_PROTOCOL_FCP] = "scsi:fcp",
	[SYSFS_BUS_SCSI + SCSI_PROTOCOL_SPI] = "scsi:spi",
	[SYSFS_BUS_SCSI + SCSI_PROTOCOL_SSA] = "scsi:ssa",
	[SYSFS_BUS_SCSI + SCSI_PROTOCOL_SBP] = "scsi:sbp",
	[SYSFS_BUS_SCSI + SCSI_PROTOCOL_SRP] = "scsi:srp",
	[SYSFS_BUS_SCSI + SCSI_PROTOCOL_ISCSI] = "scsi:iscsi",
	[SYSFS_BUS_SCSI + SCSI_PROTOCOL_SAS] = "scsi:sas",
	[SYSFS_BUS_SCSI + SCSI_PROTOCOL_ADT] = "scsi:adt",
	[SYSFS_BUS_SCSI + SCSI_PROTOCOL_ATA] = "scsi:ata",
	[SYSFS_BUS_SCSI + SCSI_PROTOCOL_USB] = "scsi:usb",
	[SYSFS_BUS_SCSI + SCSI_PROTOCOL_UNSPEC] = "scsi:unspec",
	[SYSFS_BUS_NVME + NVME_PROTOCOL_PCIE] = "nvme:pcie",
	[SYSFS_BUS_NVME + NVME_PROTOCOL_RDMA] = "nvme:rdma",
	[SYSFS_BUS_NVME + NVME_PROTOCOL_FC] = "nvme:fc",
	[SYSFS_BUS_NVME + NVME_PROTOCOL_TCP] = "nvme:tcp",
	[SYSFS_BUS_NVME + NVME_PROTOCOL_LOOP] = "nvme:loop",
	[SYSFS_BUS_NVME + NVME_PROTOCOL_APPLE_NVME] = "nvme:apple-nvme",
	[SYSFS_BUS_NVME + NVME_PROTOCOL_UNSPEC] = "nvme:unspec",
};

struct adapter_group *
alloc_adaptergroup(void)
{
	struct adapter_group *agp;

	agp = (struct adapter_group *)calloc(1, sizeof(struct adapter_group));

	if (!agp)
		return NULL;

	agp->host_groups = vector_alloc();
	if (!agp->host_groups) {
		free(agp);
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
		free(agp);
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
		free(hgp);
	}
	vector_free(hostgroups);
}

struct host_group *
alloc_hostgroup(void)
{
	struct host_group *hgp;

	hgp = (struct host_group *)calloc(1, sizeof(struct host_group));

	if (!hgp)
		return NULL;

	hgp->paths = vector_alloc();

	if (!hgp->paths) {
		free(hgp);
		hgp = NULL;
	}
	return hgp;
}

struct path *
alloc_path (void)
{
	struct path * pp;

	pp = (struct path *)calloc(1, sizeof(struct path));

	if (pp) {
		pp->initialized = INIT_NEW;
		pp->sg_id.host_no = -1;
		pp->sg_id.channel = -1;
		pp->sg_id.scsi_id = -1;
		pp->sg_id.lun = SCSI_INVALID_LUN;
		pp->sg_id.proto_id = PROTOCOL_UNSET;
		pp->fd = -1;
		pp->tpgs = TPGS_UNDEF;
		pp->tpg_id = GROUP_ID_UNDEF;
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
uninitialize_path(struct path *pp)
{
	if (!pp)
		return;

	pp->dmstate = PSTATE_UNDEF;
	pp->state = PATH_UNCHECKED;
	pp->uid_attribute = NULL;
	pp->checker_timeout = 0;
	pp->pending_ticks = 0;

	if (checker_selected(&pp->checker))
		checker_put(&pp->checker);

	if (prio_selected(&pp->prio))
		prio_put(&pp->prio);

	if (pp->fd >= 0) {
		close(pp->fd);
		pp->fd = -1;
	}
}

void
free_path (struct path * pp)
{
	if (!pp)
		return;

	uninitialize_path(pp);

	if (pp->udev) {
		udev_device_unref(pp->udev);
		pp->udev = NULL;
	}
	if (pp->vpd_data)
		free(pp->vpd_data);

	vector_free(pp->hwe);

	free(pp);
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

void cleanup_pathvec_and_free_paths(vector *vec)
{
	free_pathvec(*vec, FREE_PATHS);
}

struct pathgroup *
alloc_pathgroup (void)
{
	struct pathgroup * pgp;

	pgp = (struct pathgroup *)calloc(1, sizeof(struct pathgroup));

	if (!pgp)
		return NULL;

	pgp->paths = vector_alloc();

	if (!pgp->paths) {
		free(pgp);
		return NULL;
	}

	dm_pathgroup_to_gen(pgp)->ops = &dm_gen_pathgroup_ops;
	return pgp;
}

void free_pathgroup(struct pathgroup *pgp)
{
	if (!pgp)
		return;

	free_pathvec(pgp->paths, KEEP_PATHS);
	free(pgp);
}

void free_pgvec(vector pgvec)
{
	int i;
	struct pathgroup * pgp;

	if (!pgvec)
		return;

	vector_foreach_slot (pgvec, pgp, i)
		free_pathgroup(pgp);

	vector_free(pgvec);
}

struct multipath *
alloc_multipath (void)
{
	struct multipath * mpp;

	mpp = (struct multipath *)calloc(1, sizeof(struct multipath));

	if (mpp) {
		mpp->bestpg = 1;
		mpp->mpcontext = NULL;
		mpp->no_path_retry = NO_PATH_RETRY_UNDEF;
		dm_multipath_to_gen(mpp)->ops = &dm_gen_multipath_ops;
	}
	return mpp;
}

void *set_mpp_hwe(struct multipath *mpp, const struct path *pp)
{
	if (!mpp || !pp || !pp->hwe)
		return NULL;
	if (mpp->hwe)
		return mpp->hwe;
	mpp->hwe = vector_convert(NULL, pp->hwe,
				  struct hwentry, identity);
	return mpp->hwe;
}

void free_multipath_attributes(struct multipath *mpp)
{
	if (!mpp)
		return;

	if (mpp->selector) {
		free(mpp->selector);
		mpp->selector = NULL;
	}

	if (mpp->features) {
		free(mpp->features);
		mpp->features = NULL;
	}

	if (mpp->hwhandler) {
		free(mpp->hwhandler);
		mpp->hwhandler = NULL;
	}
}

void free_multipath(struct multipath *mpp)
{
	struct pathgroup *pg;
	struct path *pp;
	int i, j;

	if (!mpp)
		return;

	free_multipath_attributes(mpp);

	if (mpp->alias) {
		free(mpp->alias);
		mpp->alias = NULL;
	}
	vector_foreach_slot (mpp->pg, pg, i) {
		vector_foreach_slot (pg->paths, pp, j)
			if (pp->mpp == mpp)
				pp->mpp = NULL;
	}
	vector_foreach_slot (mpp->paths, pp, i)
		if (pp->mpp == mpp)
			pp->mpp = NULL;
	free_pathvec(mpp->paths, KEEP_PATHS);
	free_pgvec(mpp->pg);
	mpp->paths = mpp->pg = NULL;
	if (mpp->hwe) {
		vector_free(mpp->hwe);
		mpp->hwe = NULL;
	}
	free(mpp->mpcontext);
	free(mpp);
}

void cleanup_multipath(struct multipath **pmpp)
{
	if (*pmpp)
		free_multipath(*pmpp);
}

void free_multipathvec(vector mpvec)
{
	int i;
	struct multipath * mpp;

	if (!mpvec)
		return;

	vector_foreach_slot (mpvec, mpp, i)
		free_multipath(mpp);

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
		condlog(3, "%s: Empty device name", pp->dev_t);
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
find_mp_by_minor (const struct vector_s *mpvec, unsigned int minor)
{
	int i;
	struct multipath * mpp;

	if (!mpvec)
		return NULL;

	vector_foreach_slot (mpvec, mpp, i) {
		if (!has_dm_info(mpp))
			continue;

		if (mpp->dmi.minor == minor)
			return mpp;
	}
	return NULL;
}

struct multipath *
find_mp_by_wwid (const struct vector_s *mpvec, const char * wwid)
{
	int i;
	struct multipath * mpp;

	if (!mpvec || strlen(wwid) >= WWID_SIZE)
		return NULL;

	vector_foreach_slot (mpvec, mpp, i)
		if (!strncmp(mpp->wwid, wwid, WWID_SIZE))
			return mpp;

	return NULL;
}

struct multipath *
find_mp_by_alias (const struct vector_s *mpvec, const char * alias)
{
	int i;
	size_t len;
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
find_mp_by_str (const struct vector_s *mpvec, const char * str)
{
	int minor;
	char dummy;
	struct multipath *mpp = NULL;

	if (sscanf(str, "dm-%d%c", &minor, &dummy) == 1)
		mpp = find_mp_by_minor(mpvec, minor);
	if (!mpp)
		mpp = find_mp_by_alias(mpvec, str);
	if (!mpp)
		mpp = find_mp_by_wwid(mpvec, str);

	if (!mpp)
		condlog(2, "%s: invalid map name.", str);
	return mpp;
}

struct path *
find_path_by_dev (const struct vector_s *pathvec, const char *dev)
{
	int i;
	struct path * pp;

	if (!pathvec || !dev)
		return NULL;

	vector_foreach_slot (pathvec, pp, i)
		if (!strcmp(pp->dev, dev))
			return pp;

	condlog(4, "%s: dev not found in pathvec", dev);
	return NULL;
}

struct path *
find_path_by_devt (const struct vector_s *pathvec, const char * dev_t)
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

struct path *mp_find_path_by_devt(const struct multipath *mpp, const char *devt)
{
	struct path *pp;
	struct pathgroup *pgp;
	unsigned int i, j;

	pp = find_path_by_devt(mpp->paths, devt);
	if (pp)
		return pp;

	vector_foreach_slot (mpp->pg, pgp, i){
		vector_foreach_slot (pgp->paths, pp, j){
			if (!strcmp(pp->dev_t, devt))
				return pp;
		}
	}
	return NULL;
}

static int do_pathcount(const struct multipath *mpp, const int *states,
			unsigned int nr_states)
{
	struct pathgroup *pgp;
	struct path *pp;
	int count = 0;
	unsigned int i, j, k;

	if (!mpp->pg || !nr_states)
		return count;

	vector_foreach_slot (mpp->pg, pgp, i) {
		vector_foreach_slot (pgp->paths, pp, j) {
			for (k = 0; k < nr_states; k++) {
				if (pp->state == states[k]) {
					count++;
					break;
				}
			}
		}
	}
	return count;
}

int pathcount(const struct multipath *mpp, int state)
{
	return do_pathcount(mpp, &state, 1);
}

int count_active_paths(const struct multipath *mpp)
{
	struct pathgroup *pgp;
	struct path *pp;
	int count = 0;
	int i, j;

	if (!mpp->pg)
		return 0;

	vector_foreach_slot (mpp->pg, pgp, i) {
		vector_foreach_slot (pgp->paths, pp, j) {
			if (pp->state == PATH_UP || pp->state == PATH_GHOST)
				count++;
		}
	}
	return count;
}

int count_active_pending_paths(const struct multipath *mpp)
{
	int states[] = {PATH_UP, PATH_GHOST, PATH_PENDING};

	return do_pathcount(mpp, states, 3);
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

int add_feature(char **features_p, const char *new_feat)
{
	int count = 0, new_count, len;
	char *tmp, *feats;
	const char *ptr;

	if (!features_p)
		return 1;

	/* Nothing to do */
	if (!new_feat || *new_feat == '\0')
		return 0;

	len = strlen(new_feat);
	if (isspace(*new_feat) || isspace(*(new_feat + len - 1))) {
		condlog(0, "internal error: feature \"%s\" has leading or trailing spaces",
			new_feat);
		return 1;
	}

	ptr = new_feat;
	new_count = 1;
	while (*ptr != '\0') {
		if (isspace(*ptr) && !isspace(*(ptr + 1)) && *(ptr + 1) != '\0')
			new_count++;
		ptr++;
	}

	/* default feature is null */
	if(!*features_p)
	{
		len = asprintf(&feats, "%0d %s", new_count, new_feat);
		if(len == -1)
			return 1;

		*features_p = feats;
		return 0;
	}

	/* Check if feature is already present */
	tmp = *features_p;
	while ((tmp = strstr(tmp, new_feat)) != NULL) {
		if (isspace(*(tmp - 1)) &&
		    (isspace(*(tmp + len)) || *(tmp + len) == '\0'))
			return 0;
		tmp += len;
	}

	/* Get feature count */
	count = strtoul(*features_p, &tmp, 10);
	if (*features_p == tmp || (!isspace(*tmp) && *tmp != '\0')) {
		condlog(0, "parse error in feature string \"%s\"", *features_p);
		return 1;
	}
	count += new_count;
	if (asprintf(&feats, "%0d%s %s", count, tmp, new_feat) < 0)
		return 1;

	free(*features_p);
	*features_p = feats;

	return 0;
}

int remove_feature(char **features_p, const char *old_feat)
{
	int count = 0, len;
	char *feats_start, *ptr, *new;

	if (!features_p || !*features_p)
		return 1;

	/* Nothing to do */
	if (!old_feat || *old_feat == '\0')
		return 0;

	len = strlen(old_feat);
	if (isspace(*old_feat) || isspace(*(old_feat + len - 1))) {
		condlog(0, "internal error: feature \"%s\" has leading or trailing spaces",
			old_feat);
		return 1;
	}

	/* Check if present and not part of a larger feature token*/
	ptr = *features_p + 1;
	while ((ptr = strstr(ptr, old_feat)) != NULL) {
		if (isspace(*(ptr - 1)) &&
		    (isspace(*(ptr + len)) || *(ptr + len) == '\0'))
			break;
		ptr += len;
	}
	if (!ptr)
		return 0;

	/* Get feature count */
	count = strtoul(*features_p, &feats_start, 10);
	if (*features_p == feats_start || !isspace(*feats_start)) {
		condlog(0, "parse error in feature string \"%s\"", *features_p);
		return 1;
	}

	/* Update feature count */
	count--;
	while (*old_feat != '\0') {
		if (isspace(*old_feat) && !isspace(*(old_feat + 1)) &&
		    *(old_feat + 1) != '\0')
			count--;
		old_feat++;
	}

	/* Quick exit if all features have been removed */
	if (count == 0) {
		new = malloc(2);
		if (!new)
			return 1;
		strcpy(new, "0");
		goto out;
	}

	/* Update feature count space */
	new = malloc(strlen(*features_p) - len + 1);
	if (!new)
		return 1;

	/* Copy the feature count */
	sprintf(new, "%0d", count);
	/*
	 * Copy existing features up to the feature
	 * about to be removed
	 */
	strncat(new, feats_start, (size_t)(ptr - feats_start));
	/* Skip feature to be removed */
	ptr += len;
	/* Copy remaining features */
	while (isspace(*ptr))
		ptr++;
	if (*ptr != '\0')
		strcat(new, ptr);
	else
		strchop(new);

out:
	free(*features_p);
	*features_p = new;

	return 0;
}

unsigned int bus_protocol_id(const struct path *pp) {
	if (!pp || pp->bus < 0 || pp->bus > SYSFS_BUS_NVME)
		return SYSFS_BUS_UNDEF;
	if (pp->bus != SYSFS_BUS_SCSI && pp->bus != SYSFS_BUS_NVME)
		return pp->bus;
	if (pp->sg_id.proto_id < 0)
		return SYSFS_BUS_UNDEF;
	if (pp->bus == SYSFS_BUS_SCSI &&
	    pp->sg_id.proto_id > SCSI_PROTOCOL_UNSPEC)
		return SYSFS_BUS_UNDEF;
	if (pp->bus == SYSFS_BUS_NVME &&
	    pp->sg_id.proto_id > NVME_PROTOCOL_UNSPEC)
		return SYSFS_BUS_UNDEF;
	return pp->bus + pp->sg_id.proto_id;
}
