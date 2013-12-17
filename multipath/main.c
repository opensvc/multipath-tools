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

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <libudev.h>

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
#include <errno.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <wwids.h>
#include "dev_t.h"

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
	fprintf (stderr, "  %s [-c|-w|-W] [-d] [-r] [-v lvl] [-p pol] [-b fil] [-q] [dev]\n", progname);
	fprintf (stderr, "  %s -l|-ll|-f [-v lvl] [-b fil] [dev]\n", progname);
	fprintf (stderr, "  %s -F [-v lvl]\n", progname);
	fprintf (stderr, "  %s -t\n", progname);
	fprintf (stderr, "  %s -h\n", progname);
	fprintf (stderr,
		"\n"
		"Where:\n"
		"  -h      print this usage text\n" \
		"  -l      show multipath topology (sysfs and DM info)\n" \
		"  -ll     show multipath topology (maximum info)\n" \
		"  -f      flush a multipath device map\n" \
		"  -F      flush all multipath device maps\n" \
		"  -c      check if a device should be a path in a multipath device\n" \
		"  -q      allow queue_if_no_path when multipathd is not running\n"\
		"  -d      dry run, do not create or update devmaps\n" \
		"  -t      dump internal hardware table\n" \
		"  -r      force devmap reload\n" \
		"  -B      treat the bindings file as read only\n" \
		"  -p      policy failover|multibus|group_by_serial|group_by_prio\n" \
		"  -b fil  bindings file location\n" \
		"  -w      remove a device from the wwids file\n" \
		"  -W      reset the wwids file include only the current devices\n" \
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
				if (devt2devname(pp->dev, FILE_NAME_SIZE,
						 pp->dev_t)) {
					/*
					 * path is not in sysfs anymore
					 */
					pp->chkrstate = pp->state = PATH_DOWN;
					continue;
				}
				pp->mpp = mpp;
				if (pathinfo(pp, conf->hwtable, DI_ALL))
					pp->state = PATH_UNCHECKED;
				continue;
			}
			pp->mpp = mpp;
			if (pp->state == PATH_UNCHECKED ||
			    pp->state == PATH_WILD) {
				if (pathinfo(pp, conf->hwtable, DI_CHECKER))
					pp->state = PATH_UNCHECKED;
			}

			if (pp->priority == PRIO_UNDEF) {
				if (pathinfo(pp, conf->hwtable, DI_PRIO))
					pp->priority = PRIO_UNDEF;
			}
		}
	}
	return 0;
}

static int
get_dm_mpvec (vector curmp, vector pathvec, char * refwwid)
{
	int i;
	struct multipath * mpp;
	char params[PARAMS_SIZE], status[PARAMS_SIZE];

	if (dm_get_maps(curmp))
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

		dm_get_map(mpp->alias, &mpp->size, params);
		condlog(3, "params = %s", params);
		dm_get_status(mpp->alias, status);
		condlog(3, "status = %s", status);

		disassemble_map(pathvec, params, mpp);

		/*
		 * disassemble_map() can add new paths to pathvec.
		 * If not in "fast list mode", we need to fetch information
		 * about them
		 */
		if (conf->list != 1)
			update_paths(mpp);

		if (conf->list > 1)
			mpp->bestpg = select_path_group(mpp);

		disassemble_status(status, mpp);

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

	dev = convert_dev(conf->dev, (conf->dev_type == DEV_DEVNODE));

	/*
	 * if we have a blacklisted device parameter, exit early
	 */
	if (dev && conf->dev_type == DEV_DEVNODE && conf->dry_run != 3 &&
	    (filter_devnode(conf->blist_devnode,
			    conf->elist_devnode, dev) > 0)) {
		if (conf->dry_run == 2)
			printf("%s is not a valid multipath device path\n",
			       conf->dev);
		goto out;
	}
	/*
	 * scope limiting must be translated into a wwid
	 * failing the translation is fatal (by policy)
	 */
	if (conf->dev) {
		int failed = get_refwwid(conf->dev, conf->dev_type, pathvec,
					 &refwwid);
		if (!refwwid) {
			if (failed == 2 && conf->dry_run == 2)
				printf("%s is not a valid multipath device path\n", conf->dev);
			else
				condlog(3, "scope is nul");
			goto out;
		}
		if (conf->dry_run == 3) {
			r = remove_wwid(refwwid);
			if (r == 0)
				printf("wwid '%s' removed\n", refwwid);
			else if (r == 1) {
				printf("wwid '%s' not in wwids file\n",
					refwwid);
				r = 0;
			}
			goto out;
		}
		condlog(3, "scope limited to %s", refwwid);
		if (conf->dry_run == 2) {
			if (check_wwids_file(refwwid, 0) == 0){
				printf("%s is a valid multipath device path\n", conf->dev);
				r = 0;
			}
			else
				printf("%s is not a valid multipath device path\n", conf->dev);
			goto out;
		}
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

	get_path_layout(pathvec, 0);

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

static int
dump_config (void)
{
	char * c;
	char * reply;
	unsigned int maxlen = 256;
	int again = 1;

	reply = MALLOC(maxlen);

	while (again) {
		if (!reply)
			return 1;
		c = reply;
		c += snprint_defaults(c, reply + maxlen - c);
		again = ((c - reply) == maxlen);
		if (again) {
			reply = REALLOC(reply, maxlen *= 2);
			continue;
		}
		c += snprint_blacklist(c, reply + maxlen - c);
		again = ((c - reply) == maxlen);
		if (again) {
			reply = REALLOC(reply, maxlen *= 2);
			continue;
		}
		c += snprint_blacklist_except(c, reply + maxlen - c);
		again = ((c - reply) == maxlen);
		if (again) {
			reply = REALLOC(reply, maxlen *= 2);
			continue;
		}
		c += snprint_hwtable(c, reply + maxlen - c, conf->hwtable);
		again = ((c - reply) == maxlen);
		if (again) {
			reply = REALLOC(reply, maxlen *= 2);
			continue;
		}
		c += snprint_mptable(c, reply + maxlen - c, conf->mptable);
		again = ((c - reply) == maxlen);
		if (again)
			reply = REALLOC(reply, maxlen *= 2);
	}

	printf("%s", reply);
	FREE(reply);
	return 0;
}

static int
get_dev_type(char *dev) {
	struct stat buf;
	int i;

	if (stat(dev, &buf) == 0 && S_ISBLK(buf.st_mode)) {
		if (dm_is_dm_major(MAJOR(buf.st_rdev)))
			return DEV_DEVMAP;
		return DEV_DEVNODE;
	}
	else if (sscanf(dev, "%d:%d", &i, &i) == 2)
		return DEV_DEVT;
	else
		return DEV_DEVMAP;
}

int
main (int argc, char *argv[])
{
	struct udev *udev;
	int arg;
	extern char *optarg;
	extern int optind;
	int r = 1;

	udev = udev_new();

	if (load_config(DEFAULT_CONFIGFILE, udev))
		exit(1);

	while ((arg = getopt(argc, argv, ":dchl::FfM:v:p:b:BrtqwW")) != EOF ) {
		switch(arg) {
		case 1: printf("optarg : %s\n",optarg);
			break;
		case 'v':
			if (sizeof(optarg) > sizeof(char *) ||
			    !isdigit(optarg[0])) {
				usage (argv[0]);
				exit(1);
			}

			conf->verbosity = atoi(optarg);
			break;
		case 'b':
			conf->bindings_file = strdup(optarg);
			break;
		case 'B':
			conf->bindings_read_only = 1;
			break;
		case 'q':
			conf->allow_queueing = 1;
			break;
		case 'c':
			conf->dry_run = 2;
			break;
		case 'd':
			if (!conf->dry_run)
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
				exit(1);
			}
			break;
		case 'r':
			conf->force_reload = 1;
			break;
		case 't':
			r = dump_config();
			goto out_free_config;
		case 'h':
			usage(argv[0]);
			exit(0);
		case 'w':
			conf->dry_run = 3;
			break;
		case 'W':
			conf->dry_run = 4;
			break;
		case ':':
			fprintf(stderr, "Missing option argument\n");
			usage(argv[0]);
			exit(1);
		case '?':
			fprintf(stderr, "Unknown switch: %s\n", optarg);
			usage(argv[0]);
			exit(1);
		default:
			usage(argv[0]);
			exit(1);
		}
	}

	if (getuid() != 0) {
		fprintf(stderr, "need to be root\n");
		exit(1);
	}

	if (dm_prereq())
		exit(1);
	dm_drv_version(conf->version, TGT_MPATH);

	if (optind < argc) {
		conf->dev = MALLOC(FILE_NAME_SIZE);

		if (!conf->dev)
			goto out;

		strncpy(conf->dev, argv[optind], FILE_NAME_SIZE);
		conf->dev_type = get_dev_type(conf->dev);
	}
	conf->daemon = 0;

	if (conf->max_fds) {
		struct rlimit fd_limit;

		fd_limit.rlim_cur = conf->max_fds;
		fd_limit.rlim_max = conf->max_fds;
		if (setrlimit(RLIMIT_NOFILE, &fd_limit) < 0)
			condlog(0, "can't set open fds limit to %d : %s",
				conf->max_fds, strerror(errno));
	}

	if (init_checkers()) {
		condlog(0, "failed to initialize checkers");
		goto out;
	}
	if (init_prio()) {
		condlog(0, "failed to initialize prioritizers");
		goto out;
	}
	dm_init();

	if (conf->dry_run == 2 &&
	    (!conf->dev || conf->dev_type == DEV_DEVMAP)) {
		condlog(0, "the -c option requires a path to check");
		goto out;
	}
	if (conf->dry_run == 3 && !conf->dev) {
		condlog(0, "the -w option requires a device");
		goto out;
	}
	if (conf->dry_run == 4) {
		struct multipath * mpp;
		int i;
		vector curmp;

		curmp = vector_alloc();
		if (!curmp) {
			condlog(0, "can't allocate memory for mp list");
			goto out;
		}
		if (dm_get_maps(curmp) == 0)
			r = replace_wwids(curmp);
		if (r == 0)
			printf("successfully reset wwids\n");
		vector_foreach_slot_backwards(curmp, mpp, i) {
			vector_del_slot(curmp, i);
			free_multipath(mpp, KEEP_PATHS);
		}
		vector_free(curmp);
		goto out;
	}
	if (conf->remove == FLUSH_ONE) {
		if (conf->dev_type == DEV_DEVMAP) {
			r = dm_suspend_and_flush_map(conf->dev);
		} else
			condlog(0, "must provide a map name to remove");

		goto out;
	}
	else if (conf->remove == FLUSH_ALL) {
		r = dm_flush_maps();
		goto out;
	}
	while ((r = configure()) < 0)
		condlog(3, "restart multipath configuration process");

out:
	udev_wait(conf->cookie);

	dm_lib_release();
	dm_lib_exit();

	cleanup_prio();
	cleanup_checkers();

out_free_config:
	/*
	 * Freeing config must be done after dm_lib_exit(), because
	 * the logging function (dm_write_log()), which is called there,
	 * references the config.
	 */
	free_config(conf);
	conf = NULL;
	udev_unref(udev);
#ifdef _DEBUG_
	dbg_free_final(NULL);
#endif
	return r;
}
