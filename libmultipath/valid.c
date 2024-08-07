/*
  Copyright (c) 2020 Benjamin Marzinski, IBM

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <stddef.h>
#include <errno.h>
#include <libudev.h>
#include <dirent.h>
#include <libmount/libmount.h>

#include "vector.h"
#include "config.h"
#include "debug.h"
#include "util.h"
#include "devmapper.h"
#include "discovery.h"
#include "wwids.h"
#include "sysfs.h"
#include "blacklist.h"
#include "mpath_cmd.h"
#include "valid.h"

static int subdir_filter(const struct dirent *ent)
{
	unsigned int j;
	static char const *const skip[] = {
		".",
		"..",
		"holders",
		"integrity",
		"mq",
		"power",
		"queue",
		"slaves",
		"trace",
	};

	if (ent->d_type != DT_DIR)
		return 0;

	for (j = 0; j < ARRAY_SIZE(skip); j++)
		if (!strcmp(skip[j], ent->d_name))
			return 0;
	return 1;
}

static int read_partitions(const char *syspath, vector parts)
{
	struct scandir_result sr = { .n = 0 };
	char path[PATH_MAX], *last;
	char *prop;
	int i;

	strlcpy(path, syspath, sizeof(path));
	sr.n = scandir(path, &sr.di, subdir_filter, NULL);
	if (sr.n == -1)
		return -errno;

	pthread_cleanup_push_cast(free_scandir_result, &sr);

	/* parts[0] is the whole disk */
	if ((prop = strdup(strrchr(path, '/') + 1)) != NULL) {
		if (vector_alloc_slot(parts))
			vector_set_slot(parts, prop);
		else
			free(prop);
	}

	last = path + strlen(path);
	for (i = 0; i < sr.n; i++) {
		struct stat st;

		/* only add dirs that have the "partition" attribute */
		snprintf(last, sizeof(path) - (last - path), "/%s/partition",
			 sr.di[i]->d_name);

		if (stat(path, &st) == 0 &&
		    (prop = strdup(sr.di[i]->d_name)) != NULL) {
			if (vector_alloc_slot(parts))
				vector_set_slot(parts, prop);
			else
				free(prop);
		}
	}

	pthread_cleanup_pop(1);
	return 0;
}

static int no_dots(const struct dirent *ent)
{
	const char *name = ent->d_name;

	if (name[0] == '.' &&
	    (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
		return 0;
	return 1;
}

static int check_holders(const char *syspath)
{
	struct scandir_result __attribute__((cleanup(free_scandir_result)))
		sr = { .n = 0 };

	sr.n = scandir(syspath, &sr.di, no_dots, NULL);
	if (sr.n > 0)
		condlog(4, "%s: found holders under %s", __func__, syspath);
	return sr.n;
}

static int check_all_holders(const struct _vector *parts)
{
	char syspath[PATH_MAX];
	const char *sysname;
	unsigned int j;

	if (VECTOR_SIZE(parts) == 0)
		return 0;

	if (safe_sprintf(syspath, "/sys/class/block/%s/holders",
			 (const char *)VECTOR_SLOT(parts, 0)))
		return -EOVERFLOW;

	if (check_holders(syspath) > 0)
		return 1;

	j = 1;
	vector_foreach_slot_after(parts, sysname, j) {
		if (safe_sprintf(syspath, "/sys/class/block/%s/%s/holders",
				 (const char *)VECTOR_SLOT(parts, 0), sysname))
			return -EOVERFLOW;
		if (check_holders(syspath) > 0)
			return 1;
	}
	return 0;
}

static void cleanup_table(void *arg)
{
	if (arg)
		mnt_free_table((struct libmnt_table *)arg);
}

static void cleanup_cache(void *arg)
{
	if (arg)
#ifdef LIBMOUNT_HAS_MNT_UNREF_CACHE
		mnt_unref_cache((struct libmnt_cache *)arg);
#else
		mnt_free_cache((struct libmnt_cache *)arg);
#endif
}

/*
 * Passed a vector of partitions and a libmount table,
 * check if any of the partitions in the vector is referenced in the table.
 * Note that mnt_table_find_srcpath() also resolves mounts by symlinks.
 */
static int check_mnt_table(const struct _vector *parts,
			   struct libmnt_table *tbl,
			   const char *table_name)
{
	unsigned int i;
	const char *sysname;
	char devpath[PATH_MAX];

	vector_foreach_slot(parts, sysname, i) {
		if (!safe_sprintf(devpath, "/dev/%s", sysname) &&
		    mnt_table_find_srcpath(tbl, devpath,
					   MNT_ITER_FORWARD) != NULL) {
			condlog(4, "%s: found %s in %s", __func__,
				sysname, table_name);
			return 1;
		}
	}
	return 0;
}

static int check_mountinfo(const struct _vector *parts)
{
	static const char mountinfo[] = "/proc/self/mountinfo";
	struct libmnt_table *tbl;
	struct libmnt_cache *cache;
	FILE *stream;
	int used = 0, ret;

	tbl = mnt_new_table();
	if (!tbl )
		return -errno;

	pthread_cleanup_push(cleanup_table, tbl);
	cache = mnt_new_cache();
	if (cache) {
		pthread_cleanup_push(cleanup_cache, cache);
		if (mnt_table_set_cache(tbl, cache) == 0) {
			stream = fopen(mountinfo, "r");
			if (stream != NULL) {
				pthread_cleanup_push(cleanup_fclose, stream);
				ret = mnt_table_parse_stream(tbl, stream, mountinfo);
				pthread_cleanup_pop(1);

				if (ret == 0)
					used = check_mnt_table(parts, tbl,
							       "mountinfo");
			}
		}
		pthread_cleanup_pop(1);
	}
	pthread_cleanup_pop(1);
	return used;
}

#ifdef LIBMOUNT_SUPPORTS_SWAP
static int check_swaps(const struct _vector *parts)
{
	struct libmnt_table *tbl;
	struct libmnt_cache *cache;
	int used = 0, ret;

	tbl = mnt_new_table();
	if (!tbl )
		return -errno;

	pthread_cleanup_push(cleanup_table, tbl);
	cache = mnt_new_cache();
	if (cache) {
		pthread_cleanup_push(cleanup_cache, cache);
		if (mnt_table_set_cache(tbl, cache) == 0) {
			ret = mnt_table_parse_swaps(tbl, NULL);
			if (ret == 0)
				used = check_mnt_table(parts, tbl, "swaps");
		}
		pthread_cleanup_pop(1);
	}
	pthread_cleanup_pop(1);
	return used;
}
#else
static int check_swaps(const struct _vector *parts __attribute__((unused)))
{
	return 0;
}
#endif


/*
 * Given a block device, check if the device itself or any of its
 * partitions is in use
 * - by sysfs holders (e.g. LVM)
 * - mounted according to /proc/self/mountinfo
 * - used as swap
 */
static int is_device_in_use(struct udev_device *udevice)
{
	const char *syspath;
	vector parts;
	int used = 0, ret;

	syspath = udev_device_get_syspath(udevice);
	if (!syspath)
		return -ENOMEM;

	parts = vector_alloc();
	if (!parts)
		return -ENOMEM;

	pthread_cleanup_push_cast(free_strvec, parts);
	if ((ret = read_partitions(syspath, parts)) == 0)
		used =  check_all_holders(parts) > 0 ||
			check_mountinfo(parts) > 0 ||
			check_swaps(parts) > 0;
	pthread_cleanup_pop(1);

	if (ret < 0)
		return ret;

	condlog(3, "%s: %s is %sin use", __func__, syspath, used ? "" : "not ");
	return used;
}

int
is_path_valid(const char *name, struct config *conf, struct path *pp,
	      bool check_multipathd)
{
	int r;
	int fd;
	const char *prop;

	if (!pp || !name || !conf)
		return PATH_IS_ERROR;

	if (conf->find_multipaths <= FIND_MULTIPATHS_UNDEF ||
	    conf->find_multipaths >= __FIND_MULTIPATHS_LAST)
		return PATH_IS_ERROR;

	if (safe_sprintf(pp->dev, "%s", name))
		return PATH_IS_ERROR;

	if (sysfs_is_multipathed(pp, true)) {
		if (pp->wwid[0] == '\0')
			return PATH_IS_ERROR;
		return PATH_IS_VALID_NO_CHECK;
	}

	if (check_multipathd) {
		fd = mpath_connect__(1);
		if (fd < 0) {
			if (errno != EAGAIN) {
				condlog(3, "multipathd not running");
				return PATH_IS_NOT_VALID;
			}
		} else
			mpath_disconnect(fd);
	}

	pp->udev = udev_device_new_from_subsystem_sysname(udev, "block", name);
	if (!pp->udev)
		return PATH_IS_ERROR;

	prop = udev_device_get_property_value(pp->udev, "DEVTYPE");
	if (prop == NULL || strcmp(prop, "disk"))
		return PATH_IS_NOT_VALID;

	r = pathinfo(pp, conf, DI_SYSFS | DI_WWID | DI_BLACKLIST);
	if (r == PATHINFO_SKIPPED)
		return PATH_IS_NOT_VALID;
	else if (r)
		return PATH_IS_ERROR;

	if (pp->wwid[0] == '\0')
		return PATH_IS_NOT_VALID;

	r = is_failed_wwid(pp->wwid);
	if (r != WWID_IS_NOT_FAILED) {
		if (r == WWID_IS_FAILED)
			return PATH_IS_NOT_VALID;
		return PATH_IS_ERROR;
	}

	if ((conf->find_multipaths == FIND_MULTIPATHS_GREEDY ||
	     conf->find_multipaths == FIND_MULTIPATHS_SMART) &&
	    is_device_in_use(pp->udev) > 0)
		return PATH_IS_NOT_VALID;

	if (conf->find_multipaths == FIND_MULTIPATHS_GREEDY)
		return PATH_IS_VALID;

	if (check_wwids_file(pp->wwid, 0) == 0)
		return PATH_IS_VALID_NO_CHECK;

	if (dm_find_map_by_wwid(pp->wwid, NULL, NULL) == DMP_OK)
		return PATH_IS_VALID;

	/* all these act like FIND_MULTIPATHS_STRICT for finding if a
	 * path is valid */
	if (conf->find_multipaths != FIND_MULTIPATHS_SMART)
		return PATH_IS_NOT_VALID;

	return PATH_IS_MAYBE_VALID;
}
