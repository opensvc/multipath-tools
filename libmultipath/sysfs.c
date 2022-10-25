/*
 * Copyright (C) 2005-2006 Kay Sievers <kay.sievers@vrfy.org>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation version 2 of the License.
 *
 *	This program is distributed in the hope that it will be useful, but
 *	WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *	General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License along
 *	with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */


#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>
#include <dirent.h>
#include <libudev.h>
#include <fnmatch.h>
#include <limits.h>

#include "checkers.h"
#include "vector.h"
#include "structs.h"
#include "sysfs.h"
#include "list.h"
#include "util.h"
#include "debug.h"
#include "devmapper.h"
#include "config.h"

/*
 * When we modify an attribute value we cannot rely on libudev for now,
 * as libudev lacks the capability to update an attribute value.
 * So for modified attributes we need to implement our own function.
 */
static ssize_t __sysfs_attr_get_value(struct udev_device *dev, const char *attr_name,
				      char *value, size_t value_len, bool binary)
{
	const char *syspath;
	char devpath[PATH_MAX];
	int fd = -1;
	ssize_t size = -1;

	if (!dev || !attr_name || !value || !value_len) {
		condlog(1, "%s: invalid parameters", __func__);
		return -EINVAL;
	}

	syspath = udev_device_get_syspath(dev);
	if (!syspath) {
		condlog(3, "%s: invalid udevice", __func__);
		return -EINVAL;
	}
	if (safe_sprintf(devpath, "%s/%s", syspath, attr_name)) {
		condlog(3, "%s: devpath overflow", __func__);
		return -EOVERFLOW;
	}
	condlog(4, "open '%s'", devpath);
	/* read attribute value */
	fd = open(devpath, O_RDONLY);
	if (fd < 0) {
		condlog(3, "%s: attribute '%s' can not be opened: %s",
			__func__, devpath, strerror(errno));
		return -errno;
	}
	pthread_cleanup_push(cleanup_fd_ptr, &fd);

	size = read(fd, value, value_len);
	if (size < 0) {
		size = -errno;
		condlog(3, "%s: read from %s failed: %s", __func__, devpath,
			strerror(errno));
		if (!binary)
			value[0] = '\0';
	} else if (!binary && size == (ssize_t)value_len) {
		condlog(3, "%s: overflow reading from %s (required len: %zu)",
			__func__, devpath, size);
		value[size - 1] = '\0';
	} else if (!binary) {
		value[size] = '\0';
		size = strchop(value);
	}

	pthread_cleanup_pop(1);
	return size;
}

ssize_t sysfs_attr_get_value(struct udev_device *dev, const char *attr_name,
			     char *value, size_t value_len)
{
	return __sysfs_attr_get_value(dev, attr_name, value, value_len, false);
}

ssize_t sysfs_bin_attr_get_value(struct udev_device *dev, const char *attr_name,
				 unsigned char *value, size_t value_len)
{
	return __sysfs_attr_get_value(dev, attr_name, (char *)value,
				      value_len, true);
}

ssize_t sysfs_attr_set_value(struct udev_device *dev, const char *attr_name,
			     const char * value, size_t value_len)
{
	const char *syspath;
	char devpath[PATH_MAX];
	int fd = -1;
	ssize_t size = -1;

	if (!dev || !attr_name || !value || !value_len) {
		condlog(1, "%s: invalid parameters", __func__);
		return -EINVAL;
	}

	syspath = udev_device_get_syspath(dev);
	if (!syspath) {
		condlog(3, "%s: invalid udevice", __func__);
		return -EINVAL;
	}
	if (safe_sprintf(devpath, "%s/%s", syspath, attr_name)) {
		condlog(3, "%s: devpath overflow", __func__);
		return -EOVERFLOW;
	}

	condlog(4, "open '%s'", devpath);
	/* write attribute value */
	fd = open(devpath, O_WRONLY);
	if (fd < 0) {
		condlog(3, "%s: attribute '%s' can not be opened: %s",
			__func__, devpath, strerror(errno));
		return -errno;
	}
	pthread_cleanup_push(cleanup_fd_ptr, &fd);

	size = write(fd, value, value_len);
	if (size < 0) {
		size = -errno;
		condlog(3, "%s: write to %s failed: %s", __func__, 
			devpath, strerror(errno));
	} else if (size < (ssize_t)value_len)
		condlog(3, "%s: underflow writing %zu bytes to %s. Wrote %zd bytes",
			__func__, value_len, devpath, size);

	pthread_cleanup_pop(1);
	return size;
}

int
sysfs_get_size (struct path *pp, unsigned long long * size)
{
	char attr[255];
	int r;

	if (!pp->udev || !size)
		return 1;

	attr[0] = '\0';
	if (!sysfs_attr_get_value_ok(pp->udev, "size", attr, sizeof(attr))) {
		condlog(3, "%s: No size attribute in sysfs", pp->dev);
		return 1;
	}

	r = sscanf(attr, "%llu\n", size);

	if (r != 1) {
		condlog(3, "%s: Cannot parse size attribute", pp->dev);
		*size = 0;
		return 1;
	}

	return 0;
}

int devt2devname(char *devname, int devname_len, const char *devt)
{
	struct udev_device *u_dev;
	const char * dev_name;
	int r;

	if (!devname || !devname_len || !devt)
		return 1;

	u_dev = udev_device_new_from_devnum(udev, 'b', parse_devt(devt));
	if (!u_dev) {
		condlog(0, "\"%s\": invalid major/minor numbers, not found in sysfs", devt);
		return 1;
	}

	dev_name = udev_device_get_sysname(u_dev);
	if (!dev_name) {
		udev_device_unref(u_dev);
		return 1;
	}
	r = strlcpy(devname, dev_name, devname_len);
	udev_device_unref(u_dev);

	return !(r < devname_len);
}

int sysfs_check_holders(char * check_devt, char * new_devt)
{
	unsigned int major, new_minor, table_minor;
	char path[PATH_MAX], check_dev[FILE_NAME_SIZE];
	char * table_name;
	DIR *dirfd;
	struct dirent *holder;

	if (sscanf(new_devt,"%d:%d", &major, &new_minor) != 2) {
		condlog(1, "invalid device number %s", new_devt);
		return 0;
	}

	if (devt2devname(check_dev, sizeof(check_dev), check_devt)) {
		condlog(1, "can't get devname for %s", check_devt);
		return 0;
	}

	condlog(3, "%s: checking holder", check_dev);

	snprintf(path, sizeof(path), "/sys/block/%s/holders", check_dev);
	dirfd = opendir(path);
	if (dirfd == NULL) {
		condlog(3, "%s: failed to open directory %s (%d)",
			check_dev, path, errno);
		return 0;
	}
	while ((holder = readdir(dirfd)) != NULL) {
		if ((strcmp(holder->d_name,".") == 0) ||
		    (strcmp(holder->d_name,"..") == 0))
			continue;

		if (sscanf(holder->d_name, "dm-%d", &table_minor) != 1) {
			condlog(3, "%s: %s is not a dm-device",
				check_dev, holder->d_name);
			continue;
		}
		if (table_minor == new_minor) {
			condlog(3, "%s: holder already correct", check_dev);
			continue;
		}
		table_name = dm_mapname(major, table_minor);
		if (!table_name) {
			condlog(2, "%s: mapname not found for %d:%d", check_dev,
				major, table_minor);
			continue;
		}
		condlog(0, "%s: reassign table %s old %s new %s", check_dev,
			table_name, check_devt, new_devt);

		dm_reassign_table(table_name, check_devt, new_devt);
		free(table_name);
	}
	closedir(dirfd);

	return 0;
}

static int select_dm_devs(const struct dirent *di)
{
	return fnmatch("dm-*", di->d_name, FNM_FILE_NAME) == 0;
}

bool sysfs_is_multipathed(struct path *pp, bool set_wwid)
{
	char pathbuf[PATH_MAX];
	struct scandir_result sr;
	struct dirent **di;
	int n, r, i;
	bool found = false;

	n = snprintf(pathbuf, sizeof(pathbuf), "/sys/block/%s/holders",
		     pp->dev);

	if (n < 0 || (size_t)n >= sizeof(pathbuf)) {
		condlog(1, "%s: pathname overflow", __func__);
		return false;
	}

	r = scandir(pathbuf, &di, select_dm_devs, alphasort);
	if (r == 0)
		return false;
	else if (r < 0) {
		condlog(1, "%s: error scanning %s", __func__, pathbuf);
		return false;
	}

	sr.di = di;
	sr.n = r;
	pthread_cleanup_push_cast(free_scandir_result, &sr);
	for (i = 0; i < r && !found; i++) {
		int fd = -1;
		int nr;
		char uuid[WWID_SIZE + UUID_PREFIX_LEN];

		if (safe_snprintf(pathbuf + n, sizeof(pathbuf) - n,
				  "/%s/dm/uuid", di[i]->d_name))
			continue;

		fd = open(pathbuf, O_RDONLY);
		if (fd == -1) {
			condlog(1, "%s: error opening %s", __func__, pathbuf);
			continue;
		}

		pthread_cleanup_push(cleanup_fd_ptr, &fd);
		nr = read(fd, uuid, sizeof(uuid));
		if (nr > (int)UUID_PREFIX_LEN &&
		    !memcmp(uuid, UUID_PREFIX, UUID_PREFIX_LEN)) {
			found = true;
			if (set_wwid) {
				nr -= UUID_PREFIX_LEN;
				memcpy(pp->wwid, uuid + UUID_PREFIX_LEN, nr);
				if (nr == WWID_SIZE) {
					condlog(4, "%s: overflow while reading from %s",
						__func__, pathbuf);
					pp->wwid[0] = '\0';
				} else {
					pp->wwid[nr] = '\0';
					strchop(pp->wwid);
				}
			}
		} else if (nr < 0)
			condlog(1, "%s: error reading from %s: %m",
				__func__, pathbuf);

		pthread_cleanup_pop(1);
	}
	pthread_cleanup_pop(1);

	return found;
}
