#include "mt-libudev.h"
#include <stddef.h>
#include <libudev.h>
#include "util.h"

static pthread_mutex_t libudev_mutex = PTHREAD_MUTEX_INITIALIZER;

struct udev *mt_udev_ref(struct udev *udev)
{
	struct udev *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_ref(udev);

	pthread_cleanup_pop(1);
	return ret;
}

struct udev *mt_udev_unref(struct udev *udev)
{
	struct udev *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_unref(udev);

	pthread_cleanup_pop(1);
	return ret;
}

struct udev *mt_udev_new(void)
{
	struct udev *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_new();

	pthread_cleanup_pop(1);
	return ret;
}

struct udev_list_entry *mt_udev_list_entry_get_next(struct udev_list_entry *list_entry)
{
	struct udev_list_entry *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_list_entry_get_next(list_entry);

	pthread_cleanup_pop(1);
	return ret;
}

struct udev_list_entry *
mt_udev_list_entry_get_by_name(struct udev_list_entry *list_entry, const char *name)
{
	struct udev_list_entry *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_list_entry_get_by_name(list_entry, name);

	pthread_cleanup_pop(1);
	return ret;
}

const char *mt_udev_list_entry_get_name(struct udev_list_entry *list_entry)
{
	const char *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_list_entry_get_name(list_entry);

	pthread_cleanup_pop(1);
	return ret;
}

const char *mt_udev_list_entry_get_value(struct udev_list_entry *list_entry)
{
	const char *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_list_entry_get_value(list_entry);

	pthread_cleanup_pop(1);
	return ret;
}

struct udev_device *
mt_udev_device_new_from_syspath(struct udev *udev, const char *syspath)
{
	struct udev_device *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_new_from_syspath(udev, syspath);

	pthread_cleanup_pop(1);
	return ret;
}

struct udev_device *
mt_udev_device_new_from_devnum(struct udev *udev, char type, dev_t devnum)
{
	struct udev_device *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_new_from_devnum(udev, type, devnum);

	pthread_cleanup_pop(1);
	return ret;
}

struct udev_device *
mt_udev_device_new_from_subsystem_sysname(struct udev *udev, const char *subsystem,
					  const char *sysname)
{
	struct udev_device *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_new_from_subsystem_sysname(udev, subsystem, sysname);

	pthread_cleanup_pop(1);
	return ret;
}

struct udev_device *mt_udev_device_new_from_device_id(struct udev *udev, char *id)
{
	struct udev_device *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_new_from_device_id(udev, id);

	pthread_cleanup_pop(1);
	return ret;
}

struct udev_device *mt_udev_device_new_from_environment(struct udev *udev)
{
	struct udev_device *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_new_from_environment(udev);

	pthread_cleanup_pop(1);
	return ret;
}

struct udev_device *mt_udev_device_ref(struct udev_device *udev_device)
{
	struct udev_device *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_ref(udev_device);

	pthread_cleanup_pop(1);
	return ret;
}

struct udev_device *mt_udev_device_unref(struct udev_device *udev_device)
{
	struct udev_device *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_unref(udev_device);

	pthread_cleanup_pop(1);
	return ret;
}

struct udev *mt_udev_device_get_udev(struct udev_device *udev_device)
{
	struct udev *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_get_udev(udev_device);

	pthread_cleanup_pop(1);
	return ret;
}

struct udev_device *mt_udev_device_get_parent(struct udev_device *udev_device)
{
	struct udev_device *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_get_parent(udev_device);

	pthread_cleanup_pop(1);
	return ret;
}

struct udev_device *mt_udev_device_get_parent_with_subsystem_devtype(
	struct udev_device *udev_device, const char *subsystem, const char *devtype)
{
	struct udev_device *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_get_parent_with_subsystem_devtype(udev_device,
							    subsystem, devtype);

	pthread_cleanup_pop(1);
	return ret;
}

const char *mt_udev_device_get_devpath(struct udev_device *udev_device)
{
	const char *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_get_devpath(udev_device);

	pthread_cleanup_pop(1);
	return ret;
}

const char *mt_udev_device_get_subsystem(struct udev_device *udev_device)
{
	const char *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_get_subsystem(udev_device);

	pthread_cleanup_pop(1);
	return ret;
}

const char *mt_udev_device_get_devtype(struct udev_device *udev_device)
{
	const char *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_get_devtype(udev_device);

	pthread_cleanup_pop(1);
	return ret;
}

const char *mt_udev_device_get_syspath(struct udev_device *udev_device)
{
	const char *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_get_syspath(udev_device);

	pthread_cleanup_pop(1);
	return ret;
}

const char *mt_udev_device_get_sysname(struct udev_device *udev_device)
{
	const char *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_get_sysname(udev_device);

	pthread_cleanup_pop(1);
	return ret;
}

dev_t mt_udev_device_get_devnum(struct udev_device *udev_device)
{
	dev_t ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_get_devnum(udev_device);

	pthread_cleanup_pop(1);
	return ret;
}

unsigned long long mt_udev_device_get_seqnum(struct udev_device *udev_device)
{
	unsigned long long ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_get_seqnum(udev_device);

	pthread_cleanup_pop(1);
	return ret;
}

const char *mt_udev_device_get_driver(struct udev_device *udev_device)
{
	const char *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_get_driver(udev_device);

	pthread_cleanup_pop(1);
	return ret;
}

const char *mt_udev_device_get_devnode(struct udev_device *udev_device)
{
	const char *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_get_devnode(udev_device);

	pthread_cleanup_pop(1);
	return ret;
}

int mt_udev_device_get_is_initialized(struct udev_device *udev_device)
{
	int ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_get_is_initialized(udev_device);

	pthread_cleanup_pop(1);
	return ret;
}

const char *
mt_udev_device_get_property_value(struct udev_device *udev_device, const char *key)
{
	const char *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_get_property_value(udev_device, key);

	pthread_cleanup_pop(1);
	return ret;
}

const char *mt_udev_device_get_sysattr_value(struct udev_device *udev_device,
					     const char *sysattr)
{
	const char *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_get_sysattr_value(udev_device, sysattr);

	pthread_cleanup_pop(1);
	return ret;
}

int mt_udev_device_set_sysattr_value(struct udev_device *udev_device,
				     const char *sysattr, char *value)
{
	int ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_set_sysattr_value(udev_device, sysattr, value);

	pthread_cleanup_pop(1);
	return ret;
}

struct udev_list_entry *
mt_udev_device_get_properties_list_entry(struct udev_device *udev_device)
{
	struct udev_list_entry *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_device_get_properties_list_entry(udev_device);

	pthread_cleanup_pop(1);
	return ret;
}

/*
 * multipath-tools doesn't use these, and some of them aren't available
 * in older libudev versions. Keeping the code here in case we will
 * need it in the future.

struct udev_list_entry *mt_udev_device_get_devlinks_list_entry(struct
udev_device *udev_device)
{
    struct udev_list_entry *ret;
    pthread_mutex_lock(&libudev_mutex);
    pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

    ret = udev_device_get_devlinks_list_entry(udev_device);

    pthread_cleanup_pop(1);
    return ret;
}

struct udev_list_entry *mt_udev_device_get_tags_list_entry(struct udev_device
*udev_device)
{
    struct udev_list_entry *ret;
    pthread_mutex_lock(&libudev_mutex);
    pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

    ret = udev_device_get_tags_list_entry(udev_device);

    pthread_cleanup_pop(1);
    return ret;
}

struct udev_list_entry *mt_udev_device_get_current_tags_list_entry(struct
udev_device *udev_device)
{
    struct udev_list_entry *ret;
    pthread_mutex_lock(&libudev_mutex);
    pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

    ret = udev_device_get_current_tags_list_entry(udev_device);

    pthread_cleanup_pop(1);
    return ret;
}

struct udev_list_entry *mt_udev_device_get_sysattr_list_entry(struct
udev_device *udev_device)
{
    struct udev_list_entry *ret;
    pthread_mutex_lock(&libudev_mutex);
    pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

    ret = udev_device_get_sysattr_list_entry(udev_device);

    pthread_cleanup_pop(1);
    return ret;
}
*/

struct udev_monitor *
mt_udev_monitor_new_from_netlink(struct udev *udev, const char *name)
{
	struct udev_monitor *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_monitor_new_from_netlink(udev, name);

	pthread_cleanup_pop(1);
	return ret;
}

struct udev_monitor *mt_udev_monitor_ref(struct udev_monitor *udev_monitor)
{
	struct udev_monitor *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_monitor_ref(udev_monitor);

	pthread_cleanup_pop(1);
	return ret;
}

struct udev_monitor *mt_udev_monitor_unref(struct udev_monitor *udev_monitor)
{
	struct udev_monitor *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_monitor_unref(udev_monitor);

	pthread_cleanup_pop(1);
	return ret;
}

int mt_udev_monitor_enable_receiving(struct udev_monitor *udev_monitor)
{
	int ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_monitor_enable_receiving(udev_monitor);

	pthread_cleanup_pop(1);
	return ret;
}

int mt_udev_monitor_get_fd(struct udev_monitor *udev_monitor)
{
	int ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_monitor_get_fd(udev_monitor);

	pthread_cleanup_pop(1);
	return ret;
}

struct udev_device *mt_udev_monitor_receive_device(struct udev_monitor *udev_monitor)
{
	struct udev_device *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_monitor_receive_device(udev_monitor);

	pthread_cleanup_pop(1);
	return ret;
}

int mt_udev_monitor_filter_add_match_subsystem_devtype(
	struct udev_monitor *udev_monitor, const char *subsystem, const char *devtype)
{
	int ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_monitor_filter_add_match_subsystem_devtype(udev_monitor,
							      subsystem, devtype);

	pthread_cleanup_pop(1);
	return ret;
}

int mt_udev_monitor_set_receive_buffer_size(struct udev_monitor *udev_monitor, int size)
{
	int ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_monitor_set_receive_buffer_size(udev_monitor, size);

	pthread_cleanup_pop(1);
	return ret;
}

struct udev_enumerate *mt_udev_enumerate_new(struct udev *udev)
{
	struct udev_enumerate *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_enumerate_new(udev);

	pthread_cleanup_pop(1);
	return ret;
}

struct udev_enumerate *mt_udev_enumerate_ref(struct udev_enumerate *udev_enumerate)
{
	struct udev_enumerate *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_enumerate_ref(udev_enumerate);

	pthread_cleanup_pop(1);
	return ret;
}

struct udev_enumerate *mt_udev_enumerate_unref(struct udev_enumerate *udev_enumerate)
{
	struct udev_enumerate *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_enumerate_unref(udev_enumerate);

	pthread_cleanup_pop(1);
	return ret;
}

int mt_udev_enumerate_add_match_subsystem(struct udev_enumerate *udev_enumerate,
					  const char *subsystem)
{
	int ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_enumerate_add_match_subsystem(udev_enumerate, subsystem);

	pthread_cleanup_pop(1);
	return ret;
}

int mt_udev_enumerate_add_nomatch_subsystem(struct udev_enumerate *udev_enumerate,
					    const char *subsystem)
{
	int ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_enumerate_add_nomatch_subsystem(udev_enumerate, subsystem);

	pthread_cleanup_pop(1);
	return ret;
}

int mt_udev_enumerate_add_match_sysattr(struct udev_enumerate *udev_enumerate,
					const char *sysattr, const char *value)
{
	int ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_enumerate_add_match_sysattr(udev_enumerate, sysattr, value);

	pthread_cleanup_pop(1);
	return ret;
}

int mt_udev_enumerate_add_nomatch_sysattr(struct udev_enumerate *udev_enumerate,
					  const char *sysattr, const char *value)
{
	int ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_enumerate_add_nomatch_sysattr(udev_enumerate, sysattr, value);

	pthread_cleanup_pop(1);
	return ret;
}

int mt_udev_enumerate_add_match_property(struct udev_enumerate *udev_enumerate,
					 const char *property, const char *value)
{
	int ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_enumerate_add_match_property(udev_enumerate, property, value);

	pthread_cleanup_pop(1);
	return ret;
}

int mt_udev_enumerate_add_match_tag(struct udev_enumerate *udev_enumerate,
				    const char *tag)
{
	int ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_enumerate_add_match_tag(udev_enumerate, tag);

	pthread_cleanup_pop(1);
	return ret;
}

int mt_udev_enumerate_add_match_parent(struct udev_enumerate *udev_enumerate,
				       struct udev_device *parent)
{
	int ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_enumerate_add_match_parent(udev_enumerate, parent);

	pthread_cleanup_pop(1);
	return ret;
}

int mt_udev_enumerate_add_match_is_initialized(struct udev_enumerate *udev_enumerate)
{
	int ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_enumerate_add_match_is_initialized(udev_enumerate);

	pthread_cleanup_pop(1);
	return ret;
}

int mt_udev_enumerate_add_syspath(struct udev_enumerate *udev_enumerate,
				  const char *syspath)
{
	int ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_enumerate_add_syspath(udev_enumerate, syspath);

	pthread_cleanup_pop(1);
	return ret;
}

int mt_udev_enumerate_scan_devices(struct udev_enumerate *udev_enumerate)
{
	int ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_enumerate_scan_devices(udev_enumerate);

	pthread_cleanup_pop(1);
	return ret;
}

struct udev_list_entry *
mt_udev_enumerate_get_list_entry(struct udev_enumerate *udev_enumerate)
{
	struct udev_list_entry *ret;

	pthread_mutex_lock(&libudev_mutex);
	pthread_cleanup_push(cleanup_mutex, &libudev_mutex);

	ret = udev_enumerate_get_list_entry(udev_enumerate);

	pthread_cleanup_pop(1);
	return ret;
}
