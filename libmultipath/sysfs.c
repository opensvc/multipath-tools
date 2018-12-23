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

/*
 * When we modify an attribute value we cannot rely on libudev for now,
 * as libudev lacks the capability to update an attribute value.
 * So for modified attributes we need to implement our own function.
 */
ssize_t sysfs_attr_get_value(struct udev_device *dev, const char *attr_name,
			     char * value, size_t value_len)
{
	char devpath[PATH_SIZE];
	struct stat statbuf;
	int fd;
	ssize_t size = -1;

	if (!dev || !attr_name || !value)
		return 0;

	snprintf(devpath, PATH_SIZE, "%s/%s", udev_device_get_syspath(dev),
		 attr_name);
	condlog(4, "open '%s'", devpath);
	/* read attribute value */
	fd = open(devpath, O_RDONLY);
	if (fd < 0) {
		condlog(4, "attribute '%s' can not be opened: %s",
			devpath, strerror(errno));
		return -errno;
	}
	if (fstat(fd, &statbuf) < 0) {
		condlog(4, "stat '%s' failed: %s", devpath, strerror(errno));
		close(fd);
		return -ENXIO;
	}
	/* skip directories */
	if (S_ISDIR(statbuf.st_mode)) {
		condlog(4, "%s is a directory", devpath);
		close(fd);
		return -EISDIR;
	}
	/* skip non-writeable files */
	if ((statbuf.st_mode & S_IRUSR) == 0) {
		condlog(4, "%s is not readable", devpath);
		close(fd);
		return -EPERM;
	}

	size = read(fd, value, value_len);
	if (size < 0) {
		condlog(4, "read from %s failed: %s", devpath, strerror(errno));
		size = -errno;
		value[0] = '\0';
	} else if (size == value_len) {
		value[size - 1] = '\0';
		condlog(4, "overflow while reading from %s", devpath);
		size = 0;
	} else {
		value[size] = '\0';
		size = strchop(value);
	}

	close(fd);
	return size;
}

ssize_t sysfs_bin_attr_get_value(struct udev_device *dev, const char *attr_name,
				 unsigned char * value, size_t value_len)
{
	char devpath[PATH_SIZE];
	struct stat statbuf;
	int fd;
	ssize_t size = -1;

	if (!dev || !attr_name || !value)
		return 0;

	snprintf(devpath, PATH_SIZE, "%s/%s", udev_device_get_syspath(dev),
		 attr_name);
	condlog(4, "open '%s'", devpath);
	/* read attribute value */
	fd = open(devpath, O_RDONLY);
	if (fd < 0) {
		condlog(4, "attribute '%s' can not be opened: %s",
			devpath, strerror(errno));
		return -errno;
	}
	if (fstat(fd, &statbuf) != 0) {
		condlog(4, "stat '%s' failed: %s", devpath, strerror(errno));
		close(fd);
		return -ENXIO;
	}

	/* skip directories */
	if (S_ISDIR(statbuf.st_mode)) {
		condlog(4, "%s is a directory", devpath);
		close(fd);
		return -EISDIR;
	}

	/* skip non-writeable files */
	if ((statbuf.st_mode & S_IRUSR) == 0) {
		condlog(4, "%s is not readable", devpath);
		close(fd);
		return -EPERM;
	}

	size = read(fd, value, value_len);
	if (size < 0) {
		condlog(4, "read from %s failed: %s", devpath, strerror(errno));
		size = -errno;
	} else if (size == value_len) {
		condlog(4, "overflow while reading from %s", devpath);
		size = 0;
	}

	close(fd);
	return size;
}

ssize_t sysfs_attr_set_value(struct udev_device *dev, const char *attr_name,
			     const char * value, size_t value_len)
{
	char devpath[PATH_SIZE];
	struct stat statbuf;
	int fd;
	ssize_t size = -1;

	if (!dev || !attr_name || !value || !value_len)
		return 0;

	snprintf(devpath, PATH_SIZE, "%s/%s", udev_device_get_syspath(dev),
		 attr_name);
	condlog(4, "open '%s'", devpath);
	/* write attribute value */
	fd = open(devpath, O_WRONLY);
	if (fd < 0) {
		condlog(4, "attribute '%s' can not be opened: %s",
			devpath, strerror(errno));
		return -errno;
	}
	if (fstat(fd, &statbuf) != 0) {
		condlog(4, "stat '%s' failed: %s", devpath, strerror(errno));
		close(fd);
		return -errno;
	}

	/* skip directories */
	if (S_ISDIR(statbuf.st_mode)) {
		condlog(4, "%s is a directory", devpath);
		close(fd);
		return -EISDIR;
	}

	/* skip non-writeable files */
	if ((statbuf.st_mode & S_IWUSR) == 0) {
		condlog(4, "%s is not writeable", devpath);
		close(fd);
		return -EPERM;
	}

	size = write(fd, value, value_len);
	if (size < 0) {
		condlog(4, "write to %s failed: %s", devpath, strerror(errno));
		size = -errno;
	} else if (size < value_len) {
		condlog(4, "tried to write %ld to %s. Wrote %ld",
			(long)value_len, devpath, (long)size);
		size = 0;
	}

	close(fd);
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
	if (sysfs_attr_get_value(pp->udev, "size", attr, 255) <= 0) {
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

int sysfs_check_holders(char * check_devt, char * new_devt)
{
	unsigned int major, new_minor, table_minor;
	char path[PATH_MAX], check_dev[PATH_SIZE];
	char * table_name;
	DIR *dirfd;
	struct dirent *holder;

	if (sscanf(new_devt,"%d:%d", &major, &new_minor) != 2) {
		condlog(1, "invalid device number %s", new_devt);
		return 0;
	}

	if (devt2devname(check_dev, PATH_SIZE, check_devt)) {
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

		condlog(0, "%s: reassign table %s old %s new %s", check_dev,
			table_name, check_devt, new_devt);

		dm_reassign_table(table_name, check_devt, new_devt);
		FREE(table_name);
	}
	closedir(dirfd);

	return 0;
}

static int select_dm_devs(const struct dirent *di)
{
	return fnmatch("dm-*", di->d_name, FNM_FILE_NAME) == 0;
}

bool sysfs_is_multipathed(const struct path *pp)
{
	char pathbuf[PATH_MAX];
	struct scandir_result sr;
	struct dirent **di;
	int n, r, i;
	bool found = false;

	n = snprintf(pathbuf, sizeof(pathbuf), "/sys/block/%s/holders",
		     pp->dev);

	if (n >= sizeof(pathbuf)) {
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
		long fd;
		int nr;
		char uuid[6];

		if (snprintf(pathbuf + n, sizeof(pathbuf) - n,
			     "/%s/dm/uuid", di[i]->d_name)
		    >= sizeof(pathbuf) - n)
			continue;

		fd = open(pathbuf, O_RDONLY);
		if (fd == -1) {
			condlog(1, "%s: error opening %s", __func__, pathbuf);
			continue;
		}

		pthread_cleanup_push(close_fd, (void *)fd);
		nr = read(fd, uuid, sizeof(uuid));
		if (nr == sizeof(uuid) && !memcmp(uuid, "mpath-", sizeof(uuid)))
			found = true;
		else if (nr < 0) {
			condlog(1, "%s: error reading from %s: %s",
				__func__, pathbuf, strerror(errno));
		}
		pthread_cleanup_pop(1);
	}
	pthread_cleanup_pop(1);

	return found;
}
