/*
 * sysfs.h
 */

#ifndef _LIBMULTIPATH_SYSFS_H
#define _LIBMULTIPATH_SYSFS_H

ssize_t sysfs_attr_set_value(struct udev_device *dev, const char *attr_name,
			     char * value, size_t value_len);
ssize_t sysfs_attr_get_value(struct udev_device *dev, const char *attr_name,
			     char * value, size_t value_len);
int sysfs_get_size (struct path *pp, unsigned long long * size);
int sysfs_check_holders(char * check_devt, char * new_devt);
#endif
