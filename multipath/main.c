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

#include "main.h"
#include "pgpolicies.h"
#include "dict.h"

static char *
get_refwwid (vector pathvec)
{
	struct path * pp;
	char buff[FILE_NAME_SIZE];
	char * refwwid;

	if (conf->dev_type == DEV_NONE)
		return NULL;

	if (conf->dev_type == DEV_DEVNODE) {
		condlog(3, "limited scope = %s", conf->dev);
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

		refwwid = MALLOC(WWID_SIZE);

		if (!refwwid)
			return NULL;

		memcpy(refwwid, pp->wwid, WWID_SIZE);
		return refwwid;
	}

	if (conf->dev_type == DEV_DEVT) {
		condlog(3, "limited scope = %s", conf->dev);
		pp = find_path_by_devt(pathvec, conf->dev);
		
		if (!pp) {
			pp = alloc_path();

			if (!pp)
				return NULL;

			devt2devname(conf->dev, buff);

			if(safe_sprintf(pp->dev, "%s", buff)) {
				fprintf(stderr, "pp->dev too small\n");
				exit(1);
			}
			if (pathinfo(pp, conf->hwtable, DI_SYSFS | DI_WWID))
				return NULL;
			
			if (store_path(pathvec, pp)) {
				free_path(pp);
				return NULL;
			}
		}

		refwwid = MALLOC(WWID_SIZE);

		if (!refwwid)
			return NULL;
		
		memcpy(refwwid, pp->wwid, WWID_SIZE);
		return refwwid;
	}
	if (conf->dev_type == DEV_DEVMAP) {
		condlog(3, "limited scope = %s", conf->dev);
		/*
		 * may be an alias
		 */
		refwwid = get_mpe_wwid(conf->dev);

		if (refwwid)
			return refwwid;
		
		/*
		 * or directly a wwid
		 */
		refwwid = MALLOC(WWID_SIZE);

		if (!refwwid)
			return NULL;

		strncpy(refwwid, conf->dev, WWID_SIZE);
		return refwwid;
	}
	return NULL;
}

/*
 * print_path styles
 */
#define PRINT_PATH_ALL		0
#define PRINT_PATH_SHORT	1

static void
print_path (struct path * pp, int style)
{
	if (style != PRINT_PATH_SHORT && pp->wwid)
		printf ("%s ", pp->wwid);
	else
		printf ("  \\_ ");

	if (pp->sg_id.host_no < 0)
		printf("#:#:#:# ");
	else {
		printf("%i:%i:%i:%i ",
		       pp->sg_id.host_no,
		       pp->sg_id.channel,
		       pp->sg_id.scsi_id,
		       pp->sg_id.lun);
	}
	if (pp->dev)
		printf("%-4s ", pp->dev);

	if (pp->dev_t)
		printf("%-7s ", pp->dev_t);

	switch (pp->state) {
	case PATH_UP:
		printf("[ready ]");
		break;
	case PATH_DOWN:
		printf("[faulty]");
		break;
	case PATH_SHAKY:
		printf("[shaky ]");
		break;
	default:
		break;
	}
	switch (pp->dmstate) {
	case PSTATE_ACTIVE:
		printf("[active]");
		break;
	case PSTATE_FAILED:
		printf("[failed]");
		break;
	default:
		break;
	}
	if (pp->claimed)
		printf("[claimed]");

	if (style != PRINT_PATH_SHORT && pp->product_id)
		printf("[%.16s]", pp->product_id);

	fprintf(stdout, "\n");
}

static void
print_map (struct multipath * mpp)
{
	if (mpp->size && mpp->params)
		printf("0 %lu %s %s\n",
			 mpp->size, DEFAULT_TARGET, mpp->params);
	return;
}

static void
print_all_paths (vector pathvec)
{
	int i;
	struct path * pp;

	vector_foreach_slot (pathvec, pp, i)
		print_path(pp, PRINT_PATH_ALL);
}

static void
print_mp (struct multipath * mpp)
{
	int j, i;
	struct path * pp = NULL;
	struct pathgroup * pgp = NULL;

	if (mpp->action == ACT_NOTHING || conf->verbosity == 0)
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

	if (mpp->size < 2000)
		printf("[size=%lu kB]", mpp->size / 2);
	else if (mpp->size < (2000 * 1024))
		printf("[size=%lu MB]", mpp->size / 2 / 1024);
	else if (mpp->size < (2000 * 1024 * 1024))
		printf("[size=%lu GB]", mpp->size / 2 / 1024 / 1024);
	else
		printf("[size=%lu TB]", mpp->size / 2 / 1024 / 1024 / 1024);

	if (mpp->features)
		printf("[features=\"%s\"]", mpp->features);

	if (mpp->hwhandler)
		printf("[hwhandler=\"%s\"]", mpp->hwhandler);

	fprintf(stdout, "\n");

	if (!mpp->pg)
		return;

	vector_foreach_slot (mpp->pg, pgp, j) {
		printf("\\_ ");

		if (mpp->selector)
			printf("%s ", mpp->selector);

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
		if (mpp->nextpg && mpp->nextpg == j + 1)
			printf("[best]");

		printf("\n");

		vector_foreach_slot (pgp->paths, pp, i)
			print_path(pp, PRINT_PATH_SHORT);
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
		if (memcmp(pp->wwid, refwwid, WWID_SIZE) != 0) {
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
	char * p;
	struct pathgroup * pgp;
	struct path * pp;

	p = mp->params;
	freechar = sizeof(mp->params);
	
	shift = snprintf(p, freechar, "%s %s %i %i",
			 mp->features, mp->hwhandler,
			 VECTOR_SIZE(mp->pg), mp->nextpg);

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
			shift = snprintf(p, freechar, " %s %d",
					 pp->dev_t, conf->minio);
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
	struct path * pp;
	int i;

	/*
	 * don't bother if devmap size is unknown
	 */
	if (mpp->size <= 0) {
		condlog(3, "%s devmap size is unknown", mpp->alias);
		return 1;
	}

	/*
	 * don't bother if a constituant path is claimed
	 * FIXME : claimed detection broken, always unclaimed for now
	 */
	vector_foreach_slot (mpp->paths, pp, i) {
		if (pp->claimed) {
			condlog(3, "%s claimed", pp->dev);
			return 1;
		}
	}

	/*
	 * properties selectors
	 */
	select_pgpolicy(mpp);
	select_selector(mpp);
	select_features(mpp);
	select_hwhandler(mpp);

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
	select_path_group(mpp);

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

/*
 * detect if a path is in the map we are about to create but not in the
 * current one (triggers a valid reload)
 * if a path is in the current map but not in the one we are about to create,
 * don't reload : it may come back latter so save the reload burden
 */
static int
pgcmp2 (struct multipath * mpp, struct multipath * cmpp)
{
	int i, j, k, l;
	struct pathgroup * pgp;
	struct pathgroup * cpgp;
	struct path * pp;
	struct path * cpp;
	int found = 0;

	vector_foreach_slot (mpp->pg, pgp, i) {
		vector_foreach_slot (pgp->paths, pp, j) {
			vector_foreach_slot (cmpp->pg, cpgp, k) {
				vector_foreach_slot (cpgp->paths, cpp, l) {
					if (pp == cpp) {
						found = 1;
						break;
					}
				}
				if (found)
					break;
			}
			if (found)
				found = 0;
			else
				return 1;
		}
	}
	return 0;
}

static void
select_action (struct multipath * mpp, vector curmp)
{
	struct multipath * cmpp;

	cmpp = find_mp(curmp, mpp->alias);

	if (!cmpp) {
		mpp->action = ACT_CREATE;
		return;
	}
	if (pathcount(mpp, PATH_UP) == 0) {
		condlog(3, "no good path");
		mpp->action = ACT_NOTHING;
		return;
	}
	if (cmpp->size != mpp->size) {
		condlog(3, "size different than current");
		mpp->action = ACT_RELOAD;
		return;
	}
	if (strncmp(cmpp->features, mpp->features,
		    strlen(mpp->features))) {
		condlog(3, "features different than current");
		mpp->action =  ACT_RELOAD;
		return;
	}
	if (strncmp(cmpp->hwhandler, mpp->hwhandler,
		    strlen(mpp->hwhandler))) {
		condlog(3, "hwhandler different than current");
		mpp->action = ACT_RELOAD;
		return;
	}
	if (strncmp(cmpp->selector, mpp->selector,
		    strlen(mpp->selector))) {
		condlog(3, "selector different than current");
		mpp->action = ACT_RELOAD;
		return;
	}
	if (VECTOR_SIZE(cmpp->pg) != VECTOR_SIZE(mpp->pg)) {
		condlog(3, "different number of PG");
		mpp->action = ACT_RELOAD;
		return;
	}
	if (pgcmp2(mpp, cmpp)) {
		condlog(3, "different path group topology");
		mpp->action = ACT_RELOAD;
		return;
	}
	if (cmpp->nextpg != mpp->nextpg) {
		condlog(3, "nextpg different than current");
		mpp->action = ACT_SWITCHPG;
		return;
	}
	mpp->action = ACT_NOTHING;
	return;
}

static int
reinstate_paths (struct multipath * mpp)
{
	int i, j;
	struct pathgroup * pgp;
	struct path * pp;

	vector_foreach_slot (mpp->pg, pgp, i) {
		vector_foreach_slot (pgp->paths, pp, j) {
			if (pp->state != PATH_UP &&
			    (pgp->status == PGSTATE_DISABLED ||
			     pgp->status == PGSTATE_ACTIVE))
				continue;

			if (pp->dmstate == PSTATE_FAILED) {
				if (dm_reinstate(mpp->alias, pp->dev_t))
					condlog(0, "error reinstating %s",
						pp->dev);
			}
		}
	}
	return 0;
}

static int
domap (struct multipath * mpp)
{
	int op = ACT_NOTHING;
	int r = 0;

	print_mp(mpp);

	/*
	 * last chance to quit before touching the devmaps
	 */
	if (conf->dry_run || mpp->action == ACT_NOTHING)
		return 0;

	if (mpp->action == ACT_SWITCHPG) {
		dm_switchgroup(mpp->alias, mpp->nextpg);
		/*
		 * we may have avoided reinstating paths because there where in
		 * active or disabled PG. Now that the topology has changed,
		 * retry.
		 */
		reinstate_paths(mpp);
		return 0;
	}
	if (mpp->action == ACT_CREATE)
		op = DM_DEVICE_CREATE;

	if (mpp->action == ACT_RELOAD)
		op = DM_DEVICE_RELOAD;

		
	/*
	 * device mapper creation or updating
	 * here we know we'll have garbage on stderr from
	 * libdevmapper. so shut it down temporarily.
	 */
	dm_log_init_verbose(0);

	r = dm_addmap(op, mpp->alias, DEFAULT_TARGET, mpp->params, mpp->size);

	if (r == 0)
		dm_simplecmd(DM_DEVICE_REMOVE, mpp->alias);
	else if (op == DM_DEVICE_RELOAD)
		dm_simplecmd(DM_DEVICE_RESUME, mpp->alias);

	/*
	 * PG order is random, so we need to set the primary one
	 * upon create or reload
	 */
	dm_switchgroup(mpp->alias, mpp->nextpg);

	dm_log_init_verbose(1);

	return r;
}

static int
coalesce_paths (vector curmp, vector pathvec)
{
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
		select_alias(mpp);

		pp1->mpp = mpp;
		strcpy(mpp->wwid, pp1->wwid);
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
		if (setup_map(mpp)) {
			free_multipath(mpp, KEEP_PATHS);
			continue;
		}
		condlog(3, "action preset to %i", mpp->action);

		if (mpp->action == ACT_UNDEF)
			select_action(mpp, curmp);

		condlog(3, "action set to %i", mpp->action);

		domap(mpp);
		free_multipath(mpp, KEEP_PATHS);
	}
	return 0;
}

static void
usage (char * progname)
{
	fprintf (stderr, VERSION_STRING);
	fprintf (stderr, "Usage: %s\t[-v level] [-d] [-l|-ll]\n",
		progname);
	fprintf (stderr,
		"\t\t\t[-p failover|multibus|group_by_serial|group_by_prio]\n" \
		"\t\t\t[device]\n" \
		"\n" \
		"\t-v level\tverbosty level\n" \
		"\t   0\t\t\tno output\n" \
		"\t   1\t\t\tprint created devmap names only\n" \
		"\t   2\t\t\tdefault verbosity\n" \
		"\t   3\t\t\tprint debug information\n" \
		"\t-d\t\tdry run, do not create or update devmaps\n" \
		"\t-l\t\tshow multipath topology (sysfs and DM info)\n" \
		"\t-ll\t\tshow multipath topology (maximum info)\n" \
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
update_pathvec (vector pathvec)
{
	int i;
	struct path * pp;

	vector_foreach_slot (pathvec, pp, i) {
		if (pp->dev && pp->dev_t && strlen(pp->dev) == 0) {
			devt2devname(pp->dev, pp->dev_t);
			pathinfo(pp, conf->hwtable,
				DI_SYSFS | DI_CHECKER | DI_SERIAL | DI_PRIO);
		}
		if (pp->checkfn && pp->state == PATH_UNCHECKED)
			pp->state = pp->checkfn(pp->fd, NULL, NULL);
	}
	return 0;
}

static int
get_dm_mpvec (vector curmp, vector pathvec, char * refwwid)
{
	int i;
	struct multipath * mpp;
	char * wwid;

	if (dm_get_maps(curmp, DEFAULT_TARGET))
		return 1;

	vector_foreach_slot (curmp, mpp, i) {
		wwid = get_mpe_wwid(mpp->alias);

		if (wwid) {
			strncpy(mpp->wwid, wwid, WWID_SIZE);
			wwid = NULL;
		} else
			strncpy(mpp->wwid, mpp->alias, WWID_SIZE);

		if (refwwid && strncmp(mpp->wwid, refwwid, WWID_SIZE))
			continue;

		condlog(3, "params = %s", mpp->params);
		condlog(3, "status = %s", mpp->status);
		disassemble_map(pathvec, mpp->params, mpp);

		/*
		 * disassemble_map may have added new paths to pathvec.
		 * If not in "fast list mode", we need to fetch information
		 * about them
		 */
		if (conf->list != 1)
			update_pathvec(pathvec);

		disassemble_status(mpp->status, mpp);

		if (conf->list)
			print_mp(mpp);

		if (!conf->dry_run)
			reinstate_paths(mpp);
	}
	return 0;
}

int
main (int argc, char *argv[])
{
	vector curmp = NULL;
	vector pathvec = NULL;
	int i;
	int di_flag;
	int arg;
	extern char *optarg;
	extern int optind;
	char * refwwid = NULL;

	if (getuid() != 0) {
		fprintf(stderr, "need to be root\n");
		exit(1);
	}

	if (dm_prereq(DEFAULT_TARGET, 1, 0, 3)) {
		condlog(0, "device mapper prerequisites not met");
		exit(1);
	}
	if (sysfs_get_mnt_path(sysfs_path, FILE_NAME_SIZE)) {
		condlog(0, "multipath tools need sysfs mounted");
		exit(1);
	}
	if (load_config(DEFAULT_CONFIGFILE))
		exit(1);

	while ((arg = getopt(argc, argv, ":qdl::Fi:M:v:p:")) != EOF ) {
		switch(arg) {
		case 1: printf("optarg : %s\n",optarg);
			break;
		case 'v':
			if (sizeof(optarg) > sizeof(char *) ||
			    !isdigit(optarg[0]))
				usage (argv[0]);

			conf->verbosity = atoi(optarg);
			break;
		case 'd':
			conf->dry_run = 1;
			break;
		case 'F':
			dm_flush_maps(DEFAULT_TARGET);
			goto out;
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
	if (conf->dev && blacklist(conf->blist, conf->dev))
		goto out;
	
	if (!cache_cold(CACHE_EXPIRE)) {
		condlog(3, "load path identifiers cache");
		cache_load(pathvec);
	}

	/*
	 * get a path list
	 */
	if (conf->list > 1)
		/* extended path info '-ll' */
		di_flag = DI_SYSFS | DI_CHECKER;
	else if (conf->list)
		/* minimum path info '-l' */
		di_flag = DI_SYSFS;
	else
		/* maximum info */
		di_flag = DI_ALL;

	if (path_discovery(pathvec, conf, di_flag) || VECTOR_SIZE(pathvec) == 0)
		goto out;

	if (conf->verbosity > 2) {
		fprintf(stdout, "#\n# all paths :\n#\n");
		print_all_paths(pathvec);
	}

	refwwid = get_refwwid(pathvec);

	if (get_dm_mpvec(curmp, pathvec, refwwid))
		goto out;

	cache_dump(pathvec);
	filter_pathvec(pathvec, refwwid);

	if (conf->list)
		goto out;

	/*
	 * core logic entry point
	 */
	coalesce_paths(curmp, pathvec);

out:
	if (refwwid)
		FREE(refwwid);

	free_multipathvec(curmp, KEEP_PATHS);
	free_pathvec(pathvec, FREE_PATHS);
	free_config(conf);
	dm_lib_release();
	dm_lib_exit();
#ifdef _DEBUG_
	dbg_free_final(NULL);
#endif
	exit(0);
}
