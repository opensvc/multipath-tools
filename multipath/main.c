/*
 * Soft:        multipath device mapper target autoconfig
 *
 * Version:     $Id: main.h,v 0.0.1 2003/09/18 15:13:38 cvaroqui Exp $
 *
 * Author:      Christophe Varoqui
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
 *
 * Copyright (c) 2003, 2004, 2005 Christophe Varoqui
 * Copyright (c) 2005 Benjamin Marzinski, Redhat
 * Copyright (c) 2005 Kiyoshi Ueda, NEC
 * Copyright (c) 2005 Patrick Caulfield, Redhat
 * Copyright (c) 2005 Edward Goggin, EMC
 */

#include <stdio.h>
#include <unistd.h>
#include <ctype.h>

#include <checkers.h>
#include <prio.h>
#include <vector.h>
#include <memory.h>
#include <libdevmapper.h>
#include <devmapper.h>
#include <util.h>
#include <defaults.h>
#include <structs.h>
#include <structs_vec.h>
#include <dmparser.h>
#include <sysfs.h>
#include <config.h>
#include <blacklist.h>
#include <discovery.h>
#include <debug.h>
#include <switchgroup.h>
#include <print.h>
#include <alias.h>
#include <configure.h>
#include <pgpolicies.h>
#include <version.h>

int logsink;

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

static void
usage (char * progname)
{
	fprintf (stderr, VERSION_STRING);
	fprintf (stderr, "Usage:\n");
	fprintf (stderr, "  %s [-d] [-r] [-v lvl] [-p pol] [-b fil] [dev]\n", progname);
	fprintf (stderr, "  %s -l|-ll|-f [-v lvl] [-b fil] [dev]\n", progname);
	fprintf (stderr, "  %s -F [-v lvl]\n", progname);
	fprintf (stderr, "  %s -h\n", progname);
	fprintf (stderr,
		"\n"
		"Where:\n"
		"  -h      print this usage text\n" \
		"  -l      show multipath topology (sysfs and DM info)\n" \
		"  -ll     show multipath topology (maximum info)\n" \
		"  -f      flush a multipath device map\n" \
		"  -F      flush all multipath device maps\n" \
		"  -d      dry run, do not create or update devmaps\n" \
		"  -r      force devmap reload\n" \
		"  -p      policy failover|multibus|group_by_serial|group_by_prio\n" \
		"  -b fil  bindings file location\n" \
		"  -p pol  force all maps to specified path grouping policy :\n" \
		"          . failover            one path per priority group\n" \
		"          . multibus            all paths in one priority group\n" \
		"          . group_by_serial     one priority group per serial\n" \
		"          . group_by_prio       one priority group per priority lvl\n" \
		"          . group_by_node_name  one priority group per target node\n" \
		"  -v lvl  verbosity level\n" \
		"          . 0 no output\n" \
		"          . 1 print created devmap names only\n" \
		"          . 2 default verbosity\n" \
		"          . 3 print debug information\n" \
		"  dev     action limited to:\n" \
		"          . multipath named 'dev' (ex: mpath0) or\n" \
		"          . multipath whose wwid is 'dev' (ex: 60051..)\n" \
		"          . multipath including the path named 'dev' (ex: /dev/sda)\n" \
		"          . multipath including the path with maj:min 'dev' (ex: 8:0)\n" \
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
				pp->mpp = mpp;
				pathinfo(pp, conf->hwtable, DI_ALL);
				continue;
			}
			pp->mpp = mpp;
			if (pp->state == PATH_UNCHECKED)
				pathinfo(pp, conf->hwtable, DI_CHECKER);

			if (pp->priority == PRIO_UNDEF)
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
			print_multipath_topology(mpp, conf->verbosity);

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
	struct vectors vecs;
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
	vecs.pathvec = pathvec;
	vecs.mpvec = curmp;

	/*
	 * dev is "/dev/" . "sysfs block dev"
	 */
	if (conf->dev) {
		if (!strncmp(conf->dev, "/dev/", 5) &&
		    strlen(conf->dev) > 5)
			dev = conf->dev + 5;
		else
			dev = conf->dev;
	}
	
	/*
	 * if we have a blacklisted device parameter, exit early
	 */
	if (dev && 
	    (filter_devnode(conf->blist_devnode, conf->elist_devnode, dev) > 0))
			goto out;
	
	/*
	 * scope limiting must be translated into a wwid
	 * failing the translation is fatal (by policy)
	 */
	if (conf->dev) {
		refwwid = get_refwwid(conf->dev, conf->dev_type, pathvec);

		if (!refwwid) {
			condlog(3, "scope is nul");
			goto out;
		}
		condlog(3, "scope limited to %s", refwwid);
		if (filter_wwid(conf->blist_wwid, conf->elist_wwid,
				refwwid) > 0)
			goto out;
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

	get_path_layout(pathvec);

	if (get_dm_mpvec(curmp, pathvec, refwwid))
		goto out;

	filter_pathvec(pathvec, refwwid);

	if (conf->list) {
		r = 0;
		goto out;
	}

	/*
	 * core logic entry point
	 */
	r = coalesce_paths(&vecs, NULL, NULL, conf->force_reload);

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
	int i, r = 1;

	if (getuid() != 0) {
		fprintf(stderr, "need to be root\n");
		exit(1);
	}

	if (dm_prereq(DEFAULT_TARGET))
		exit(1);

	if (load_config(DEFAULT_CONFIGFILE))
		exit(1);

	if (init_checkers()) {
		condlog(0, "failed to initialize checkers");
		exit(1);
	}
	if (init_prio()) {
		condlog(0, "failed to initialize prioritizers");
		exit(1);
	}
	if (sysfs_init(conf->sysfs_dir, FILE_NAME_SIZE)) {
		condlog(0, "multipath tools need sysfs mounted");
		exit(1);
	}
	while ((arg = getopt(argc, argv, ":dhl::FfM:v:p:b:r")) != EOF ) {
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
		case 'r':
			conf->force_reload = 1;
			break;
		case 'h':
			usage(argv[0]);
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
	dm_init();

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
	sysfs_cleanup();
	dm_lib_release();
	dm_lib_exit();

	/*
	 * Freeing config must be done after dm_lib_exit(), because
	 * the logging function (dm_write_log()), which is called there,
	 * references the config.
	 */
	free_config(conf);
	conf = NULL;

#ifdef _DEBUG_
	dbg_free_final(NULL);
#endif
	return r;
}
