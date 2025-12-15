#ifndef MT_LIBUDEV_H
#define MT_LIBUDEV_H

#include <pthread.h>
#include <stdarg.h>    /* for va_list */
#include <sys/types.h> /* For dev_t */

struct udev;
struct udev_list_entry;
struct udev_device;
struct udev_monitor;
struct udev_enumerate;

struct udev *mt_udev_ref(struct udev *udev);
struct udev *mt_udev_unref(struct udev *udev);
struct udev *mt_udev_new(void);

struct udev_list_entry *
mt_udev_list_entry_get_next(struct udev_list_entry *list_entry);
struct udev_list_entry *
mt_udev_list_entry_get_by_name(struct udev_list_entry *list_entry, const char *name);
const char *mt_udev_list_entry_get_name(struct udev_list_entry *list_entry);
const char *mt_udev_list_entry_get_value(struct udev_list_entry *list_entry);

struct udev_device *
mt_udev_device_new_from_syspath(struct udev *udev, const char *syspath);
struct udev_device *
mt_udev_device_new_from_devnum(struct udev *udev, char type, dev_t devnum);
struct udev_device *
mt_udev_device_new_from_subsystem_sysname(struct udev *udev, const char *subsystem,
					  const char *sysname);
/*
 * Some older libudev versions  don't use "const" for the id argument,
 * therefore we can't use it here, either.
 */
struct udev_device *mt_udev_device_new_from_device_id(struct udev *udev, char *id);
struct udev_device *mt_udev_device_new_from_environment(struct udev *udev);

struct udev_device *mt_udev_device_ref(struct udev_device *udev_device);
struct udev_device *mt_udev_device_unref(struct udev_device *udev_device);
struct udev *mt_udev_device_get_udev(struct udev_device *udev_device);
struct udev_device *mt_udev_device_get_parent(struct udev_device *udev_device);
struct udev_device *mt_udev_device_get_parent_with_subsystem_devtype(
	struct udev_device *udev_device, const char *subsystem,
	const char *devtype);
const char *mt_udev_device_get_devpath(struct udev_device *udev_device);
const char *mt_udev_device_get_subsystem(struct udev_device *udev_device);
const char *mt_udev_device_get_devtype(struct udev_device *udev_device);
const char *mt_udev_device_get_syspath(struct udev_device *udev_device);
const char *mt_udev_device_get_sysname(struct udev_device *udev_device);
int mt_udev_device_get_is_initialized(struct udev_device *udev_device);
const char *
mt_udev_device_get_property_value(struct udev_device *udev_device, const char *key);
dev_t mt_udev_device_get_devnum(struct udev_device *udev_device);
unsigned long long int mt_udev_device_get_seqnum(struct udev_device *udev_device);
const char *mt_udev_device_get_driver(struct udev_device *udev_device);
const char *mt_udev_device_get_devnode(struct udev_device *udev_device);
const char *mt_udev_device_get_sysattr_value(struct udev_device *udev_device,
					     const char *sysattr);
/*
 * libudev-215 (Debian jessie) doesn't use "const" for the value argument,
 * therefore we can't use it here, either.
 */
int mt_udev_device_set_sysattr_value(struct udev_device *udev_device,
				     const char *sysattr, char *value);

struct udev_list_entry *
mt_udev_device_get_properties_list_entry(struct udev_device *udev_device);
/*
 * multipath-tools doesn't use these, and some of them aren't available
 * in older libudev versions.

 struct udev_list_entry *mt_udev_device_get_devlinks_list_entry(struct
 udev_device *udev_device); struct udev_list_entry
 *mt_udev_device_get_tags_list_entry(struct udev_device *udev_device); struct
 udev_list_entry *mt_udev_device_get_current_tags_list_entry(struct udev_device
 *udev_device); struct udev_list_entry
 *mt_udev_device_get_sysattr_list_entry(struct udev_device *udev_device);
*/
struct udev_monitor *
mt_udev_monitor_new_from_netlink(struct udev *udev, const char *name);
struct udev_monitor *mt_udev_monitor_ref(struct udev_monitor *udev_monitor);
struct udev_monitor *mt_udev_monitor_unref(struct udev_monitor *udev_monitor);
int mt_udev_monitor_enable_receiving(struct udev_monitor *udev_monitor);
int mt_udev_monitor_get_fd(struct udev_monitor *udev_monitor);
struct udev_device *
mt_udev_monitor_receive_device(struct udev_monitor *udev_monitor);
int mt_udev_monitor_filter_add_match_subsystem_devtype(
	struct udev_monitor *udev_monitor, const char *subsystem,
	const char *devtype);
int mt_udev_monitor_set_receive_buffer_size(struct udev_monitor *udev_monitor,
					    int size);

struct udev_enumerate *mt_udev_enumerate_new(struct udev *udev);
struct udev_enumerate *mt_udev_enumerate_ref(struct udev_enumerate *udev_enumerate);
struct udev_enumerate *
mt_udev_enumerate_unref(struct udev_enumerate *udev_enumerate);
int mt_udev_enumerate_add_match_subsystem(struct udev_enumerate *udev_enumerate,
					  const char *subsystem);
int mt_udev_enumerate_add_nomatch_subsystem(struct udev_enumerate *udev_enumerate,
					    const char *subsystem);
int mt_udev_enumerate_add_match_sysattr(struct udev_enumerate *udev_enumerate,
					const char *sysattr, const char *value);
int mt_udev_enumerate_add_nomatch_sysattr(struct udev_enumerate *udev_enumerate,
					  const char *sysattr, const char *value);
int mt_udev_enumerate_add_match_property(struct udev_enumerate *udev_enumerate,
					 const char *property, const char *value);
int mt_udev_enumerate_add_match_tag(struct udev_enumerate *udev_enumerate,
				    const char *tag);
int mt_udev_enumerate_add_match_parent(struct udev_enumerate *udev_enumerate,
				       struct udev_device *parent);
int mt_udev_enumerate_add_match_is_initialized(struct udev_enumerate *udev_enumerate);
int mt_udev_enumerate_add_syspath(struct udev_enumerate *udev_enumerate,
				  const char *syspath);
int mt_udev_enumerate_scan_devices(struct udev_enumerate *udev_enumerate);
int mt_udev_enumerate_scan_subsystems(struct udev_enumerate *udev_enumerate);
struct udev_list_entry *
mt_udev_enumerate_get_list_entry(struct udev_enumerate *udev_enumerate);

#endif
