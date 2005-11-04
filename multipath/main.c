/*
 * Soft:        multipath device mapper target autoconfig
 *
 * Version:     $Id: main.h,v 0.0.1 2003/09/18 15:13:38 cvaroqui Exp $
 *
 * Author:      Copyright (C) 2003 Christophe Varoqui
 *
 *              This program is distributed in the hope that it will be useful,
 *              but WITHOUT ANY WARRANTY; without even the implied warranty of
 *              MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *              See the GNU General Public License for more details.
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/file.h>
#include <errno.h>

#include <parser.h>
#include <vector.h>
#include <memory.h>
#include <libdevmapper.h>
#include <devmapper.h>
#include <checkers.h>
#include <path_state.h>
#include <blacklist.h>
#include <hwtable.h>
#include <util.h>
#include <defaults.h>
#include <structs.h>
#include <dmparser.h>
#include <cache.h>
#include <config.h>
#include <propsel.h>
#include <discovery.h>
#include <debug.h>
#include <switchgroup.h>
#include <sysfs/libsysfs.h>
#include <print.h>
#include <alias.h>

#include "main.h"
#include "pgpolicies.h"
#include "dict.h"

/* for column aligned output */
struct path_layout pl;

static char *
get_refwwid (vector pathvec)
{
	struct path * pp;
	char buff[FILE_NAME_SIZE];
	char * refwwid;

	if (conf->dev_type == DEV_NONE)
		return NULL;

	if (conf->dev_type == DEV_DEVNODE) {
		basename(conf->dev, buff);
		pp = find_path_by_dev(pathvec, buff);
		
		if (!pp) {
			pp = alloc_path();

			if (!pp)
				return NULL;

			strncpy(pp->dev, buff, FILE_NAME_SIZE);

			if (pathinfo(pp, conf->hwtable, DI_SYSFS | DI_WWID))
				return NULL;

			if (store_path(pathvec, pp)) {
				free_path(pp);
				return NULL;
			}
		}
		refwwid = pp->wwid;
		goto out;
	}

	if (conf->dev_type == DEV_DEVT) {
		pp = find_path_by_devt(pathvec, conf->dev);
		
		if (!pp) {
			if (devt2devname(buff, conf->dev))
				return NULL;

			pp = alloc_path();

			if (!pp)
				return NULL;

			strncpy(pp->dev, buff, FILE_NAME_SIZE);

			if (pathinfo(pp, conf->hwtable, DI_SYSFS | DI_WWID))
				return NULL;
			
			if (store_path(pathvec, pp)) {
				free_path(pp);
				return NULL;
			}
		}
		refwwid = pp->wwid;
		goto out;
	}
	if (conf->dev_type == DEV_DEVMAP) {
		/*
		 * may be a binding
		 */
		refwwid = get_user_friendly_wwid(conf->dev,
						 conf->bindings_file);

		if (refwwid)
			return refwwid;

		/*
		 * or may be an alias
		 */
		refwwid = get_mpe_wwid(conf->dev);

		/*
		 * or directly a wwid
		 */
		if (!refwwid)
			refwwid = conf->dev;
	}
out:
	if (refwwid && strlen(refwwid))
		return STRDUP(refwwid);

	return NULL;
}

static void
print_path (struct path * pp, char * style)
{
	char line[MAX_LINE_LEN];

	snprint_path(&line[0], MAX_LINE_LEN, style, pp, &pl);
	printf("%s", line);
}

static void
print_map (struct multipath * mpp)
{
	if (mpp->size && mpp->params)
		printf("0 %llu %s %s\n",
			 mpp->size, DEFAULT_TARGET, mpp->params);
	return;
}

static void
print_all_paths (vector pathvec, int banner)
{
	int i;
	struct path * pp;
	char line[MAX_LINE_LEN];

	if (!VECTOR_SIZE(pathvec)) {
		if (banner)
			fprintf(stdout, "===== no paths =====\n");
		return;
	}
	
	if (banner)
		fprintf(stdout, "===== paths list =====\n");

	get_path_layout(&pl, pathvec);
	snprint_path_header(line, MAX_LINE_LEN, PRINT_PATH_LONG, &pl);
	fprintf(stdout, "%s", line);

	vector_foreach_slot (pathvec, pp, i)
		print_path(pp, PRINT_PATH_LONG);
}

static void
print_mp (struct multipath * mpp)
{
	int j, i;
	struct path * pp = NULL;
	struct pathgroup * pgp = NULL;

	if (mpp->action == ACT_NOTHING || !conf->verbosity || !mpp->size)
		return;

	if (conf->verbosity > 1) {
		switch (mpp->action) {
		case ACT_RELOAD:
			printf("%s: ", ACT_RELOAD_STR);
			break;

		case ACT_CREATE:
			printf("%s: ", ACT_CREATE_STR);
			break;

		case ACT_SWITCHPG:
			printf("%s: ", ACT_SWITCHPG_STR);
			break;

		default:
			break;
		}
	}

	if (mpp->alias)
		printf("%s", mpp->alias);

	if (conf->verbosity == 1) {
		printf("\n");
		return;
	}
	if (strncmp(mpp->alias, mpp->wwid, WWID_SIZE))
		printf(" (%s)", mpp->wwid);

	printf("\n");

	if (mpp->size < (1 << 11))
		printf("[size=%llu kB]", mpp->size >> 1);
	else if (mpp->size < (1 << 21))
		printf("[size=%llu MB]", mpp->size >> 11);
	else if (mpp->size < (1 << 31))
		printf("[size=%llu GB]", mpp->size >> 21);
	else
		printf("[size=%llu TB]", mpp->size >> 31);

	if (mpp->features)
		printf("[features=\"%s\"]", mpp->features);

	if (mpp->hwhandler)
		printf("[hwhandler=\"%s\"]", mpp->hwhandler);

	fprintf(stdout, "\n");

	if (!mpp->pg)
		return;

	vector_foreach_slot (mpp->pg, pgp, j) {
		printf("\\_ ");

		if (mpp->selector) {
			printf("%s ", mpp->selector);
#if 0
			/* align to path status info */
			for (i = pl.hbtl_len + pl.dev_len + pl.dev_t_len + 4;
			     i > strlen(mpp->selector); i--)
				printf(" ");
#endif
		}
		if (pgp->priority)
			printf("[prio=%i]", pgp->priority);

		switch (pgp->status) {
		case PGSTATE_ENABLED:
			printf("[enabled]");
			break;
		case PGSTATE_DISABLED:
			printf("[disabled]");
			break;
		case PGSTATE_ACTIVE:
			printf("[active]");
			break;
		default:
			break;
		}
		printf("\n");

		vector_foreach_slot (pgp->paths, pp, i)
			print_path(pp, PRINT_PATH_INDENT);
	}
	printf("\n");
}

static int
filter_pathvec (vector pathvec, char * refwwid)
{
	int i;
	struct path * pp;

	if (!refwwid || !strlen(refwwid))
		return 0;

	vector_foreach_slot (pathvec, pp, i) {
		if (strncmp(pp->wwid, refwwid, WWID_SIZE) != 0) {
			condlog(3, "skip path %s : out of scope", pp->dev);
			free_path(pp);
			vector_del_slot(pathvec, i);
			i--;
		}
	}
	return 0;
}

/*
 * Transforms the path group vector into a proper device map string
 */
int
assemble_map (struct multipath * mp)
{
	int i, j;
	int shift, freechar;
	int minio;
	char * p;
	struct pathgroup * pgp;
	struct path * pp;

	p = mp->params;
	freechar = sizeof(mp->params);
	
	shift = snprintf(p, freechar, "%s %s %i %i",
			 mp->features, mp->hwhandler,
			 VECTOR_SIZE(mp->pg), mp->bestpg);

	if (shift >= freechar) {
		fprintf(stderr, "mp->params too small\n");
		return 1;
	}
	p += shift;
	freechar -= shift;
	
	vector_foreach_slot (mp->pg, pgp, i) {
		pgp = VECTOR_SLOT(mp->pg, i);
		shift = snprintf(p, freechar, " %s %i 1", mp->selector,
				 VECTOR_SIZE(pgp->paths));
		if (shift >= freechar) {
			fprintf(stderr, "mp->params too small\n");
			return 1;
		}
		p += shift;
		freechar -= shift;

		vector_foreach_slot (pgp->paths, pp, j) {
			minio = conf->minio;
			
			if (mp->rr_weight == RR_WEIGHT_PRIO && pp->priority)
				minio *= pp->priority;

			shift = snprintf(p, freechar, " %s %d",
					 pp->dev_t, minio);
			if (shift >= freechar) {
				fprintf(stderr, "mp->params too small\n");
				return 1;
			}
			p += shift;
			freechar -= shift;
		}
	}
	if (freechar < 1) {
		fprintf(stderr, "mp->params too small\n");
		return 1;
	}
	snprintf(p, 1, "\n");

	if (conf->verbosity > 2)
		print_map(mp);

	return 0;
}

static int
setup_map (struct multipath * mpp)
{
	/*
	 * don't bother if devmap size is unknown
	 */
	if (mpp->size <= 0) {
		condlog(3, "%s devmap size is unknown", mpp->alias);
		return 1;
	}

	/*
	 * properties selectors
	 */
	select_pgpolicy(mpp);
	select_selector(mpp);
	select_features(mpp);
	select_hwhandler(mpp);
	select_rr_weight(mpp);
	select_no_path_retry(mpp);

	/*
	 * apply selected grouping policy to valid paths
	 */
	switch (mpp->pgpolicy) {
	case MULTIBUS:
		one_group(mpp);
		break;
	case FAILOVER:
		one_path_per_group(mpp);
		break;
	case GROUP_BY_SERIAL:
		group_by_serial(mpp);
		break;
	case GROUP_BY_PRIO:
		group_by_prio(mpp);
		break;
	case GROUP_BY_NODE_NAME:
		group_by_node_name(mpp);
		break;
	default:
		break;
	}

	if (mpp->pg == NULL) {
		condlog(3, "pgpolicy failed to produce a pg vector");
		return 1;
	}

	/*
	 * ponders each path group and determine highest prio pg
	 * to switch over (default to first)
	 */
	mpp->bestpg = select_path_group(mpp);

	/*
	 * transform the mp->pg vector of vectors of paths
	 * into a mp->params strings to feed the device-mapper
	 */
	if (assemble_map(mpp)) {
		condlog(3, "problem assembing map");
		return 1;
	}
	return 0;
}

static int
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

static void
compute_pgid(struct pathgroup * pgp)
{
	struct path * pp;
	int i;

	vector_foreach_slot (pgp->paths, pp, i)
		pgp->id ^= (long)pp;
}

static int
pgcmp (struct multipath * mpp, struct multipath * cmpp)
{
	int i, j;
	struct pathgroup * pgp;
	struct pathgroup * cpgp;
	int r = 0;

	vector_foreach_slot (mpp->pg, pgp, i) {
		compute_pgid(pgp);

		vector_foreach_slot (cmpp->pg, cpgp, j) {
			if (pgp->id == cpgp->id) {
				r = 0;
				break;
			}
			r++;
		}
		if (r)
			return r;
	}
	return r;
}

static void
select_action (struct multipath * mpp, vector curmp)
{
	struct multipath * cmpp;

	cmpp = find_mp_by_alias(curmp, mpp->alias);

	if (!cmpp) {
		cmpp = find_mp_by_wwid(curmp, mpp->wwid);

		if (cmpp && !conf->dry_run) {
			condlog(2, "remove: %s (dup of %s)",
				cmpp->alias, mpp->alias);
			dm_flush_map(cmpp->alias, DEFAULT_TARGET);
		}
		mpp->action = ACT_CREATE;
		condlog(3, "set ACT_CREATE: map does not exists");
		return;
	}

	if (!find_mp_by_wwid(curmp, mpp->wwid)) {
		condlog(2, "remove: %s (wwid changed)", cmpp->alias);
		dm_flush_map(mpp->alias, NULL);
		strncat(cmpp->wwid, mpp->wwid, WWID_SIZE);
		drop_multipath(curmp, cmpp->wwid, KEEP_PATHS);
		mpp->action = ACT_CREATE;
		condlog(3, "set ACT_CREATE: map wwid change");
		return;
	}
		
	if (pathcount(mpp, PATH_UP) == 0) {
		mpp->action = ACT_NOTHING;
		condlog(3, "set ACT_NOTHING: no usable path");
		return;
	}
	if (cmpp->size != mpp->size) {
		mpp->action = ACT_RELOAD;
		condlog(3, "set ACT_RELOAD: size change");
		return;
	}
	if (!mpp->no_path_retry && /* let features be handled by the daemon */
	    strncmp(cmpp->features, mpp->features, strlen(mpp->features))) {
		mpp->action =  ACT_RELOAD;
		condlog(3, "set ACT_RELOAD: features change");
		return;
	}
	if (strncmp(cmpp->hwhandler, mpp->hwhandler,
		    strlen(mpp->hwhandler))) {
		mpp->action = ACT_RELOAD;
		condlog(3, "set ACT_RELOAD: hwhandler change");
		return;
	}
	if (strncmp(cmpp->selector, mpp->selector,
		    strlen(mpp->selector))) {
		mpp->action = ACT_RELOAD;
		condlog(3, "set ACT_RELOAD: selector change");
		return;
	}
	if (VECTOR_SIZE(cmpp->pg) != VECTOR_SIZE(mpp->pg)) {
		mpp->action = ACT_RELOAD;
		condlog(3, "set ACT_RELOAD: number of path group change");
		return;
	}
	if (pgcmp(mpp, cmpp)) {
		mpp->action = ACT_RELOAD;
		condlog(3, "set ACT_RELOAD: path group topology change");
		return;
	}
	if (cmpp->nextpg != mpp->bestpg) {
		mpp->action = ACT_SWITCHPG;
		condlog(3, "set ACT_SWITCHPG: next path group change");
		return;
	}
	mpp->action = ACT_NOTHING;
	condlog(3, "set ACT_NOTHING: map unchanged");
	return;
}

static int
reinstate_paths (struct multipath * mpp)
{
	int i, j;
	struct pathgroup * pgp;
	struct path * pp;

	if (!mpp->pg)
		return 0;

	vector_foreach_slot (mpp->pg, pgp, i) {
		if (!pgp->paths)
			continue;

		vector_foreach_slot (pgp->paths, pp, j) {
			if (pp->state != PATH_UP &&
			    (pgp->status == PGSTATE_DISABLED ||
			     pgp->status == PGSTATE_ACTIVE))
				continue;

			if (pp->dmstate == PSTATE_FAILED) {
				if (dm_reinstate_path(mpp->alias, pp->dev_t))
					condlog(0, "error reinstating %s",
						pp->dev);
			}
		}
	}
	return 0;
}

int lock_multipath (struct multipath * mpp, int lock)
{
	struct pathgroup * pgp;
	struct path * pp;
	int i, j;

	if (!mpp || !mpp->pg)
		return 0;
	
	vector_foreach_slot (mpp->pg, pgp, i) {
		if (!pgp->paths)
			continue;
		vector_foreach_slot(pgp->paths, pp, j) {
			if (lock && flock(pp->fd, LOCK_EX | LOCK_NB) &&
			    errno == EWOULDBLOCK)
				return 1;
			else if (!lock)
				flock(pp->fd, LOCK_UN);
		}
	}
	return 0;
}

/*
 * Return value:
 *  -1: Retry
 *   0: DM_DEVICE_CREATE or DM_DEVICE_RELOAD failed, or dry_run mode.
 *   1: DM_DEVICE_CREATE or DM_DEVICE_RELOAD succeeded.
 *   2: Map is already existing.
 */
static int
domap (struct multipath * mpp)
{
	int r = 0;

	/*
	 * last chance to quit before touching the devmaps
	 */
	if (conf->dry_run)
		return 0;

	switch (mpp->action) {
	case ACT_NOTHING:
		return 2;

	case ACT_SWITCHPG:
		dm_switchgroup(mpp->alias, mpp->bestpg);
		/*
		 * we may have avoided reinstating paths because there where in
		 * active or disabled PG. Now that the topology has changed,
		 * retry.
		 */
		reinstate_paths(mpp);
		return 2;

	case ACT_CREATE:
		if (lock_multipath(mpp, 1)) {
			condlog(3, "%s: in use", mpp->alias);
			return -1;
		}
		dm_shut_log();

		if (dm_map_present(mpp->alias))
			break;

		r = dm_addmap(DM_DEVICE_CREATE, mpp->alias, DEFAULT_TARGET,
			      mpp->params, mpp->size, mpp->wwid);

		/*
		 * DM_DEVICE_CREATE is actually DM_DEV_CREATE plus
		 * DM_TABLE_LOAD. Failing the second part leaves an
		 * empty map. Clean it up.
		 */
		if (!r && dm_map_present(mpp->alias)) {
			condlog(3, "%s: failed to load map "
				   "(a path might be in use)",
				   mpp->alias);
			dm_flush_map(mpp->alias, NULL);
		}

		lock_multipath(mpp, 0);
		dm_restore_log();
		break;

	case ACT_RELOAD:
		r = (dm_addmap(DM_DEVICE_RELOAD, mpp->alias, DEFAULT_TARGET,
			      mpp->params, mpp->size, NULL) &&
		     dm_simplecmd(DM_DEVICE_RESUME, mpp->alias));
		break;

	default:
		break;
	}

	if (r) {
		/*
		 * DM_DEVICE_CREATE or DM_DEVICE_RELOAD succeeded
		 */
		dm_switchgroup(mpp->alias, mpp->bestpg);
		print_mp(mpp);
	}

	return r;
}

static int
deadmap (struct multipath * mpp)
{
	int i, j;
	struct pathgroup * pgp;
	struct path * pp;

	if (!mpp->pg)
		return 1;

	vector_foreach_slot (mpp->pg, pgp, i) {
		if (!pgp->paths)
			continue;

		vector_foreach_slot (pgp->paths, pp, j)
			if (strlen(pp->dev))
				return 0; /* alive */
	}
	
	return 1; /* dead */
}

static int
coalesce_paths (vector curmp, vector pathvec)
{
	int r = 1;
	int k, i;
	char empty_buff[WWID_SIZE];
	struct multipath * mpp;
	struct path * pp1;
	struct path * pp2;

	memset(empty_buff, 0, WWID_SIZE);

	vector_foreach_slot (pathvec, pp1, k) {
		/* skip this path for some reason */

		/* 1. if path has no unique id or wwid blacklisted */
		if (memcmp(empty_buff, pp1->wwid, WWID_SIZE) == 0 ||
		    blacklist(conf->blist, pp1->wwid))
			continue;

		/* 2. if path already coalesced */
		if (pp1->mpp)
			continue;

		/*
		 * at this point, we know we really got a new mp
		 */
		mpp = alloc_multipath();

		if (!mpp)
			return 1;

		mpp->mpe = find_mpe(pp1->wwid);
		mpp->hwe = pp1->hwe;
		strcpy(mpp->wwid, pp1->wwid);
		select_alias(mpp);

		pp1->mpp = mpp;
		mpp->size = pp1->size;
		mpp->paths = vector_alloc();

		if (pp1->priority < 0)
			mpp->action = ACT_NOTHING;

		if (!mpp->paths)
			return 1;
		
		if (store_path(mpp->paths, pp1))
			return 1;

		for (i = k + 1; i < VECTOR_SIZE(pathvec); i++) {
			pp2 = VECTOR_SLOT(pathvec, i);

			if (strcmp(pp1->wwid, pp2->wwid))
				continue;
			
			pp2->mpp = mpp;

			if (pp2->size != mpp->size) {
				/*
				 * ouch, avoid feeding that to the DM
				 */
				condlog(3, "path size mismatch : discard %s",
				     mpp->wwid);
				mpp->action = ACT_NOTHING;
			}
			if (pp2->priority < 0)
				mpp->action = ACT_NOTHING;

			if (store_path(mpp->paths, pp2))
				return 1;
		}
		if (setup_map(mpp))
			goto next;

		if (mpp->action == ACT_UNDEF)
			select_action(mpp, curmp);

		r = domap(mpp);

		if (r < 0)
			return r;

		if (r && mpp->no_path_retry != NO_PATH_RETRY_UNDEF) {
			if (mpp->no_path_retry == NO_PATH_RETRY_FAIL)
				dm_queue_if_no_path(mpp->alias, 0);
			else
				dm_queue_if_no_path(mpp->alias, 1);
		}

next:
		drop_multipath(curmp, mpp->wwid, KEEP_PATHS);
		free_multipath(mpp, KEEP_PATHS);
	}
	/*
	 * Flush maps with only dead paths (ie not in sysfs)
	 * Keep maps with only failed paths
	 */
	vector_foreach_slot (curmp, mpp, i) {
		if (!deadmap(mpp))
			continue;

		if (dm_flush_map(mpp->alias, DEFAULT_TARGET))
			condlog(2, "remove: %s (dead) failed!",
				mpp->alias);
		else
			condlog(2, "remove: %s (dead)", mpp->alias);
	}
	return 0;
}

static void
usage (char * progname)
{
	fprintf (stderr, VERSION_STRING);
	fprintf (stderr, "Usage: %s\t[-v level] [-d] [-l|-ll|-f|-F]\n",
		progname);
	fprintf (stderr,
		"\t\t\t[-p failover|multibus|group_by_serial|group_by_prio]\n" \
		"\t\t\t[device]\n" \
		"\n" \
		"\t-v level\tverbosity level\n" \
		"\t   0\t\t\tno output\n" \
		"\t   1\t\t\tprint created devmap names only\n" \
		"\t   2\t\t\tdefault verbosity\n" \
		"\t   3\t\t\tprint debug information\n" \
		"\t-b file\t\tbindings file location\n" \
		"\t-d\t\tdry run, do not create or update devmaps\n" \
		"\t-l\t\tshow multipath topology (sysfs and DM info)\n" \
		"\t-ll\t\tshow multipath topology (maximum info)\n" \
		"\t-f\t\tflush a multipath device map\n" \
		"\t-F\t\tflush all multipath device maps\n" \
		"\t-p policy\tforce all maps to specified policy :\n" \
		"\t   failover\t\t1 path per priority group\n" \
		"\t   multibus\t\tall paths in 1 priority group\n" \
		"\t   group_by_serial\t1 priority group per serial\n" \
		"\t   group_by_prio\t1 priority group per priority lvl\n" \
		"\t   group_by_node_name\t1 priority group per target node\n" \
		"\n" \
		"\tdevice\t\tlimit scope to the device's multipath\n" \
		"\t\t\t(udev-style $DEVNAME reference, eg /dev/sdb\n" \
		"\t\t\tor major:minor or a device map name)\n" \
		);

	exit(1);
}

static int
update_paths (struct multipath * mpp)
{
	int i, j;
	struct pathgroup * pgp;
	struct path * pp;

	if (!mpp->pg)
		return 0;

	vector_foreach_slot (mpp->pg, pgp, i) {
		if (!pgp->paths)
			continue;

		vector_foreach_slot (pgp->paths, pp, j) {
			if (!strlen(pp->dev)) {
				if (devt2devname(pp->dev, pp->dev_t)) {
					/*
					 * path is not in sysfs anymore
					 */
					pp->state = PATH_DOWN;
					continue;
				}
				pathinfo(pp, conf->hwtable,
					 DI_SYSFS | DI_CHECKER | \
					 DI_SERIAL | DI_PRIO);
				continue;
			}
			if (pp->state == PATH_UNCHECKED)
				pathinfo(pp, conf->hwtable, DI_CHECKER);

			if (!pp->priority)
				pathinfo(pp, conf->hwtable, DI_PRIO);
		}
	}
	return 0;
}

static int
get_dm_mpvec (vector curmp, vector pathvec, char * refwwid)
{
	int i;
	struct multipath * mpp;

	if (dm_get_maps(curmp, DEFAULT_TARGET))
		return 1;

	vector_foreach_slot (curmp, mpp, i) {
		/*
		 * discard out of scope maps
		 */
		if (mpp->wwid && refwwid &&
		    strncmp(mpp->wwid, refwwid, WWID_SIZE)) {
			condlog(3, "skip map %s: out of scope", mpp->alias);
			free_multipath(mpp, KEEP_PATHS);
			vector_del_slot(curmp, i);
			i--;
			continue;
		}

		condlog(3, "params = %s", mpp->params);
		condlog(3, "status = %s", mpp->status);

		disassemble_map(pathvec, mpp->params, mpp);

		/*
		 * disassemble_map() can add new paths to pathvec.
		 * If not in "fast list mode", we need to fetch information
		 * about them
		 */
		if (conf->list != 1)
			update_paths(mpp);

		if (conf->list > 1)
			mpp->bestpg = select_path_group(mpp);

		disassemble_status(mpp->status, mpp);

		if (conf->list)
			print_mp(mpp);

		if (!conf->dry_run)
			reinstate_paths(mpp);
	}
	return 0;
}


/*
 * Return value:
 *  -1: Retry
 *   0: Success
 *   1: Failure
 */
static int
configure (void)
{
	vector curmp = NULL;
	vector pathvec = NULL;
	int r = 1;
	int di_flag = 0;
	char * refwwid = NULL;
	char * dev = NULL;

	/*
	 * allocate core vectors to store paths and multipaths
	 */
	curmp = vector_alloc();
	pathvec = vector_alloc();

	if (!curmp || !pathvec) {
		condlog(0, "can not allocate memory");
		goto out;
	}

	/*
	 * if we have a blacklisted device parameter, exit early
	 */
	if (conf->dev) {
		if (!strncmp(conf->dev, "/dev/", 5) &&
		    strlen(conf->dev) > 5)
			dev = conf->dev + 5;
		else
			dev = conf->dev;
	}
	
	if (dev && blacklist(conf->blist, dev))
		goto out;
	
	condlog(3, "load path identifiers cache");
	cache_load(pathvec);

	if (conf->verbosity > 2)
		print_all_paths(pathvec, 1);

	/*
	 * scope limiting must be translated into a wwid
	 * failing the translation is fatal (by policy)
	 */
	if (conf->dev) {
		refwwid = get_refwwid(pathvec);

		if (!refwwid) {
			condlog(3, "scope is nul");
			goto out;
		}
		condlog(3, "scope limited to %s", refwwid);
	}

	/*
	 * get a path list
	 */
	if (conf->dev)
		di_flag = DI_WWID;

	if (conf->list > 1)
		/* extended path info '-ll' */
		di_flag |= DI_SYSFS | DI_CHECKER;
	else if (conf->list)
		/* minimum path info '-l' */
		di_flag |= DI_SYSFS;
	else
		/* maximum info */
		di_flag = DI_ALL;

	if (path_discovery(pathvec, conf, di_flag))
		goto out;

	if (conf->verbosity > 2)
		print_all_paths(pathvec, 1);

	get_path_layout(&pl, pathvec);

	if (get_dm_mpvec(curmp, pathvec, refwwid))
		goto out;

	filter_pathvec(pathvec, refwwid);

	if (conf->list)
		goto out;

	/*
	 * core logic entry point
	 */
	r = coalesce_paths(curmp, pathvec);

out:
	if (refwwid)
		FREE(refwwid);

	free_multipathvec(curmp, KEEP_PATHS);
	free_pathvec(pathvec, FREE_PATHS);

	return r;
}

int
main (int argc, char *argv[])
{
	int arg;
	extern char *optarg;
	extern int optind;
	int i, r;

	if (getuid() != 0) {
		fprintf(stderr, "need to be root\n");
		exit(1);
	}

	if (dm_prereq(DEFAULT_TARGET, 1, 0, 3))
		exit(1);

	if (sysfs_get_mnt_path(sysfs_path, FILE_NAME_SIZE)) {
		condlog(0, "multipath tools need sysfs mounted");
		exit(1);
	}
	if (load_config(DEFAULT_CONFIGFILE))
		exit(1);

	while ((arg = getopt(argc, argv, ":qdl::Ffi:M:v:p:b:")) != EOF ) {
		switch(arg) {
		case 1: printf("optarg : %s\n",optarg);
			break;
		case 'v':
			if (sizeof(optarg) > sizeof(char *) ||
			    !isdigit(optarg[0]))
				usage (argv[0]);

			conf->verbosity = atoi(optarg);
			break;
		case 'b':
			conf->bindings_file = optarg;
			break;
		case 'd':
			conf->dry_run = 1;
			break;
		case 'f':
			conf->remove = FLUSH_ONE;
			break;
		case 'F':
			conf->remove = FLUSH_ALL;
			break;
		case 'l':
			conf->list = 1;
			conf->dry_run = 1;

			if (optarg && !strncmp(optarg, "l", 1))
				conf->list++;

			break;
		case 'M':
#if _DEBUG_
			debug = atoi(optarg);
#endif
			break;
		case 'p':
			conf->pgpolicy_flag = get_pgpolicy_id(optarg);
			if (conf->pgpolicy_flag == -1) {
				printf("'%s' is not a valid policy\n", optarg);
				usage(argv[0]);
			}                
			break;
		case ':':
			fprintf(stderr, "Missing option arguement\n");
			usage(argv[0]);        
		case '?':
			fprintf(stderr, "Unknown switch: %s\n", optarg);
			usage(argv[0]);
		default:
			usage(argv[0]);
		}
	}        
	if (optind < argc) {
		conf->dev = MALLOC(FILE_NAME_SIZE);

		if (!conf->dev)
			goto out;

		strncpy(conf->dev, argv[optind], FILE_NAME_SIZE);

		if (filepresent(conf->dev))
			conf->dev_type = DEV_DEVNODE;
		else if (sscanf(conf->dev, "%d:%d", &i, &i) == 2)
			conf->dev_type = DEV_DEVT;
		else
			conf->dev_type = DEV_DEVMAP;

	}

	if (conf->remove == FLUSH_ONE) {
		if (conf->dev_type == DEV_DEVMAP)
			dm_flush_map(conf->dev, DEFAULT_TARGET);
		else
			condlog(0, "must provide a map name to remove");

		goto out;
	}
	else if (conf->remove == FLUSH_ALL) {
		dm_flush_maps(DEFAULT_TARGET);
		goto out;
	}
	while ((r = configure()) < 0)
		condlog(3, "restart multipath configuration process");
	
out:
	free_config(conf);
	dm_lib_release();
	dm_lib_exit();
#ifdef _DEBUG_
	dbg_free_final(NULL);
#endif
	return r;
}
