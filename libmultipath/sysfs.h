/*
 * sysfs.h
 */

#ifndef _LIBMULTIPATH_SYSFS_H
#define _LIBMULTIPATH_SYSFS_H
#include <stdbool.h>

ssize_t sysfs_attr_set_value(struct udev_device *dev, const char *attr_name,
			     const char * value, size_t value_len);
ssize_t sysfs_attr_get_value(struct udev_device *dev, const char *attr_name,
			     char * value, size_t value_len);
ssize_t sysfs_bin_attr_get_value(struct udev_device *dev, const char *attr_name,
				 unsigned char * value, size_t value_len);
int sysfs_get_size (struct path *pp, unsigned long long * size);
int sysfs_check_holders(char * check_devt, char * new_devt);
bool sysfs_is_multipathed(const struct path *pp);
#endif
