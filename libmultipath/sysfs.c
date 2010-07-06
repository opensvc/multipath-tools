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

#include "checkers.h"
#include "vector.h"
#include "structs.h"
#include "sysfs.h"
#include "list.h"
#include "util.h"
#include "debug.h"

char sysfs_path[PATH_SIZE];

/* attribute value cache */
static LIST_HEAD(attr_list);
struct sysfs_attr {
	struct list_head node;
	char path[PATH_SIZE];
	char *value;			/* points to value_local if value is cached */
	char value_local[NAME_SIZE];
};

/* list of sysfs devices */
static LIST_HEAD(sysfs_dev_list);
struct sysfs_dev {
	struct list_head node;
	struct sysfs_device dev;
};

int sysfs_init(char *path, size_t len)
{
	if (path) {
		strlcpy(sysfs_path, path, len);
		remove_trailing_chars(sysfs_path, '/');
	} else
		strlcpy(sysfs_path, "/sys", sizeof(sysfs_path));
	dbg("sysfs_path='%s'", sysfs_path);

	INIT_LIST_HEAD(&attr_list);
	INIT_LIST_HEAD(&sysfs_dev_list);
	return 0;
}

void sysfs_cleanup(void)
{
	struct sysfs_attr *attr_loop;
	struct sysfs_attr *attr_temp;

	struct sysfs_dev *sysdev_loop;
	struct sysfs_dev *sysdev_temp;

	list_for_each_entry_safe(attr_loop, attr_temp, &attr_list, node) {
		list_del(&attr_loop->node);
		free(attr_loop);
	}

	list_for_each_entry_safe(sysdev_loop, sysdev_temp, &sysfs_dev_list, node) {
		list_del(&sysdev_loop->node);
		free(sysdev_loop);
	}
}

void sysfs_device_set_values(struct sysfs_device *dev, const char *devpath,
			     const char *subsystem, const char *driver)
{
	char *pos;

	strlcpy(dev->devpath, devpath, sizeof(dev->devpath));
	if (subsystem != NULL)
		strlcpy(dev->subsystem, subsystem, sizeof(dev->subsystem));
	if (driver != NULL)
		strlcpy(dev->driver, driver, sizeof(dev->driver));

	/* set kernel name */
	pos = strrchr(dev->devpath, '/');
	if (pos == NULL)
		return;
	strlcpy(dev->kernel, &pos[1], sizeof(dev->kernel));
	dbg("kernel='%s'", dev->kernel);

	/* some devices have '!' in their name, change that to '/' */
	pos = dev->kernel;
	while (pos[0] != '\0') {
		if (pos[0] == '!')
			pos[0] = '/';
		pos++;
	}

	/* get kernel number */
	pos = &dev->kernel[strlen(dev->kernel)];
	while (isdigit(pos[-1]))
		pos--;
	strlcpy(dev->kernel_number, pos, sizeof(dev->kernel_number));
	dbg("kernel_number='%s'", dev->kernel_number);
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

struct sysfs_device *sysfs_device_get(const char *devpath)
{
	char path[PATH_SIZE];
	char devpath_real[PATH_SIZE];
	struct sysfs_device *dev = NULL;
	struct sysfs_dev *sysdev_loop, *sysdev;
	struct stat statbuf;
	char link_path[PATH_SIZE];
	char link_target[PATH_SIZE];
	int len;
	char *pos;

	/* we handle only these devpathes */
	if (devpath != NULL &&
	    strncmp(devpath, "/devices/", 9) != 0 &&
	    strncmp(devpath, "/subsystem/", 11) != 0 &&
	    strncmp(devpath, "/module/", 8) != 0 &&
	    strncmp(devpath, "/bus/", 5) != 0 &&
	    strncmp(devpath, "/class/", 7) != 0 &&
	    strncmp(devpath, "/block/", 7) != 0) {
		dbg("invalid devpath '%s'", devpath);
		return NULL;
	}

	dbg("open '%s'", devpath);
	strlcpy(devpath_real, devpath, sizeof(devpath_real));
	remove_trailing_chars(devpath_real, '/');
	if (devpath[0] == '\0' )
		return NULL;

	/* if we got a link, resolve it to the real device */
	strlcpy(path, sysfs_path, sizeof(path));
	strlcat(path, devpath_real, sizeof(path));
	if (lstat(path, &statbuf) != 0) {
		/* if stat fails look in the cache */
		dbg("stat '%s' failed: %s", path, strerror(errno));
		list_for_each_entry(sysdev_loop, &sysfs_dev_list, node) {
			if (strcmp(sysdev_loop->dev.devpath, devpath_real) == 0) {
				dbg("found vanished dev in cache '%s'",
				    sysdev_loop->dev.devpath);
				return &sysdev_loop->dev;
			}
		}
		return NULL;
	}

	if (S_ISLNK(statbuf.st_mode)) {
		if (sysfs_resolve_link(devpath_real, sizeof(devpath_real)) != 0)
			return NULL;
	}

	list_for_each_entry(sysdev_loop, &sysfs_dev_list, node) {
		if (strcmp(sysdev_loop->dev.devpath, devpath_real) == 0) {
			dbg("found dev in cache '%s'", sysdev_loop->dev.devpath);
			dev = &sysdev_loop->dev;
		}
	}

	if(!dev) {
		/* it is a new device */
		dbg("new device '%s'", devpath_real);
		sysdev = malloc(sizeof(struct sysfs_dev));
		if (sysdev == NULL)
			return NULL;
		memset(sysdev, 0x00, sizeof(struct sysfs_dev));
		list_add(&sysdev->node, &sysfs_dev_list);
		dev = &sysdev->dev;
	}

	sysfs_device_set_values(dev, devpath_real, NULL, NULL);

	/* get subsystem name */
	strlcpy(link_path, sysfs_path, sizeof(link_path));
	strlcat(link_path, dev->devpath, sizeof(link_path));
	strlcat(link_path, "/subsystem", sizeof(link_path));
	len = readlink(link_path, link_target, sizeof(link_target));
	if (len > 0) {
		/* get subsystem from "subsystem" link */
		link_target[len] = '\0';
		dbg("subsystem link '%s' points to '%s'", link_path, link_target);
		pos = strrchr(link_target, '/');
		if (pos != NULL)
			strlcpy(dev->subsystem, &pos[1], sizeof(dev->subsystem));
	} else if (strstr(dev->devpath, "/drivers/") != NULL) {
		strlcpy(dev->subsystem, "drivers", sizeof(dev->subsystem));
	} else if (strncmp(dev->devpath, "/module/", 8) == 0) {
		strlcpy(dev->subsystem, "module", sizeof(dev->subsystem));
	} else if (strncmp(dev->devpath, "/subsystem/", 11) == 0) {
		pos = strrchr(dev->devpath, '/');
		if (pos == &dev->devpath[10])
			strlcpy(dev->subsystem, "subsystem",
				sizeof(dev->subsystem));
	} else if (strncmp(dev->devpath, "/class/", 7) == 0) {
		pos = strrchr(dev->devpath, '/');
		if (pos == &dev->devpath[6])
			strlcpy(dev->subsystem, "subsystem",
				sizeof(dev->subsystem));
	} else if (strncmp(dev->devpath, "/bus/", 5) == 0) {
		pos = strrchr(dev->devpath, '/');
		if (pos == &dev->devpath[4])
			strlcpy(dev->subsystem, "subsystem",
				sizeof(dev->subsystem));
	}

	/* get driver name */
	strlcpy(link_path, sysfs_path, sizeof(link_path));
	strlcat(link_path, dev->devpath, sizeof(link_path));
	strlcat(link_path, "/driver", sizeof(link_path));
	len = readlink(link_path, link_target, sizeof(link_target));
	if (len > 0) {
		link_target[len] = '\0';
		dbg("driver link '%s' points to '%s'", link_path, link_target);
		pos = strrchr(link_target, '/');
		if (pos != NULL)
			strlcpy(dev->driver, &pos[1], sizeof(dev->driver));
	}

	return dev;
}

struct sysfs_device *sysfs_device_get_parent(struct sysfs_device *dev)
{
	char parent_devpath[PATH_SIZE];
	char *pos;

	dbg("open '%s'", dev->devpath);

	/* look if we already know the parent */
	if (dev->parent != NULL)
		return dev->parent;

	strlcpy(parent_devpath, dev->devpath, sizeof(parent_devpath));
	dbg("'%s'", parent_devpath);

	/* strip last element */
	pos = strrchr(parent_devpath, '/');
	if (pos == NULL || pos == parent_devpath)
		return NULL;
	pos[0] = '\0';

	if (strncmp(parent_devpath, "/class", 6) == 0) {
		pos = strrchr(parent_devpath, '/');
		if (pos == &parent_devpath[6] || pos == parent_devpath) {
			dbg("/class top level, look for device link");
			goto device_link;
		}
	}
	if (strcmp(parent_devpath, "/block") == 0) {
		dbg("/block top level, look for device link");
		goto device_link;
	}

	/* are we at the top level? */
	pos = strrchr(parent_devpath, '/');
	if (pos == NULL || pos == parent_devpath)
		return NULL;

	/* get parent and remember it */
	dev->parent = sysfs_device_get(parent_devpath);
	return dev->parent;

device_link:
	strlcpy(parent_devpath, dev->devpath, sizeof(parent_devpath));
	strlcat(parent_devpath, "/device", sizeof(parent_devpath));
	if (sysfs_resolve_link(parent_devpath, sizeof(parent_devpath)) != 0)
		return NULL;

	/* get parent and remember it */
	dev->parent = sysfs_device_get(parent_devpath);
	return dev->parent;
}

struct sysfs_device *sysfs_device_get_parent_with_subsystem(struct sysfs_device *dev, const char *subsystem)
{
	struct sysfs_device *dev_parent;

	dev_parent = sysfs_device_get_parent(dev);
	while (dev_parent != NULL) {
		if (strcmp(dev_parent->subsystem, subsystem) == 0)
			return dev_parent;
		dev_parent = sysfs_device_get_parent(dev_parent);
	}
	return NULL;
}

void sysfs_device_put(struct sysfs_device *dev)
{
	struct sysfs_dev *sysdev_loop;

	list_for_each_entry(sysdev_loop, &sysfs_dev_list, node) {
		if (&sysdev_loop->dev == dev) {
			dbg("removed dev '%s' from cache",
			    sysdev_loop->dev.devpath);
			list_del(&sysdev_loop->node);
			free(sysdev_loop);
			return;
		}
	}
	dbg("dev '%s' not found in cache",
	    sysdev_loop->dev.devpath);

	return;
}

int
sysfs_attr_set_value(const char *devpath, const char *attr_name,
		     const char *value)
{
	char path_full[PATH_SIZE];
	int sysfs_len;
	struct stat statbuf;
	int fd, value_len, ret = -1;

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
		dbg("stat '%s' failed: %s" path_full, strerror(errno));
		goto out;
	}

	/* skip directories */
        if (S_ISDIR(statbuf.st_mode))
                goto out;

	if ((statbuf.st_mode & S_IWUSR) == 0)
		goto out;

	fd = open(path_full, O_WRONLY);
	if (fd < 0) {
		dbg("attribute '%s' can not be opened: %s",
		    path_full, strerror(errno));
		goto out;
	}
	value_len = strlen(value) + 1;
	ret = write(fd, value, value_len);
	if (ret == value_len)
		ret = 0;
	else if (ret < 0)
		dbg("write to %s failed: %s", path_full, strerror(errno));
	else {
		dbg("tried to write %d to %s. Wrote %d\n", value_len,
		    path_full, ret);
		ret = -1;
	}
	close(fd);
out:
	return ret;
}


char *sysfs_attr_get_value(const char *devpath, const char *attr_name)
{
	char path_full[PATH_SIZE];
	const char *path;
	char value[NAME_SIZE];
	struct sysfs_attr *attr_loop;
	struct sysfs_attr *attr = NULL;
	struct stat statbuf;
	int fd;
	ssize_t size;
	size_t sysfs_len;

	dbg("open '%s'/'%s'", devpath, attr_name);
	sysfs_len = strlcpy(path_full, sysfs_path, sizeof(path_full));
	if(sysfs_len >= sizeof(path_full))
		sysfs_len = sizeof(path_full) - 1;
	path = &path_full[sysfs_len];
	strlcat(path_full, devpath, sizeof(path_full));
	strlcat(path_full, "/", sizeof(path_full));
	strlcat(path_full, attr_name, sizeof(path_full));

	/* look for attribute in cache */
	list_for_each_entry(attr_loop, &attr_list, node) {
		if (strcmp(attr_loop->path, path) == 0) {
			dbg("found in cache '%s'", attr_loop->path);
			attr = attr_loop;
		}
	}
	if (!attr) {
		/* store attribute in cache */
		dbg("new uncached attribute '%s'", path_full);
		attr = malloc(sizeof(struct sysfs_attr));
		if (attr == NULL)
			return NULL;
		memset(attr, 0x00, sizeof(struct sysfs_attr));
		strlcpy(attr->path, path, sizeof(attr->path));
		dbg("add to cache '%s'", path_full);
		list_add(&attr->node, &attr_list);
	} else {
		/* clear old value */
		if(attr->value)
			memset(attr->value, 0x00, sizeof(attr->value));
	}

	if (lstat(path_full, &statbuf) != 0) {
		dbg("stat '%s' failed: %s", path_full, strerror(errno));
		goto out;
	}

	if (S_ISLNK(statbuf.st_mode)) {
		/* links return the last element of the target path */
		char link_target[PATH_SIZE];
		int len;
		const char *pos;

		len = readlink(path_full, link_target, sizeof(link_target));
		if (len > 0) {
			link_target[len] = '\0';
			pos = strrchr(link_target, '/');
			if (pos != NULL) {
				dbg("cache '%s' with link value '%s'",
				    path_full, value);
				strlcpy(attr->value_local, &pos[1],
					sizeof(attr->value_local));
				attr->value = attr->value_local;
			}
		}
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
	size = read(fd, value, sizeof(value));
	close(fd);
	if (size < 0)
		goto out;
	if (size == sizeof(value)) {
		dbg("overflow in attribute '%s', truncating", path_full);
		size--;
	}

	/* got a valid value, store and return it */
	value[size] = '\0';
	remove_trailing_chars(value, '\n');
	dbg("cache '%s' with attribute value '%s'", path_full, value);
	strlcpy(attr->value_local, value, sizeof(attr->value_local));
	attr->value = attr->value_local;

out:
	return attr && attr->value && strlen(attr->value) ? attr->value : NULL;
}

int sysfs_lookup_devpath_by_subsys_id(char *devpath_full, size_t len,
				      const char *subsystem, const char *id)
{
	size_t sysfs_len;
	char path_full[PATH_SIZE];
	char *path;
	struct stat statbuf;

	sysfs_len = strlcpy(path_full, sysfs_path, sizeof(path_full));
	path = &path_full[sysfs_len];

	if (strcmp(subsystem, "subsystem") == 0) {
		strlcpy(path, "/subsystem/", sizeof(path_full) - sysfs_len);
		strlcat(path, id, sizeof(path_full) - sysfs_len);
		if (stat(path_full, &statbuf) == 0)
			goto found;

		strlcpy(path, "/bus/", sizeof(path_full) - sysfs_len);
		strlcat(path, id, sizeof(path_full) - sysfs_len);
		if (stat(path_full, &statbuf) == 0)
			goto found;
		goto out;

		strlcpy(path, "/class/", sizeof(path_full) - sysfs_len);
		strlcat(path, id, sizeof(path_full) - sysfs_len);
		if (stat(path_full, &statbuf) == 0)
			goto found;
	}

	if (strcmp(subsystem, "module") == 0) {
		strlcpy(path, "/module/", sizeof(path_full) - sysfs_len);
		strlcat(path, id, sizeof(path_full) - sysfs_len);
		if (stat(path_full, &statbuf) == 0)
			goto found;
		goto out;
	}

	if (strcmp(subsystem, "drivers") == 0) {
		char subsys[NAME_SIZE];
		char *driver;

		strlcpy(subsys, id, sizeof(subsys));
		driver = strchr(subsys, ':');
		if (driver != NULL) {
			driver[0] = '\0';
			driver = &driver[1];
			strlcpy(path, "/subsystem/", sizeof(path_full) - sysfs_len);
			strlcat(path, subsys, sizeof(path_full) - sysfs_len);
			strlcat(path, "/drivers/", sizeof(path_full) - sysfs_len);
			strlcat(path, driver, sizeof(path_full) - sysfs_len);
			if (stat(path_full, &statbuf) == 0)
				goto found;

			strlcpy(path, "/bus/", sizeof(path_full) - sysfs_len);
			strlcat(path, subsys, sizeof(path_full) - sysfs_len);
			strlcat(path, "/drivers/", sizeof(path_full) - sysfs_len);
			strlcat(path, driver, sizeof(path_full) - sysfs_len);
			if (stat(path_full, &statbuf) == 0)
				goto found;
		}
		goto out;
	}

	strlcpy(path, "/subsystem/", sizeof(path_full) - sysfs_len);
	strlcat(path, subsystem, sizeof(path_full) - sysfs_len);
	strlcat(path, "/devices/", sizeof(path_full) - sysfs_len);
	strlcat(path, id, sizeof(path_full) - sysfs_len);
	if (stat(path_full, &statbuf) == 0)
		goto found;

	strlcpy(path, "/bus/", sizeof(path_full) - sysfs_len);
	strlcat(path, subsystem, sizeof(path_full) - sysfs_len);
	strlcat(path, "/devices/", sizeof(path_full) - sysfs_len);
	strlcat(path, id, sizeof(path_full) - sysfs_len);
	if (stat(path_full, &statbuf) == 0)
		goto found;

	strlcpy(path, "/class/", sizeof(path_full) - sysfs_len);
	strlcat(path, subsystem, sizeof(path_full) - sysfs_len);
	strlcat(path, "/", sizeof(path_full) - sysfs_len);
	strlcat(path, id, sizeof(path_full) - sysfs_len);
	if (stat(path_full, &statbuf) == 0)
		goto found;
out:
	return 0;
found:
	if (S_ISLNK(statbuf.st_mode))
		sysfs_resolve_link(path, sizeof(path_full) - sysfs_len);
	strlcpy(devpath_full, path, len);
	return 1;
}
