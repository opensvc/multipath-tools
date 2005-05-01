/* environment buffer, the kernel's size in lib/kobject_uevent.c should fit in */
#define HOTPLUG_BUFFER_SIZE		1024
#define HOTPLUG_NUM_ENVP		32
#define OBJECT_SIZE			512

#ifndef NETLINK_KOBJECT_UEVENT
#define NETLINK_KOBJECT_UEVENT		15
#endif

struct uevent {
	char *devpath;
	char *action;
	char *envp[HOTPLUG_NUM_ENVP];
};

int uevent_listen(int (*store_uev)(struct uevent *, void * trigger_data),
		  void * trigger_data);
