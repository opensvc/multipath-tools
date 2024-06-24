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
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <libudev.h>
#include <syslog.h>
#include <fcntl.h>

#include "checkers.h"
#include "prio.h"
#include "vector.h"
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
#include "dm-generic.h"
#include "print.h"
#include "alias.h"
#include "configure.h"
#include "pgpolicies.h"
#include "version.h"
#include <errno.h>
#include "wwids.h"
#include "uxsock.h"
#include "mpath_cmd.h"
#include "foreign.h"
#include "propsel.h"
#include "time-util.h"
#include "file.h"
#include "valid.h"

/*
 * Return values of configure(), check_path_valid(), and main().
 */
enum {
	RTVL_OK = 0,
	RTVL_FAIL = 1,
	RTVL_RETRY, /* returned by configure(), not by main() */
};

static int
dump_config (struct config *conf, vector hwes, vector mpvec)
{
	char * reply = snprint_config(conf, NULL, hwes, mpvec);

	if (reply != NULL) {
		printf("%s", reply);
		free(reply);
		return 0;
	} else
		return 1;
}

void rcu_register_thread_memb(void) {}

void rcu_unregister_thread_memb(void) {}

static int
filter_pathvec (vector pathvec, const char *refwwid)
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
	fprintf (stderr, "  %s [-v level] [-B|-d|-i|-q|-r] [-b file] [-p policy] [device]\n", progname);
	fprintf (stderr, "  %s [-v level] [-R retries] -f device\n", progname);
	fprintf (stderr, "  %s [-v level] [-R retries] -F\n", progname);
	fprintf (stderr, "  %s [-v level] [-l|-ll] [device]\n", progname);
	fprintf (stderr, "  %s [-v level] [-a|-w] device\n", progname);
	fprintf (stderr, "  %s [-v level] -W\n", progname);
	fprintf (stderr, "  %s [-v level] [-i] [-c|-C] device\n", progname);
	fprintf (stderr, "  %s [-v level] [-i] [-u|-U]\n", progname);
	fprintf (stderr, "  %s [-h|-t|-T]\n", progname);
	fprintf (stderr,
		"\n"
		"Where:\n"
		"  -h      print this usage text\n"
		"  -l      show multipath topology (sysfs and DM info)\n"
		"  -ll     show multipath topology (maximum info)\n"
		"  -e      enable foreign libraries with -l/-ll\n"
		"  -f      flush a multipath device map\n"
		"  -F      flush all multipath device maps\n"
		"  -a      add a device wwid to the wwids file\n"
		"  -c      check if a device should be a path in a multipath device\n"
		"  -C      check if a multipath device has usable paths\n"
		"  -q      allow queue_if_no_path when multipathd is not running\n"
		"  -d      dry run, do not create or update devmaps\n"
		"  -t      display the currently used multipathd configuration\n"
		"  -T      display the multipathd configuration without builtin defaults\n"
		"  -r      force devmap reload\n"
		"  -i      ignore wwids file\n"
		"  -B      treat the bindings file as read only\n"
		"  -b fil  bindings file location\n"
		"  -w      remove a device from the wwids file\n"
		"  -W      reset the wwids file include only the current devices\n"
		"  -R num  number of times to retry removes of in-use devices\n"
		"  -u      check if the device specified in the program environment should be a\n"
		"          path in a multipath device\n"
		"  -U      check if the device specified in the program environment is a\n"
		"          multipath device with usable paths, see -C flag\n"
		"  -p pol  force all maps to specified path grouping policy:\n"
		"          . failover            one path per priority group\n"
		"          . multibus            all paths in one priority group\n"
		"          . group_by_serial     one priority group per serial\n"
		"          . group_by_prio       one priority group per priority lvl\n"
		"          . group_by_node_name  one priority group per target node\n"
		"          . group_by_tpg        one priority group per ALUA target port group\n"
		"  -v lvl  verbosity level:\n"
		"          . 0 no output\n"
		"          . 1 print created devmap names only\n"
		"          . 2 default verbosity\n"
		"          . 3 print debug information\n"
		"  device  action limited to:\n"
		"          . multipath named 'device' (ex: mpath0)\n"
		"          . multipath whose wwid is 'device' (ex: 60051...)\n"
		"          . multipath including the path named 'device' (ex: /dev/sda or\n"
		"            /dev/dm-0)\n"
		"          . multipath including the path with maj:min 'device' (ex: 8:0)\n"
		);

}

static int
get_dm_mpvec (enum mpath_cmds cmd, vector curmp, vector pathvec, char * refwwid)
{
	int i;
	struct multipath * mpp;
	int flags = (cmd == CMD_LIST_SHORT ? DI_NOIO : DI_ALL);

	if (dm_get_maps(curmp))
		return 1;

	vector_foreach_slot (curmp, mpp, i) {
		/*
		 * discard out of scope maps
		 */
		if (refwwid && strlen(refwwid) &&
		    strncmp(mpp->wwid, refwwid, WWID_SIZE)) {
			condlog(3, "skip map %s: out of scope", mpp->alias);
			remove_map(mpp, pathvec, curmp);
			i--;
			continue;
		}

		if (update_multipath_table(mpp, pathvec, flags) != DMP_OK) {
			condlog(1, "error parsing map %s", mpp->wwid);
			remove_map(mpp, pathvec, curmp);
			i--;
			continue;
		}

		if (cmd == CMD_LIST_LONG)
			mpp->bestpg = select_path_group(mpp);

		if (cmd == CMD_LIST_SHORT ||
		    cmd == CMD_LIST_LONG)
			print_multipath_topology(mpp, libmp_verbosity);

		if (cmd == CMD_CREATE)
			reinstate_paths(mpp);
	}

	if (cmd == CMD_LIST_SHORT || cmd == CMD_LIST_LONG)
		print_foreign_topology(libmp_verbosity);

	return 0;
}

static int check_usable_paths(struct config *conf,
			      const char *devpath, enum devtypes dev_type)
{
	struct udev_device *ud = NULL;
	struct multipath *mpp = NULL;
	struct pathgroup *pg;
	struct path *pp;
	char *mapname;
	vector pathvec = NULL;
	dev_t devt;
	int r = 1, i, j;

	ud = get_udev_device(devpath, dev_type);
	if (ud == NULL)
		return r;

	devt = udev_device_get_devnum(ud);
	if (!dm_is_dm_major(major(devt))) {
		condlog(1, "%s is not a dm device", devpath);
		goto out;
	}

	mapname = dm_mapname(major(devt), minor(devt));
	if (mapname == NULL) {
		condlog(1, "dm device not found: %s", devpath);
		goto out;
	}

	if (dm_is_mpath(mapname) != 1) {
		condlog(1, "%s is not a multipath map", devpath);
		goto free;
	}

	/* pathvec is needed for disassemble_map */
	pathvec = vector_alloc();
	if (pathvec == NULL)
		goto free;

	mpp = dm_get_multipath(mapname);
	if (mpp == NULL)
		goto free;

	if (update_multipath_table(mpp, pathvec, 0) != DMP_OK)
		    goto free;

	vector_foreach_slot (mpp->pg, pg, i) {
		vector_foreach_slot (pg->paths, pp, j) {
			pp->udev = get_udev_device(pp->dev_t, DEV_DEVT);
			if (pp->udev == NULL)
				continue;
			if (pathinfo(pp, conf, DI_SYSFS|DI_NOIO|DI_CHECKER) != PATHINFO_OK)
				continue;

			if (pp->state == PATH_UP &&
			    pp->dmstate == PSTATE_ACTIVE) {
				condlog(3, "%s: path %s is usable",
					devpath, pp->dev);
				r = 0;
				goto found;
			}
		}
	}
found:
	condlog(r == 0 ? 3 : 2, "%s:%s usable paths found",
		devpath, r == 0 ? "" : " no");
free:
	free(mapname);
	free_multipath(mpp, FREE_PATHS);
	vector_free(pathvec);
out:
	udev_device_unref(ud);
	return r;
}

enum {
	FIND_MULTIPATHS_WAIT_DONE = 0,
	FIND_MULTIPATHS_WAITING = 1,
	FIND_MULTIPATHS_ERROR = -1,
	FIND_MULTIPATHS_NEVER = -2,
};

static const char shm_find_mp_dir[] = MULTIPATH_SHM_BASE "find_multipaths";

/**
 * find_multipaths_check_timeout(wwid, tmo)
 * Helper for "find_multipaths smart"
 *
 * @param[in] pp: path to check / record
 * @param[in] tmo: configured timeout for this WWID, or value <= 0 for checking
 * @param[out] until: timestamp until we must wait, CLOCK_REALTIME, if return
 *             value is FIND_MULTIPATHS_WAITING
 * @returns: FIND_MULTIPATHS_WAIT_DONE, if waiting has finished
 * @returns: FIND_MULTIPATHS_ERROR, if internal error occurred
 * @returns: FIND_MULTIPATHS_NEVER, if tmo is 0 and we didn't wait for this
 *           device
 * @returns: FIND_MULTIPATHS_WAITING, if timeout hasn't expired
 */
static int find_multipaths_check_timeout(const struct path *pp, long tmo,
					 struct timespec *until)
{
	char path[PATH_MAX];
	struct timespec now, ftimes[2], tdiff;
	struct stat st;
	int fd;
	int r, retries = 0;

	clock_gettime(CLOCK_REALTIME, &now);

	if (safe_sprintf(path, "%s/%s", shm_find_mp_dir, pp->dev_t)) {
		condlog(1, "%s: path name overflow", __func__);
		return FIND_MULTIPATHS_ERROR;
	}

	if (ensure_directories_exist(path, 0700)) {
		condlog(1, "%s: error creating directories", __func__);
		return FIND_MULTIPATHS_ERROR;
	}

retry:
	fd = open(path, O_RDONLY);
	if (fd != -1) {
		r = fstat(fd, &st);
		close(fd);

	} else if (tmo > 0) {
		if (errno == ENOENT)
			fd = open(path, O_RDWR|O_EXCL|O_CREAT, 0644);
		if (fd == -1) {
			if (errno == EEXIST && !retries++)
				/* We could have raced with another process */
				goto retry;
			condlog(1, "%s: error opening %s: %s",
				__func__, path, strerror(errno));
			return FIND_MULTIPATHS_ERROR;
		};

		/*
		 * We just created the file. Set st_mtim to our desired
		 * expiry time.
		 */
		ftimes[0].tv_sec = 0;
		ftimes[0].tv_nsec = UTIME_OMIT;
		ftimes[1].tv_sec = now.tv_sec + tmo;
		ftimes[1].tv_nsec = now.tv_nsec;
		if (futimens(fd, ftimes) != 0) {
			condlog(1, "%s: error in futimens(%s): %s", __func__,
				path, strerror(errno));
		}
		r = fstat(fd, &st);
		close(fd);
	} else
		return FIND_MULTIPATHS_NEVER;

	if (r != 0) {
		condlog(1, "%s: error in fstat for %s: %m", __func__, path);
		return FIND_MULTIPATHS_ERROR;
	}

	timespecsub(&st.st_mtim, &now, &tdiff);

	if (tdiff.tv_sec <= 0)
		return FIND_MULTIPATHS_WAIT_DONE;

	*until = tdiff;
	return FIND_MULTIPATHS_WAITING;
}

static int print_cmd_valid(int k, const vector pathvec,
			   struct config *conf)
{
	int wait = FIND_MULTIPATHS_NEVER;
	struct timespec until;
	struct path *pp;

	if (k != PATH_IS_VALID && k != PATH_IS_NOT_VALID &&
	    k != PATH_IS_MAYBE_VALID)
		return PATH_IS_NOT_VALID;

	if (k == PATH_IS_MAYBE_VALID) {
		/*
		 * Caller ensures that pathvec[0] is the path to
		 * examine.
		 */
		pp = VECTOR_SLOT(pathvec, 0);
		select_find_multipaths_timeout(conf, pp);
		wait = find_multipaths_check_timeout(
			pp, pp->find_multipaths_timeout, &until);
		if (wait != FIND_MULTIPATHS_WAITING)
			k = PATH_IS_NOT_VALID;
	} else if (pathvec != NULL && (pp = VECTOR_SLOT(pathvec, 0)))
		wait = find_multipaths_check_timeout(pp, 0, &until);
	if (wait == FIND_MULTIPATHS_WAITING)
		printf("FIND_MULTIPATHS_WAIT_UNTIL=\"%ld.%06ld\"\n",
		       (long)until.tv_sec, until.tv_nsec/1000);
	else if (wait == FIND_MULTIPATHS_WAIT_DONE)
		printf("FIND_MULTIPATHS_WAIT_UNTIL=\"0\"\n");
	printf("DM_MULTIPATH_DEVICE_PATH=\"%d\"\n",
	       k == PATH_IS_MAYBE_VALID ? 2 : k == PATH_IS_VALID ? 1 : 0);
	/* Never return RTVL_MAYBE */
	return k == PATH_IS_NOT_VALID ? PATH_IS_NOT_VALID : PATH_IS_VALID;
}

/*
 * Returns true if this device has been handled before,
 * and released to systemd.
 *
 * This must be called before get_refwwid(),
 * otherwise udev_device_new_from_environment() will have
 * destroyed environ(7).
 */
static bool released_to_systemd(void)
{
	static const char dmdp[] = "DM_MULTIPATH_DEVICE_PATH";
	const char *dm_mp_dev_path = getenv(dmdp);
	bool ret;

	ret = dm_mp_dev_path != NULL && !strcmp(dm_mp_dev_path, "0");
	condlog(4, "%s: %s=%s -> %d", __func__, dmdp,
		dm_mp_dev_path ? dm_mp_dev_path : "", ret);
	return ret;
}

static struct vectors vecs;
static void cleanup_vecs(void)
{
	free_multipathvec(vecs.mpvec, KEEP_PATHS);
	free_pathvec(vecs.pathvec, FREE_PATHS);
}

static int
configure (struct config *conf, enum mpath_cmds cmd,
	   enum devtypes dev_type, char *devpath)
{
	vector curmp = NULL;
	vector pathvec = NULL;
	vector newmp = NULL;
	int r = RTVL_FAIL, rc;
	int di_flag = 0;
	char * refwwid = NULL;
	char * dev = NULL;
	fieldwidth_t *width __attribute__((cleanup(cleanup_ucharp))) = NULL;

	/*
	 * allocate core vectors to store paths and multipaths
	 */
	curmp = vector_alloc();
	pathvec = vector_alloc();
	newmp = vector_alloc();

	if (!curmp || !pathvec || !newmp) {
		condlog(0, "cannot allocate memory");
		goto out;
	}
	vecs.pathvec = pathvec;
	vecs.mpvec = curmp;

	dev = convert_dev(devpath, (dev_type == DEV_DEVNODE));

	/*
	 * if we have a blacklisted device parameter, exit early
	 */
	if (dev && (dev_type == DEV_DEVNODE ||
		    dev_type == DEV_UEVENT) &&
	    cmd != CMD_REMOVE_WWID &&
	    (filter_devnode(conf->blist_devnode,
			    conf->elist_devnode, dev) > 0)) {
		goto out;
	}

	/*
	 * scope limiting must be translated into a wwid
	 * failing the translation is fatal (by policy)
	 */
	if (devpath) {
		get_refwwid(cmd, devpath, dev_type, pathvec, &refwwid);
		if (!refwwid) {
			condlog(4, "%s: failed to get wwid", devpath);
			condlog(3, "scope is null");
			goto out;
		}
		if (cmd == CMD_REMOVE_WWID) {
			rc = remove_wwid(refwwid);
			if (rc == 0) {
				printf("wwid '%s' removed\n", refwwid);
				r = RTVL_OK;
			} else if (rc == 1) {
				printf("wwid '%s' not in wwids file\n",
					refwwid);
				r = RTVL_OK;
			}
			goto out;
		}
		if (cmd == CMD_ADD_WWID) {
			rc = remember_wwid(refwwid);
			if (rc >= 0) {
				printf("wwid '%s' added\n", refwwid);
				r = RTVL_OK;
			} else
				printf("failed adding '%s' to wwids file\n",
				       refwwid);
			goto out;
		}
		condlog(3, "scope limited to %s", refwwid);
	}

	/*
	 * get a path list
	 */
	if (devpath)
		di_flag = DI_WWID;

	if (cmd == CMD_LIST_LONG)
		/* extended path info '-ll' */
		di_flag |= DI_SYSFS | DI_CHECKER | DI_SERIAL;
	else if (cmd == CMD_LIST_SHORT)
		/* minimum path info '-l' */
		di_flag |= DI_SYSFS;
	else
		/* maximum info */
		di_flag = DI_ALL;

	if (path_discovery(pathvec, di_flag) < 0)
		goto out;

	if (libmp_verbosity > 2)
		print_all_paths(pathvec, 1);

	if ((width = alloc_path_layout()) == NULL)
		goto out;
	get_path_layout(pathvec, 0, width);
	foreign_path_layout(width);

	if (get_dm_mpvec(cmd, curmp, pathvec, refwwid))
		goto out;

	filter_pathvec(pathvec, refwwid);

	if (cmd == CMD_DUMP_CONFIG) {
		vector hwes = get_used_hwes(pathvec);

		dump_config(conf, hwes, curmp);
		vector_free(hwes);
		r = RTVL_OK;
		goto out;
	}

	if (cmd != CMD_CREATE && cmd != CMD_DRY_RUN) {
		r = RTVL_OK;
		goto out;
	}

	/*
	 * core logic entry point
	 */
	rc = coalesce_paths(&vecs, newmp, refwwid,
			   conf->force_reload, cmd);
	r = rc == CP_RETRY ? RTVL_RETRY : rc == CP_OK ? RTVL_OK : RTVL_FAIL;

out:
	if (r == RTVL_OK &&
	    (cmd == CMD_LIST_SHORT || cmd == CMD_LIST_LONG ||
	     cmd == CMD_CREATE) &&
	    (VECTOR_SIZE(curmp) > 0 || VECTOR_SIZE(newmp) > 0) &&
	    !check_daemon())
		condlog(2, "Warning: multipath devices exist, but multipathd service is not running");

	if (refwwid)
		free(refwwid);

	free_multipathvec(curmp, KEEP_PATHS);
	vecs.mpvec = NULL;
	free_multipathvec(newmp, KEEP_PATHS);
	free_pathvec(pathvec, FREE_PATHS);
	vecs.pathvec = NULL;

	return r;
}

static int
check_path_valid(const char *name, struct config *conf, bool is_uevent)
{
	int fd, r = PATH_IS_ERROR;
	struct path *pp;
	vector pathvec = NULL;
	const char *wwid;

	pp = alloc_path();
	if (!pp)
		return RTVL_FAIL;
	if (is_uevent)
		pp->can_use_env_uid = true;

	r = is_path_valid(name, conf, pp, is_uevent);
	if (r <= PATH_IS_ERROR || r >= PATH_MAX_VALID_RESULT)
		goto fail;

	/* set path values if is_path_valid() didn't */
	if (!pp->udev)
		pp->udev = udev_device_new_from_subsystem_sysname(udev, "block",
								  name);
	if (!pp->udev)
		goto fail;

	if (!strlen(pp->dev_t)) {
		dev_t devt = udev_device_get_devnum(pp->udev);
		if (major(devt) == 0 && minor(devt) == 0)
			goto fail;
		snprintf(pp->dev_t, BLK_DEV_SIZE, "%d:%d", major(devt),
			 minor(devt));
	}

	if ((r == PATH_IS_VALID || r == PATH_IS_MAYBE_VALID) &&
	    released_to_systemd())
		r = PATH_IS_NOT_VALID;

	/* This state is only used to skip the released_to_systemd() check */
	if (r == PATH_IS_VALID_NO_CHECK)
		r = PATH_IS_VALID;

	if (r != PATH_IS_MAYBE_VALID)
		goto out;

	/*
	 * If opening the path with O_EXCL fails, the path
	 * is in use (e.g. mounted during initramfs processing).
	 * We know that it's not used by dm-multipath.
	 * We may not set SYSTEMD_READY=0 on such devices, it
	 * might cause systemd to umount the device.
	 * Use O_RDONLY, because udevd would trigger another
	 * uevent for close-after-write.
	 *
	 * The O_EXCL check is potentially dangerous, because it may
	 * race with other tasks trying to access the device. Therefore
	 * this code is only executed if the path hasn't been released
	 * to systemd earlier (see above).
	 */
	fd = open(udev_device_get_devnode(pp->udev), O_RDONLY|O_EXCL);
	if (fd >= 0)
		close(fd);
	else {
		condlog(3, "%s: path %s is in use: %m", __func__, pp->dev);
		/* Check if we raced with multipathd */
		if (sysfs_is_multipathed(pp, false))
			r = PATH_IS_VALID;
		else
			r = PATH_IS_NOT_VALID;
		goto out;
	}

	pathvec = vector_alloc();
	if (!pathvec)
		goto fail;

	if (store_path(pathvec, pp) != 0) {
		free_path(pp);
		pp = NULL;
		goto fail;
	} else {
		/* make sure path isn't freed twice */
		wwid = pp->wwid;
		pp = NULL;
	}

	/* For find_multipaths = SMART, if there is more than one path
	 * matching the refwwid, then the path is valid */
	if (path_discovery(pathvec, DI_SYSFS | DI_WWID) < 0)
		goto fail;
	filter_pathvec(pathvec, wwid);
	if (VECTOR_SIZE(pathvec) > 1)
		r = PATH_IS_VALID;
	else
		r = PATH_IS_MAYBE_VALID;

out:
	r = print_cmd_valid(r, pathvec, conf);
	/*
	 * multipath -u must exit with status 0, otherwise udev won't
	 * import its output.
	 */
	if (!is_uevent && r == PATH_IS_NOT_VALID)
		r = RTVL_FAIL;
	else
		r = RTVL_OK;
	goto cleanup;

fail:
	r = RTVL_FAIL;

cleanup:
	if (pp != NULL)
		free_path(pp);
	if (pathvec != NULL)
		free_pathvec(pathvec, FREE_PATHS);
	return r;
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

/*
 * Some multipath commands are dangerous to run while multipathd is running.
 * For example, "multipath -r" may apply a modified configuration to the kernel,
 * while multipathd is still using the old configuration, leading to
 * inconsistent state.
 *
 * It is safer to use equivalent multipathd client commands instead.
 */
enum {
	DELEGATE_OK,
	DELEGATE_ERROR,
	DELEGATE_RETRY,
	NOT_DELEGATED,
};

int delegate_to_multipathd(enum mpath_cmds cmd,
			   __attribute__((unused)) const char *dev,
			   __attribute__((unused)) enum devtypes dev_type,
			   struct config *conf)
{
	int fd;
	char command[1024], *p, *reply = NULL;
	int n, r = DELEGATE_ERROR;

	p = command;
	*p = '\0';
	n = sizeof(command);

	if (conf->skip_delegate)
		return NOT_DELEGATED;

	if (cmd == CMD_CREATE && conf->force_reload == FORCE_RELOAD_YES) {
		p += snprintf(p, n, "reconfigure all");
	}
	else if (cmd == CMD_FLUSH_ONE && dev && dev_type == DEV_DEVMAP) {
		p += snprintf(p, n, "del map %s", dev);
		if (conf->remove_retries > 0) {
			r = DELEGATE_RETRY;
			conf->remove_retries--;
		}
	}
	else if (cmd == CMD_FLUSH_ALL) {
		p += snprintf(p, n, "del maps");
		if (conf->remove_retries > 0) {
			r = DELEGATE_RETRY;
			conf->remove_retries--;
		}
	}
	/* Add other translations here */

	if (strlen(command) == 0)
		/* No command found, no need to delegate */
		return NOT_DELEGATED;

	fd = mpath_connect();
	if (fd == -1)
		return NOT_DELEGATED;

	if (p >= command + sizeof(command)) {
		condlog(0, "internal error - command buffer overflow");
		goto out;
	}

	condlog(3, "delegating command to multipathd");

	if (mpath_process_cmd(fd, command, &reply, conf->uxsock_timeout)
	    == -1) {
		if (errno == ETIMEDOUT)
			r = NOT_DELEGATED;
		condlog(1, "error in multipath command %s: %s",
			command, strerror(errno));
		goto out;
	}

	if (reply != NULL && *reply != '\0') {
		if (strncmp(reply, "fail\n", 5))
			r = DELEGATE_OK;
		else if (strcmp(reply, "fail\ntimeout\n") == 0) {
			r = NOT_DELEGATED;
			goto out;
		}
		if (r != DELEGATE_RETRY && strcmp(reply, "ok\n")) {
			/* If there is additional failure information, skip the
			 * initial 'fail' */
			if (strncmp(reply, "fail\n", 5) == 0 &&
			    strlen(reply) > 5)
				printf("%s", reply + 5);
			else
				printf("%s", reply);
		}
	}

out:
	free(reply);
	close(fd);
	return r;
}

int
main (int argc, char *argv[])
{
	int arg;
	extern char *optarg;
	extern int optind;
	int r = RTVL_FAIL;
	enum mpath_cmds cmd = CMD_CREATE;
	enum devtypes dev_type = DEV_NONE;
	char *dev = NULL;
	struct config *conf;
	bool enable_foreign = false;
	int retrigger_tries_ori;
	int force_sync_ori;

	libmultipath_init();
	if (atexit(dm_lib_exit) || atexit(libmultipath_exit))
		condlog(1, "failed to register cleanup handler for libmultipath: %m");
	logsink = LOGSINK_STDERR_WITH_TIME;
	if (init_config(DEFAULT_CONFIGFILE))
		exit(RTVL_FAIL);
	if (atexit(uninit_config))
		condlog(1, "failed to register cleanup handler for config: %m");
	conf = get_multipath_config();
	retrigger_tries_ori = conf->retrigger_tries;
	conf->retrigger_tries = 0;
	force_sync_ori = conf->force_sync;
	conf->force_sync = 1;
	if (atexit(cleanup_vecs))
		condlog(1, "failed to register cleanup handler for vecs: %m");
	if (atexit(cleanup_bindings))
		condlog(1, "failed to register cleanup handler for bindings: %m");
	while ((arg = getopt(argc, argv, ":adDcChl::eFfM:v:p:b:BrR:itTquUwW")) != EOF ) {
		switch(arg) {
		case 'v':
			if (!isdigit(optarg[0])) {
				usage (argv[0]);
				exit(RTVL_FAIL);
			}

			libmp_verbosity = atoi(optarg);
			break;
		case 'b':
			condlog(1, "option -b ignored");
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
		case 'C':
			cmd = CMD_USABLE_PATHS;
			break;
		case 'd':
			if (cmd == CMD_CREATE)
				cmd = CMD_DRY_RUN;
			break;
		case 'D':
			conf->skip_delegate = 1;
			break;
		case 'f':
			cmd = CMD_FLUSH_ONE;
			break;
		case 'F':
			cmd = CMD_FLUSH_ALL;
			break;
		case 'l':
			if (optarg && !strncmp(optarg, "l", 1))
				cmd = CMD_LIST_LONG;
			else
				cmd = CMD_LIST_SHORT;

			break;
		case 'M':
			break;
		case 'p':
			conf->pgpolicy_flag = get_pgpolicy_id(optarg);
			if (conf->pgpolicy_flag == IOPOLICY_UNDEF) {
				printf("'%s' is not a valid policy\n", optarg);
				usage(argv[0]);
				exit(RTVL_FAIL);
			}
			break;
		case 'r':
			conf->force_reload = FORCE_RELOAD_YES;
			break;
		case 'i':
			if (conf->find_multipaths == FIND_MULTIPATHS_ON ||
			    conf->find_multipaths == FIND_MULTIPATHS_STRICT)
				conf->find_multipaths = FIND_MULTIPATHS_SMART;
			else if (conf->find_multipaths == FIND_MULTIPATHS_OFF)
				conf->find_multipaths = FIND_MULTIPATHS_GREEDY;
			break;
		case 't':
			conf->retrigger_tries = retrigger_tries_ori;
			conf->force_sync = force_sync_ori;
			r = dump_config(conf, NULL, NULL) ? RTVL_FAIL : RTVL_OK;
			goto out;
		case 'T':
			cmd = CMD_DUMP_CONFIG;
			conf->retrigger_tries = retrigger_tries_ori;
			conf->force_sync = force_sync_ori;
			break;
		case 'h':
			usage(argv[0]);
			exit(RTVL_OK);
		case 'u':
			cmd = CMD_VALID_PATH;
			dev_type = DEV_UEVENT;
			break;
		case 'U':
			cmd = CMD_USABLE_PATHS;
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
		case 'R':
			conf->remove_retries = atoi(optarg);
			break;
		case 'e':
			enable_foreign = true;
			break;
		case ':':
			fprintf(stderr, "Missing option argument\n");
			usage(argv[0]);
			exit(RTVL_FAIL);
		case '?':
			fprintf(stderr, "Unknown switch: %s\n", optarg);
			usage(argv[0]);
			exit(RTVL_FAIL);
		default:
			usage(argv[0]);
			exit(RTVL_FAIL);
		}
	}

	if (getuid() != 0) {
		fprintf(stderr, "need to be root\n");
		exit(RTVL_FAIL);
	}

	if (optind < argc) {
		dev = calloc(1, FILE_NAME_SIZE);

		if (!dev)
			goto out;

		strlcpy(dev, argv[optind], FILE_NAME_SIZE);
		if (dev_type != DEV_UEVENT)
			dev_type = get_dev_type(dev);
		if (dev_type == DEV_NONE) {
			condlog(0, "'%s' is not a valid argument\n", dev);
			goto out;
		}
		if (dev_type == DEV_DEVNODE || dev_type == DEV_DEVT)
			strchop(dev);
	}
	if (dev_type == DEV_UEVENT) {
		openlog("multipath", 0, LOG_DAEMON);
		setlogmask(LOG_UPTO(libmp_verbosity + 3));
		logsink = LOGSINK_SYSLOG;
	}

	set_max_fds(conf->max_fds);

	libmp_udev_set_sync_support(1);

	if ((cmd == CMD_LIST_SHORT || cmd == CMD_LIST_LONG) && enable_foreign)
		conf->enable_foreign = strdup("");

	if (cmd == CMD_USABLE_PATHS) {
		r = check_usable_paths(conf, dev, dev_type) ?
			RTVL_FAIL : RTVL_OK;
		goto out;
	}
	if (cmd == CMD_VALID_PATH &&
	    (!dev || dev_type == DEV_DEVMAP)) {
		condlog(0, "the -c option requires a path to check");
		goto out;
	}
	if (cmd == CMD_VALID_PATH) {
		char * name = convert_dev(dev, (dev_type == DEV_DEVNODE));
		r = check_path_valid(name, conf, dev_type == DEV_UEVENT);
		goto out;
	}

	if (cmd == CMD_REMOVE_WWID && !dev) {
		condlog(0, "the -w option requires a device");
		goto out;
	}
	if (cmd == CMD_FLUSH_ONE && dev_type != DEV_DEVMAP) {
		condlog(0, "the -f option requires a map name to remove");
		goto out;
	}

	while (1) {
		int ret = delegate_to_multipathd(cmd, dev, dev_type, conf);

		if (ret == DELEGATE_OK)
			exit(RTVL_OK);
		if (ret == DELEGATE_ERROR)
			exit(RTVL_FAIL);
		if (ret == DELEGATE_RETRY)
			sleep(1);
		else /* NOT_DELEGATED */
			break;
	}

	if (check_alias_settings(conf)) {
		fprintf(stderr, "fatal configuration error, aborting\n");
		exit(RTVL_FAIL);
	}

	if (init_checkers()) {
		condlog(0, "failed to initialize checkers");
		goto out;
	}
	if (init_prio()) {
		condlog(0, "failed to initialize prioritizers");
		goto out;
	}

	/* Failing here is non-fatal */
	init_foreign(conf->enable_foreign);

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
			r = replace_wwids(curmp) ? RTVL_FAIL : RTVL_OK;
		if (r == RTVL_OK)
			printf("successfully reset wwids\n");
		vector_foreach_slot_backwards(curmp, mpp, i) {
			vector_del_slot(curmp, i);
			free_multipath(mpp, KEEP_PATHS);
		}
		vector_free(curmp);
		goto out;
	}
	if (cmd == CMD_FLUSH_ONE) {
		if (dm_is_mpath(dev) != 1) {
			condlog(0, "%s is not a multipath device", dev);
			r = RTVL_FAIL;
			goto out;
		}
		r = (dm_suspend_and_flush_map(dev, conf->remove_retries) != DM_FLUSH_OK) ?
		    RTVL_FAIL : RTVL_OK;
		goto out;
	}
	else if (cmd == CMD_FLUSH_ALL) {
		r = (dm_flush_maps(conf->remove_retries) != DM_FLUSH_OK) ?
		    RTVL_FAIL : RTVL_OK;
		goto out;
	}
	while ((r = configure(conf, cmd, dev_type, dev)) == RTVL_RETRY)
		condlog(3, "restart multipath configuration process");

out:
	put_multipath_config(conf);
	if (dev)
		free(dev);

	if (dev_type == DEV_UEVENT)
		closelog();

	return r;
}
