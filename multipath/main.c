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
#include <syslog.h>

#include "checkers.h"
#include "prio.h"
#include "vector.h"
#include "memory.h"
#include <libdevmapper.h>
#include "devmapper.h"
#include "util.h"
#include "defaults.h"
#include "config.h"
#include "structs.h"
#include "structs_vec.h"
#include "dmparser.h"
#include "sysfs.h"
#include "blacklist.h"
#include "discovery.h"
#include "debug.h"
#include "switchgroup.h"
#include "print.h"
#include "alias.h"
#include "configure.h"
#include "pgpolicies.h"
#include "version.h"
#include <errno.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "wwids.h"
#include "uxsock.h"
#include "mpath_cmd.h"

int logsink;
struct udev *udev;
struct config *multipath_conf;

struct config *get_multipath_config(void)
{
	return multipath_conf;
}

void put_multipath_config(struct config *conf)
{
	/* Noop for now */
}

void rcu_register_thread_memb(void) {}

void rcu_unregister_thread_memb(void) {}

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
	fprintf (stderr, "  %s [-a|-c|-w|-W] [-d] [-r] [-i] [-v lvl] [-p pol] [-b fil] [-q] [dev]\n", progname);
	fprintf (stderr, "  %s -l|-ll|-f [-v lvl] [-b fil] [dev]\n", progname);
	fprintf (stderr, "  %s -F [-v lvl]\n", progname);
	fprintf (stderr, "  %s -t\n", progname);
	fprintf (stderr, "  %s -h\n", progname);
	fprintf (stderr,
		"\n"
		"Where:\n"
		"  -h      print this usage text\n"
		"  -l      show multipath topology (sysfs and DM info)\n"
		"  -ll     show multipath topology (maximum info)\n"
		"  -f      flush a multipath device map\n"
		"  -F      flush all multipath device maps\n"
		"  -a      add a device wwid to the wwids file\n"
		"  -c      check if a device should be a path in a multipath device\n"
		"  -q      allow queue_if_no_path when multipathd is not running\n"
		"  -d      dry run, do not create or update devmaps\n"
		"  -t      dump internal hardware table\n"
		"  -r      force devmap reload\n"
		"  -i      ignore wwids file\n"
		"  -B      treat the bindings file as read only\n"
		"  -b fil  bindings file location\n"
		"  -w      remove a device from the wwids file\n"
		"  -W      reset the wwids file include only the current devices\n"
		"  -p pol  force all maps to specified path grouping policy :\n"
		"          . failover            one path per priority group\n"
		"          . multibus            all paths in one priority group\n"
		"          . group_by_serial     one priority group per serial\n"
		"          . group_by_prio       one priority group per priority lvl\n"
		"          . group_by_node_name  one priority group per target node\n"
		"  -v lvl  verbosity level\n"
		"          . 0 no output\n"
		"          . 1 print created devmap names only\n"
		"          . 2 default verbosity\n"
		"          . 3 print debug information\n"
		"  dev     action limited to:\n"
		"          . multipath named 'dev' (ex: mpath0) or\n"
		"          . multipath whose wwid is 'dev' (ex: 60051..)\n"
		"          . multipath including the path named 'dev' (ex: /dev/sda)\n"
		"          . multipath including the path with maj:min 'dev' (ex: 8:0)\n"
		);

}

static int
update_paths (struct multipath * mpp)
{
	int i, j;
	struct pathgroup * pgp;
	struct path * pp;
	struct config *conf;

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
				conf = get_multipath_config();
				if (pathinfo(pp, conf, DI_ALL))
					pp->state = PATH_UNCHECKED;
				put_multipath_config(conf);
				continue;
			}
			pp->mpp = mpp;
			if (pp->state == PATH_UNCHECKED ||
			    pp->state == PATH_WILD) {
				conf = get_multipath_config();
				if (pathinfo(pp, conf, DI_CHECKER))
					pp->state = PATH_UNCHECKED;
				put_multipath_config(conf);
			}

			if (pp->priority == PRIO_UNDEF) {
				conf = get_multipath_config();
				if (pathinfo(pp, conf, DI_PRIO))
					pp->priority = PRIO_UNDEF;
				put_multipath_config(conf);
			}
		}
	}
	return 0;
}

static int
get_dm_mpvec (enum mpath_cmds cmd, vector curmp, vector pathvec, char * refwwid)
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

		if (cmd == CMD_VALID_PATH)
			continue;

		dm_get_map(mpp->alias, &mpp->size, params);
		condlog(3, "params = %s", params);
		dm_get_status(mpp->alias, status);
		condlog(3, "status = %s", status);

		disassemble_map(pathvec, params, mpp, 0);

		/*
		 * disassemble_map() can add new paths to pathvec.
		 * If not in "fast list mode", we need to fetch information
		 * about them
		 */
		if (cmd != CMD_LIST_SHORT)
			update_paths(mpp);

		if (cmd == CMD_LIST_LONG)
			mpp->bestpg = select_path_group(mpp);

		disassemble_status(status, mpp);

		if (cmd == CMD_LIST_SHORT ||
		    cmd == CMD_LIST_LONG) {
			struct config *conf = get_multipath_config();
			print_multipath_topology(mpp, conf->verbosity);
			put_multipath_config(conf);
		}

		if (cmd == CMD_CREATE)
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
configure (enum mpath_cmds cmd, enum devtypes dev_type, char *devpath)
{
	vector curmp = NULL;
	vector pathvec = NULL;
	struct vectors vecs;
	int r = 1;
	int di_flag = 0;
	char * refwwid = NULL;
	char * dev = NULL;
	struct config *conf;

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

	dev = convert_dev(devpath, (dev_type == DEV_DEVNODE));

	/*
	 * if we have a blacklisted device parameter, exit early
	 */
	conf = get_multipath_config();
	if (dev && (dev_type == DEV_DEVNODE ||
		    dev_type == DEV_UEVENT) &&
	    cmd != CMD_REMOVE_WWID &&
	    (filter_devnode(conf->blist_devnode,
			    conf->elist_devnode, dev) > 0)) {
		if (cmd == CMD_VALID_PATH)
			printf("%s is not a valid multipath device path\n",
			       devpath);
		put_multipath_config(conf);
		goto out;
	}
	put_multipath_config(conf);
	/*
	 * scope limiting must be translated into a wwid
	 * failing the translation is fatal (by policy)
	 */
	if (devpath) {
		int failed = get_refwwid(cmd, devpath, dev_type,
					 pathvec, &refwwid);
		if (!refwwid) {
			condlog(4, "%s: failed to get wwid", devpath);
			if (failed == 2 && cmd == CMD_VALID_PATH)
				printf("%s is not a valid multipath device path\n", devpath);
			else
				condlog(3, "scope is null");
			goto out;
		}
		if (cmd == CMD_REMOVE_WWID) {
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
		if (cmd == CMD_ADD_WWID) {
			r = remember_wwid(refwwid);
			if (r == 0)
				printf("wwid '%s' added\n", refwwid);
			else
				printf("failed adding '%s' to wwids file\n",
				       refwwid);
			goto out;
		}
		condlog(3, "scope limited to %s", refwwid);
		/* If you are ignoring the wwids file and find_multipaths is
		 * set, you need to actually check if there are two available
		 * paths to determine if this path should be multipathed. To
		 * do this, we put off the check until after discovering all
		 * the paths */
		if (cmd == CMD_VALID_PATH &&
		    (!conf->find_multipaths || !conf->ignore_wwids)) {
			if (conf->ignore_wwids ||
			    check_wwids_file(refwwid, 0) == 0)
				r = 0;

			printf("%s %s a valid multipath device path\n",
			       devpath, r == 0 ? "is" : "is not");
			goto out;
		}
	}

	/*
	 * get a path list
	 */
	if (devpath)
		di_flag = DI_WWID;

	if (cmd == CMD_LIST_LONG)
		/* extended path info '-ll' */
		di_flag |= DI_SYSFS | DI_CHECKER;
	else if (cmd == CMD_LIST_SHORT)
		/* minimum path info '-l' */
		di_flag |= DI_SYSFS;
	else
		/* maximum info */
		di_flag = DI_ALL;

	if (path_discovery(pathvec, di_flag) < 0)
		goto out;

	if (conf->verbosity > 2)
		print_all_paths(pathvec, 1);

	get_path_layout(pathvec, 0);

	if (get_dm_mpvec(cmd, curmp, pathvec, refwwid))
		goto out;

	filter_pathvec(pathvec, refwwid);


	if (cmd == CMD_VALID_PATH) {
		/* This only happens if find_multipaths and
		 * ignore_wwids is set.
		 * If there is currently a multipath device matching
		 * the refwwid, or there is more than one path matching
		 * the refwwid, then the path is valid */
		if (VECTOR_SIZE(curmp) != 0 || VECTOR_SIZE(pathvec) > 1)
			r = 0;
		printf("%s %s a valid multipath device path\n",
		       devpath, r == 0 ? "is" : "is not");
		goto out;
	}

	if (cmd != CMD_CREATE && cmd != CMD_DRY_RUN) {
		r = 0;
		goto out;
	}

	/*
	 * core logic entry point
	 */
	r = coalesce_paths(&vecs, NULL, refwwid,
			   conf->force_reload, cmd);

out:
	if (refwwid)
		FREE(refwwid);

	free_multipathvec(curmp, KEEP_PATHS);
	free_pathvec(pathvec, FREE_PATHS);

	return r;
}

static int
dump_config (struct config *conf)
{
	char * c, * tmp = NULL;
	char * reply;
	unsigned int maxlen = 256;
	int again = 1;

	reply = MALLOC(maxlen);

	while (again) {
		if (!reply) {
			if (tmp)
				free(tmp);
			return 1;
		}
		c = tmp = reply;
		c += snprint_defaults(conf, c, reply + maxlen - c);
		again = ((c - reply) == maxlen);
		if (again) {
			reply = REALLOC(reply, maxlen *= 2);
			continue;
		}
		c += snprint_blacklist(conf, c, reply + maxlen - c);
		again = ((c - reply) == maxlen);
		if (again) {
			reply = REALLOC(reply, maxlen *= 2);
			continue;
		}
		c += snprint_blacklist_except(conf, c, reply + maxlen - c);
		again = ((c - reply) == maxlen);
		if (again) {
			reply = REALLOC(reply, maxlen *= 2);
			continue;
		}
		c += snprint_hwtable(conf, c, reply + maxlen - c, conf->hwtable);
		again = ((c - reply) == maxlen);
		if (again) {
			reply = REALLOC(reply, maxlen *= 2);
			continue;
		}
		c += snprint_overrides(conf, c, reply + maxlen - c,
				       conf->overrides);
		again = ((c - reply) == maxlen);
		if (again) {
			reply = REALLOC(reply, maxlen *= 2);
			continue;
		}
		if (VECTOR_SIZE(conf->mptable) > 0) {
			c += snprint_mptable(conf, c, reply + maxlen - c,
					     conf->mptable);
			again = ((c - reply) == maxlen);
			if (again)
				reply = REALLOC(reply, maxlen *= 2);
		}
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
		if (dm_is_dm_major(major(buf.st_rdev)))
			return DEV_DEVMAP;
		return DEV_DEVNODE;
	}
	else if (sscanf(dev, "%d:%d", &i, &i) == 2)
		return DEV_DEVT;
	else if (valid_alias(dev))
		return DEV_DEVMAP;
	return DEV_NONE;
}

int
main (int argc, char *argv[])
{
	int arg;
	extern char *optarg;
	extern int optind;
	int r = 1;
	enum mpath_cmds cmd = CMD_CREATE;
	enum devtypes dev_type;
	char *dev = NULL;
	struct config *conf;

	udev = udev_new();
	logsink = 0;
	conf = load_config(DEFAULT_CONFIGFILE);
	if (!conf)
		exit(1);
	multipath_conf = conf;
	while ((arg = getopt(argc, argv, ":adchl::FfM:v:p:b:BritquwW")) != EOF ) {
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
			cmd = CMD_VALID_PATH;
			break;
		case 'd':
			if (cmd == CMD_CREATE)
				cmd = CMD_DRY_RUN;
			break;
		case 'f':
			conf->remove = FLUSH_ONE;
			break;
		case 'F':
			conf->remove = FLUSH_ALL;
			break;
		case 'l':
			if (optarg && !strncmp(optarg, "l", 1))
				cmd = CMD_LIST_LONG;
			else
				cmd = CMD_LIST_SHORT;

			break;
		case 'M':
#if _DEBUG_
			debug = atoi(optarg);
#endif
			break;
		case 'p':
			conf->pgpolicy_flag = get_pgpolicy_id(optarg);
			if (conf->pgpolicy_flag == IOPOLICY_UNDEF) {
				printf("'%s' is not a valid policy\n", optarg);
				usage(argv[0]);
				exit(1);
			}
			break;
		case 'r':
			conf->force_reload = 1;
			break;
		case 'i':
			conf->ignore_wwids = 1;
			break;
		case 't':
			r = dump_config(conf);
			goto out_free_config;
		case 'h':
			usage(argv[0]);
			exit(0);
		case 'u':
			cmd = CMD_VALID_PATH;
			dev_type = DEV_UEVENT;
			break;
		case 'w':
			cmd = CMD_REMOVE_WWID;
			break;
		case 'W':
			cmd = CMD_RESET_WWIDS;
			break;
		case 'a':
			cmd = CMD_ADD_WWID;
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

	dm_init(conf->verbosity);
	if (dm_prereq())
		exit(1);
	dm_drv_version(conf->version, TGT_MPATH);
	dm_udev_set_sync_support(1);

	if (optind < argc) {
		dev = MALLOC(FILE_NAME_SIZE);

		if (!dev)
			goto out;

		strncpy(dev, argv[optind], FILE_NAME_SIZE);
		if (dev_type != DEV_UEVENT)
			dev_type = get_dev_type(dev);
		if (dev_type == DEV_NONE) {
			condlog(0, "'%s' is not a valid argument\n", dev);
			goto out;
		}
	}
	if (dev_type == DEV_UEVENT) {
		openlog("multipath", 0, LOG_DAEMON);
		setlogmask(LOG_UPTO(conf->verbosity + 3));
		logsink = 1;
	}

	if (conf->max_fds) {
		struct rlimit fd_limit;

		fd_limit.rlim_cur = conf->max_fds;
		fd_limit.rlim_max = conf->max_fds;
		if (setrlimit(RLIMIT_NOFILE, &fd_limit) < 0)
			condlog(0, "can't set open fds limit to %d : %s",
				conf->max_fds, strerror(errno));
	}

	if (init_checkers(conf->multipath_dir)) {
		condlog(0, "failed to initialize checkers");
		goto out;
	}
	if (init_prio(conf->multipath_dir)) {
		condlog(0, "failed to initialize prioritizers");
		goto out;
	}

	if (cmd == CMD_VALID_PATH &&
	    (!dev || dev_type == DEV_DEVMAP)) {
		condlog(0, "the -c option requires a path to check");
		goto out;
	}
	if (cmd == CMD_VALID_PATH &&
	    dev_type == DEV_UEVENT) {
		int fd;

		fd = mpath_connect();
		if (fd == -1) {
			printf("%s is not a valid multipath device path\n",
				dev);
			goto out;
		}
		mpath_disconnect(fd);
	}
	if (cmd == CMD_REMOVE_WWID && !dev) {
		condlog(0, "the -w option requires a device");
		goto out;
	}
	if (cmd == CMD_RESET_WWIDS) {
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
		if (dev_type == DEV_DEVMAP) {
			r = dm_suspend_and_flush_map(dev);
		} else
			condlog(0, "must provide a map name to remove");

		goto out;
	}
	else if (conf->remove == FLUSH_ALL) {
		r = dm_flush_maps();
		goto out;
	}
	while ((r = configure(cmd, dev_type, dev)) < 0)
		condlog(3, "restart multipath configuration process");

out:
	dm_lib_release();
	dm_lib_exit();

	cleanup_prio();
	cleanup_checkers();

	if (dev_type == DEV_UEVENT)
		closelog();

out_free_config:
	/*
	 * Freeing config must be done after dm_lib_exit(), because
	 * the logging function (dm_write_log()), which is called there,
	 * references the config.
	 */
	free_config(conf);
	conf = NULL;
	udev_unref(udev);
	if (dev)
		FREE(dev);
#ifdef _DEBUG_
	dbg_free_final(NULL);
#endif
	return r;
}
