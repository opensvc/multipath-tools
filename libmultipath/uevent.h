#ifndef _UEVENT_H
#define _UEVENT_H

/*
 * buffer for environment variables, the kernel's size in
 * lib/kobject_uevent.c should fit in
*/
#define HOTPLUG_BUFFER_SIZE		2048
#define HOTPLUG_NUM_ENVP		32
#define OBJECT_SIZE			512

#ifndef NETLINK_KOBJECT_UEVENT
#define NETLINK_KOBJECT_UEVENT		15
#endif

struct uevent {
	void *next;
	char buffer[HOTPLUG_BUFFER_SIZE + OBJECT_SIZE];
	char *devpath;
	char *action;
	char *envp[HOTPLUG_NUM_ENVP];
};

int uevent_listen(int (*store_uev)(struct uevent *, void * trigger_data),
		  void * trigger_data);
int is_uevent_busy(void);
void setup_thread_attr(pthread_attr_t *attr, size_t stacksize, int detached);

#endif /* _UEVENT_H */
