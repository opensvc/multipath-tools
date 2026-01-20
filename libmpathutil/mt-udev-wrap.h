#ifndef MT_LIBUDEV_WRAP_HPP
#define MT_LIBUDEV_WRAP_HPP
#include "mt-libudev.h"

#define udev_ref(udev)                       mt_udev_ref(udev)
#define udev_unref(udev)                     mt_udev_unref(udev)
#define udev_new()                           mt_udev_new()

#define udev_list_entry_get_next(list_entry)             mt_udev_list_entry_get_next(list_entry)
#define udev_list_entry_get_by_name(list_entry, name)    mt_udev_list_entry_get_by_name(list_entry, name)
#define udev_list_entry_get_name(list_entry)             mt_udev_list_entry_get_name(list_entry)
#define udev_list_entry_get_value(list_entry)            mt_udev_list_entry_get_value(list_entry)

#define udev_list_entry_foreach(list_entry, first_entry)	\
        for (list_entry = first_entry; \
             list_entry; \
             list_entry = udev_list_entry_get_next(list_entry))

#define udev_device_new_from_syspath(udev, syspath)               mt_udev_device_new_from_syspath(udev, syspath)
#define udev_device_new_from_devnum(udev, type, devnum)           mt_udev_device_new_from_devnum(udev, type, devnum)
#define udev_device_new_from_subsystem_sysname(udev, subsystem, sysname) \
                                                                  mt_udev_device_new_from_subsystem_sysname(udev, subsystem, sysname)
#define udev_device_new_from_device_id(udev, id) mt_udev_device_new_from_device_id(udev, id)
#define udev_device_new_from_environment(udev)                    mt_udev_device_new_from_environment(udev)

#define udev_device_ref(udev_device)                      mt_udev_device_ref(udev_device)
#define udev_device_unref(udev_device)                    mt_udev_device_unref(udev_device)
#define udev_device_get_udev(udev_device)                 mt_udev_device_get_udev(udev_device)
#define udev_device_get_parent(udev_device)               mt_udev_device_get_parent(udev_device)
#define udev_device_get_parent_with_subsystem_devtype(udev_device, subsystem, devtype) \
	mt_udev_device_get_parent_with_subsystem_devtype(udev_device, subsystem, devtype)
#define udev_device_get_devpath(udev_device)              mt_udev_device_get_devpath(udev_device)
#define udev_device_get_subsystem(udev_device)            mt_udev_device_get_subsystem(udev_device)
#define udev_device_get_devtype(udev_device)              mt_udev_device_get_devtype(udev_device)
#define udev_device_get_syspath(udev_device)              mt_udev_device_get_syspath(udev_device)
#define udev_device_get_sysname(udev_device)              mt_udev_device_get_sysname(udev_device)
#define udev_device_get_is_initialized(udev_device)       mt_udev_device_get_is_initialized(udev_device)
#define udev_device_get_property_value(udev_device, key)  mt_udev_device_get_property_value(udev_device, key)
#define udev_device_get_devnum(udev_device)               mt_udev_device_get_devnum(udev_device)
#define udev_device_get_seqnum(udev_device)               mt_udev_device_get_seqnum(udev_device)
#define udev_device_get_driver(udev_device)                       mt_udev_device_get_driver(udev_device)
#define udev_device_get_devnode(udev_device)                      mt_udev_device_get_devnode(udev_device)
#define udev_device_get_sysattr_value(udev_device, sysattr) \
                                                          mt_udev_device_get_sysattr_value(udev_device, sysattr)
#define udev_device_set_sysattr_value(udev_device, sysattr, value) \
	mt_udev_device_set_sysattr_value(udev_device, sysattr, value)

#define udev_device_get_devlinks_list_entry(udev_device)        mt_udev_device_get_devlinks_list_entry(udev_device)
#define udev_device_get_properties_list_entry(udev_device)      mt_udev_device_get_properties_list_entry(udev_device)
#define udev_device_get_tags_list_entry(udev_device)            mt_udev_device_get_tags_list_entry(udev_device)
#define udev_device_get_current_tags_list_entry(udev_device)    mt_udev_device_get_current_tags_list_entry(udev_device)
#define udev_device_get_sysattr_list_entry(udev_device)         mt_udev_device_get_sysattr_list_entry(udev_device)

#define udev_monitor_new_from_netlink(udev, name)         mt_udev_monitor_new_from_netlink(udev, name)
#define udev_monitor_ref(udev_monitor)                    mt_udev_monitor_ref(udev_monitor)
#define udev_monitor_unref(udev_monitor)                  mt_udev_monitor_unref(udev_monitor)
#define udev_monitor_enable_receiving(udev_monitor)       mt_udev_monitor_enable_receiving(udev_monitor)
#define udev_monitor_get_fd(udev_monitor)                 mt_udev_monitor_get_fd(udev_monitor)
#define udev_monitor_receive_device(udev_monitor)         mt_udev_monitor_receive_device(udev_monitor)
#define udev_monitor_filter_add_match_subsystem_devtype(udev_monitor, subsystem, devtype) \
	mt_udev_monitor_filter_add_match_subsystem_devtype(udev_monitor, subsystem, devtype)
#define udev_monitor_set_receive_buffer_size(udev_monitor, size) \
                                                          mt_udev_monitor_set_receive_buffer_size(udev_monitor, size)

#define udev_enumerate_new(udev)                          mt_udev_enumerate_new(udev)
#define udev_enumerate_unref(udev_enumerate)              mt_udev_enumerate_unref(udev_enumerate)
#define udev_enumerate_add_match_subsystem(udev_enumerate, subsystem) \
                                                          mt_udev_enumerate_add_match_subsystem(udev_enumerate, subsystem)
#define udev_enumerate_add_nomatch_subsystem(udev_enumerate, subsystem) \
                                                          mt_udev_enumerate_add_nomatch_subsystem(udev_enumerate, subsystem)
#define udev_enumerate_add_match_sysattr(udev_enumerate, sysattr, value) \
                                                          mt_udev_enumerate_add_match_sysattr(udev_enumerate, sysattr, value)
#define udev_enumerate_add_nomatch_sysattr(udev_enumerate, sysattr, value) \
                                                          mt_udev_enumerate_add_nomatch_sysattr(udev_enumerate, sysattr, value)
#define udev_enumerate_add_match_property(udev_enumerate, property, value) \
                                                          mt_udev_enumerate_add_match_property(udev_enumerate, property, value)
#define udev_enumerate_add_match_tag(udev_enumerate, tag) mt_udev_enumerate_add_match_tag(udev_enumerate, tag)
#define udev_enumerate_add_match_parent(udev_enumerate, parent) \
                                                          mt_udev_enumerate_add_match_parent(udev_enumerate, parent)
#define udev_enumerate_add_match_is_initialized(udev_enumerate) \
                                                          mt_udev_enumerate_add_match_is_initialized(udev_enumerate)
#define udev_enumerate_add_syspath(udev_enumerate, syspath) \
                                                          mt_udev_enumerate_add_syspath(udev_enumerate, syspath)

#define udev_enumerate_scan_devices(udev_enumerate)       mt_udev_enumerate_scan_devices(udev_enumerate)
#define udev_enumerate_scan_device(udev_enumerate, syspath) \
                                                          mt_udev_enumerate_scan_device(udev_enumerate, syspath)
#define udev_enumerate_get_list_entry(udev_enumerate)     mt_udev_enumerate_get_list_entry(udev_enumerate)

#endif
