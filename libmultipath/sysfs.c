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
 *	with this program; if not, write to the Free Software Foundation, Inc.,
 *	51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
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

#include "checkers.h"
#include "vector.h"
#include "structs.h"
#include "sysfs.h"
#include "list.h"
#include "util.h"
#include "debug.h"
#include "devmapper.h"

char sysfs_path[PATH_SIZE];

int sysfs_init(char *path, size_t len)
{
	if (path) {
		strlcpy(sysfs_path, path, len);
		remove_trailing_chars(sysfs_path, '/');
	} else
		strlcpy(sysfs_path, "/sys", sizeof(sysfs_path));
	dbg("sysfs_path='%s'", sysfs_path);

	return 0;
}

int sysfs_resolve_link(char *devpath, size_t size)
{
	char link_path[PATH_SIZE];
	char link_target[PATH_SIZE];
	int len;
	int i;
	int back;

	strlcpy(link_path, sysfs_path, sizeof(link_path));
	strlcat(link_path, devpath, sizeof(link_path));
	len = readlink(link_path, link_target, sizeof(link_target));
	if (len <= 0)
		return -1;
	link_target[len] = '\0';
	dbg("path link '%s' points to '%s'", devpath, link_target);

	for (back = 0; strncmp(&link_target[back * 3], "../", 3) == 0; back++)
		;
	dbg("base '%s', tail '%s', back %i", devpath, &link_target[back * 3], back);
	for (i = 0; i <= back; i++) {
		char *pos = strrchr(devpath, '/');

		if (pos == NULL)
			return -1;
		pos[0] = '\0';
	}
	dbg("after moving back '%s'", devpath);
	strlcat(devpath, "/", size);
	strlcat(devpath, &link_target[back * 3], size);
	return 0;
}

size_t sysfs_attr_get_value(const char *devpath, const char *attr_name,
			    char *attr_value, int attr_len)
{
	char path_full[PATH_SIZE];
	struct stat statbuf;
	int fd;
	ssize_t size;
	size_t sysfs_len;

	if (!attr_value || !attr_len)
		return 0;

	attr_value[0] = '\0';
	size = 0;

	dbg("open '%s'/'%s'", devpath, attr_name);
	sysfs_len = strlcpy(path_full, sysfs_path, sizeof(path_full));
	if(sysfs_len >= sizeof(path_full))
		sysfs_len = sizeof(path_full) - 1;

	strlcat(path_full, devpath, sizeof(path_full));
	strlcat(path_full, "/", sizeof(path_full));
	strlcat(path_full, attr_name, sizeof(path_full));

	if (stat(path_full, &statbuf) != 0) {
		dbg("stat '%s' failed: %s", path_full, strerror(errno));
		goto out;
	}

	/* skip directories */
	if (S_ISDIR(statbuf.st_mode))
		goto out;

	/* skip non-readable files */
	if ((statbuf.st_mode & S_IRUSR) == 0)
		goto out;

	/* read attribute value */
	fd = open(path_full, O_RDONLY);
	if (fd < 0) {
		dbg("attribute '%s' can not be opened: %s",
		    path_full, strerror(errno));
		goto out;
	}
	size = read(fd, attr_value, attr_len);
	close(fd);
	if (size < 0)
		goto out;
	if (size == attr_len) {
		dbg("overflow in attribute '%s', truncating", path_full);
		size--;
	}

	/* got a valid value, store and return it */
	attr_value[size] = '\0';
	remove_trailing_chars(attr_value, '\n');

out:
	return size;
}

ssize_t sysfs_attr_set_value(const char *devpath, const char *attr_name,
			     const char *value, int value_len)
{
	char path_full[PATH_SIZE];
	struct stat statbuf;
	int fd;
	ssize_t size = -1;
	size_t sysfs_len;

	if (!attr_name || !value || !value_len)
		return 0;

	dbg("open '%s'/'%s'", devpath, attr_name);
	sysfs_len = snprintf(path_full, PATH_SIZE, "%s%s/%s", sysfs_path,
			     devpath, attr_name);
	if (sysfs_len >= PATH_SIZE || sysfs_len < 0) {
		if (sysfs_len < 0)
			dbg("cannot copy sysfs path %s%s/%s : %s", sysfs_path,
			    devpath, attr_name, strerror(errno));
		else
			dbg("sysfs_path %s%s/%s too large", sysfs_path,
			    devpath, attr_name);
		goto out;
	}

	if (stat(path_full, &statbuf) != 0) {
		dbg("stat '%s' failed: %s", path_full, strerror(errno));
		goto out;
	}

	/* skip directories */
	if (S_ISDIR(statbuf.st_mode))
		goto out;

	/* skip non-writeable files */
	if ((statbuf.st_mode & S_IWUSR) == 0)
		goto out;

	/* write attribute value */
	fd = open(path_full, O_WRONLY);
	if (fd < 0) {
		dbg("attribute '%s' can not be opened: %s",
		    path_full, strerror(errno));
		goto out;
	}
	size = write(fd, value, value_len);
	if (size < 0)
		dbg("write to %s failed: %s", path_full, strerror(errno));
	else if (size < value_len) {
		dbg("tried to write %d to %s. Wrote %d\n", value_len,
		    path_full, size);
		size = -1;
	}

	close(fd);
out:

	return size;
}

int sysfs_check_holders(char * check_devt, char * new_devt)
{
	unsigned int major, new_minor, table_minor;
	char path[PATH_SIZE], check_dev[PATH_SIZE];
	char * table_name;
	DIR *dirfd;
	struct dirent *holder;

	if (sscanf(new_devt,"%d:%d", &major, &new_minor) != 2) {
		condlog(1, "invalid device number %s", new_devt);
		return 0;
	}

	if (devt2devname(check_dev, PATH_SIZE, check_devt))
		return 0;

	condlog(3, "%s: checking holder", check_dev);

	snprintf(path, PATH_SIZE, "/sys/block/%s/holders", check_dev);
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

		condlog(3, "%s: reassign table %s old %s new %s", check_dev,
			table_name, check_devt, new_devt);

		dm_reassign_table(table_name, check_devt, new_devt);
		FREE(table_name);
	}
	closedir(dirfd);

	return 0;
}
