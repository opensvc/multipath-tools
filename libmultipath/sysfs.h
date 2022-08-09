/*
 * sysfs.h
 */

#ifndef _LIBMULTIPATH_SYSFS_H
#define _LIBMULTIPATH_SYSFS_H
#include <stdbool.h>
#include "strbuf.h"

ssize_t sysfs_attr_set_value(struct udev_device *dev, const char *attr_name,
			     const char * value, size_t value_len);
ssize_t sysfs_attr_get_value(struct udev_device *dev, const char *attr_name,
			     char * value, size_t value_len);
ssize_t sysfs_bin_attr_get_value(struct udev_device *dev, const char *attr_name,
				 unsigned char * value, size_t value_len);
#define sysfs_attr_value_ok(rc, value_len)			\
	({							\
		ssize_t __r = rc;				\
		__r >= 0 && (size_t)__r < (size_t)value_len;	\
	})

#define sysfs_attr_get_value_ok(dev, attr, val, len) \
	({ \
		size_t __l = (len);					\
		ssize_t __rc = sysfs_attr_get_value(dev, attr, val, __l); \
		sysfs_attr_value_ok(__rc, __l); \
	})

#define log_sysfs_attr_set_value(prio, rc, fmt, __args...)		\
do {									\
	STRBUF_ON_STACK(__buf);						\
	if (print_strbuf(&__buf, fmt, ##__args) >= 0 &&			\
	    print_strbuf(&__buf, ": %s", rc < 0 ? strerror(-rc) :	\
					"write underflow") >= 0)	\
		condlog(prio, "%s", get_strbuf_str(&__buf));		\
} while(0)

int sysfs_get_size (struct path *pp, unsigned long long * size);
int sysfs_check_holders(char * check_devt, char * new_devt);
bool sysfs_is_multipathed(struct path *pp, bool set_wwid);
#endif
