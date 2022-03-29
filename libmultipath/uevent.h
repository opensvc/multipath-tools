#ifndef _UEVENT_H
#define _UEVENT_H

/*
 * buffer for environment variables, the kernel's size in
 * lib/kobject_uevent.c should fit in
*/
#define HOTPLUG_BUFFER_SIZE		2048
#define HOTPLUG_NUM_ENVP		32
#define OBJECT_SIZE			512

struct udev;
struct config;

struct uevent {
	struct list_head node;
	struct list_head merge_node;
	struct udev_device *udev;
	char buffer[HOTPLUG_BUFFER_SIZE + OBJECT_SIZE];
	char *devpath;
	char *action;
	char *kernel;
	const char *wwid;
	unsigned long seqnum;
	char *envp[HOTPLUG_NUM_ENVP];
};

struct uevent *alloc_uevent(void);
int is_uevent_busy(void);

int uevent_listen(struct udev *udev);
int uevent_dispatch(int (*store_uev)(struct uevent *, void * trigger_data),
		    void * trigger_data);
bool uevent_is_mpath(const struct uevent *uev);
void uevent_get_wwid(struct uevent *uev, const struct config *conf);

int uevent_get_env_positive_int(const struct uevent *uev,
				const char *attr);

static inline int uevent_get_major(const struct uevent *uev)
{
	return uevent_get_env_positive_int(uev, "MAJOR");
}

static inline int uevent_get_minor(const struct uevent *uev)
{
	return uevent_get_env_positive_int(uev, "MINOR");
}

static inline int uevent_get_disk_ro(const struct uevent *uev)
{
	return uevent_get_env_positive_int(uev, "DISK_RO");
}

char *uevent_get_dm_str(const struct uevent *uev, char *attr);

static inline char *uevent_get_dm_name(const struct uevent *uev)
{
	return uevent_get_dm_str(uev, "DM_NAME");
}

static inline char *uevent_get_dm_path(const struct uevent *uev)
{
	return uevent_get_dm_str(uev, "DM_PATH");
}

static inline char *uevent_get_dm_action(const struct uevent *uev)
{
	return uevent_get_dm_str(uev, "DM_ACTION");
}

#endif /* _UEVENT_H */
