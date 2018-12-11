/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 * Copyright (c) 2005 Kiyoshi Ueda, NEC
 * Copyright (c) 2005 Edward Goggin, EMC
 * Copyright (c) 2005, 2018 Benjamin Marzinski, Redhat
 */
#include <unistd.h>
#include <libdevmapper.h>
#include <sys/mman.h>
#include <pthread.h>
#include <urcu.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/dm-ioctl.h>
#include <errno.h>

#include "vector.h"
#include "structs.h"
#include "structs_vec.h"
#include "devmapper.h"
#include "debug.h"
#include "main.h"
#include "dmevents.h"
#include "util.h"

#ifndef DM_DEV_ARM_POLL
#define DM_DEV_ARM_POLL _IOWR(DM_IOCTL, DM_DEV_SET_GEOMETRY_CMD + 1, struct dm_ioctl)
#endif

enum event_actions {
	EVENT_NOTHING,
	EVENT_REMOVE,
	EVENT_UPDATE,
};

struct dev_event {
	char name[WWID_SIZE];
	uint32_t evt_nr;
	enum event_actions action;
};

struct dmevent_waiter {
	int fd;
	struct vectors *vecs;
	vector events;
	pthread_mutex_t events_lock;
};

static struct dmevent_waiter *waiter;
/*
 * DM_VERSION_MINOR hasn't been updated when DM_DEV_ARM_POLL
 * was added in kernel 4.13. 4.37.0 (4.14) has it, safely.
 */
static const unsigned int DM_VERSION_FOR_ARM_POLL[] = {4, 37, 0};

int dmevent_poll_supported(void)
{
	unsigned int v[3];

	if (dm_drv_version(v))
		return 0;

	if (VERSION_GE(v, DM_VERSION_FOR_ARM_POLL))
		return 1;
	return 0;
}


int init_dmevent_waiter(struct vectors *vecs)
{
	if (!vecs) {
		condlog(0, "can't create waiter structure. invalid vectors");
		goto fail;
	}
	waiter = (struct dmevent_waiter *)malloc(sizeof(struct dmevent_waiter));
	if (!waiter) {
		condlog(0, "failed to allocate waiter structure");
		goto fail;
	}
	memset(waiter, 0, sizeof(struct dmevent_waiter));
	waiter->events = vector_alloc();
	if (!waiter->events) {
		condlog(0, "failed to allocate waiter events vector");
		goto fail_waiter;
	}
	waiter->fd = open("/dev/mapper/control", O_RDWR);
	if (waiter->fd < 0) {
		condlog(0, "failed to open /dev/mapper/control for waiter");
		goto fail_events;
	}
	pthread_mutex_init(&waiter->events_lock, NULL);
	waiter->vecs = vecs;

	return 0;
fail_events:
	vector_free(waiter->events);
fail_waiter:
	free(waiter);
fail:
	waiter = NULL;
	return -1;
}

void cleanup_dmevent_waiter(void)
{
	struct dev_event *dev_evt;
	int i;

	if (!waiter)
		return;
	pthread_mutex_destroy(&waiter->events_lock);
	close(waiter->fd);
	vector_foreach_slot(waiter->events, dev_evt, i)
		free(dev_evt);
	vector_free(waiter->events);
	free(waiter);
	waiter = NULL;
}

static int arm_dm_event_poll(int fd)
{
	struct dm_ioctl dmi;
	memset(&dmi, 0, sizeof(dmi));
	dmi.version[0] = DM_VERSION_FOR_ARM_POLL[0];
	dmi.version[1] = DM_VERSION_FOR_ARM_POLL[1];
	dmi.version[2] = DM_VERSION_FOR_ARM_POLL[2];
	/* This flag currently does nothing. It simply exists to
	 * duplicate the behavior of libdevmapper */
	dmi.flags = 0x4;
	dmi.data_start = offsetof(struct dm_ioctl, data);
	dmi.data_size = sizeof(dmi);
	return ioctl(fd, DM_DEV_ARM_POLL, &dmi);
}

/*
 * As of version 4.37.0 device-mapper stores the event number in the
 * dm_names structure after the name, when DM_DEVICE_LIST is called
 */
static uint32_t dm_event_nr(struct dm_names *n)
{
	return *(uint32_t *)(((uintptr_t)(strchr(n->name, 0) + 1) + 7) & ~7);
}

static int dm_get_events(void)
{
	struct dm_task *dmt;
	struct dm_names *names;
	struct dev_event *dev_evt;
	int i;

	if (!(dmt = libmp_dm_task_create(DM_DEVICE_LIST)))
		return -1;

	dm_task_no_open_count(dmt);

	if (!dm_task_run(dmt))
		goto fail;

	if (!(names = dm_task_get_names(dmt)))
		goto fail;

	pthread_mutex_lock(&waiter->events_lock);
	vector_foreach_slot(waiter->events, dev_evt, i)
		dev_evt->action = EVENT_REMOVE;
	while (names->dev) {
		uint32_t event_nr;

		/* Don't delete device if dm_is_mpath() fails without
		 * checking the device type */
		if (dm_is_mpath(names->name) == 0)
			goto next;

		event_nr = dm_event_nr(names);
		vector_foreach_slot(waiter->events, dev_evt, i) {
			if (!strcmp(dev_evt->name, names->name)) {
				if (event_nr != dev_evt->evt_nr) {
					dev_evt->evt_nr = event_nr;
					dev_evt->action = EVENT_UPDATE;
				} else
					dev_evt->action = EVENT_NOTHING;
				break;
			}
		}
next:
		if (!names->next)
			break;
		names = (void *)names + names->next;
	}
	pthread_mutex_unlock(&waiter->events_lock);
	dm_task_destroy(dmt);
	return 0;

fail:
	dm_task_destroy(dmt);
	return -1;
}

/* You must call __setup_multipath() after calling this function, to
 * deal with any events that came in before the device was added */
int watch_dmevents(char *name)
{
	int event_nr;
	struct dev_event *dev_evt, *old_dev_evt;
	int i;

	/* We know that this is a multipath device, so only fail if
	 * device-mapper tells us that we're wrong */
	if (dm_is_mpath(name) == 0) {
		condlog(0, "%s: not a multipath device. can't watch events",
			name);
		return -1;
	}

	if ((event_nr = dm_geteventnr(name)) < 0)
		return -1;

	dev_evt = (struct dev_event *)malloc(sizeof(struct dev_event));
	if (!dev_evt) {
		condlog(0, "%s: can't allocate event waiter structure", name);
		return -1;
	}

	strlcpy(dev_evt->name, name, WWID_SIZE);
	dev_evt->evt_nr = event_nr;
	dev_evt->action = EVENT_NOTHING;

	pthread_mutex_lock(&waiter->events_lock);
	vector_foreach_slot(waiter->events, old_dev_evt, i){
		if (!strcmp(dev_evt->name, old_dev_evt->name)) {
			/* caller will be updating this device */
			old_dev_evt->evt_nr = event_nr;
			old_dev_evt->action = EVENT_NOTHING;
			pthread_mutex_unlock(&waiter->events_lock);
			condlog(2, "%s: already waiting for events on device",
				name);
			free(dev_evt);
			return 0;
		}
	}
	if (!vector_alloc_slot(waiter->events)) {
		pthread_mutex_unlock(&waiter->events_lock);
		free(dev_evt);
		return -1;
	}
	vector_set_slot(waiter->events, dev_evt);
	pthread_mutex_unlock(&waiter->events_lock);
	return 0;
}

void unwatch_all_dmevents(void)
{
	struct dev_event *dev_evt;
	int i;

	pthread_mutex_lock(&waiter->events_lock);
	vector_foreach_slot(waiter->events, dev_evt, i)
		free(dev_evt);
	vector_reset(waiter->events);
	pthread_mutex_unlock(&waiter->events_lock);
}

static void unwatch_dmevents(char *name)
{
	struct dev_event *dev_evt;
	int i;

	pthread_mutex_lock(&waiter->events_lock);
	vector_foreach_slot(waiter->events, dev_evt, i) {
		if (!strcmp(dev_evt->name, name)) {
			vector_del_slot(waiter->events, i);
			free(dev_evt);
			break;
		}
	}
	pthread_mutex_unlock(&waiter->events_lock);
}

/*
 * returns the reschedule delay
 * negative means *stop*
 */

/* poll, arm, update, return */
static int dmevent_loop (void)
{
	int r, i = 0;
	struct pollfd pfd;
	struct dev_event *dev_evt;

	pfd.fd = waiter->fd;
	pfd.events = POLLIN;
	r = poll(&pfd, 1, -1);
	if (r <= 0) {
		condlog(0, "failed polling for dm events: %s", strerror(errno));
		/* sleep 1s and hope things get better */
		return 1;
	}

	if (arm_dm_event_poll(waiter->fd) != 0) {
		condlog(0, "Cannot re-arm event polling: %s", strerror(errno));
		/* sleep 1s and hope things get better */
		return 1;
	}

	if (dm_get_events() != 0) {
		condlog(0, "failed getting dm events: %s", strerror(errno));
		/* sleep 1s and hope things get better */
		return 1;
	}

	/*
	 * upon event ...
	 */

	while (1) {
		int done = 1;
		struct dev_event curr_dev;

		pthread_mutex_lock(&waiter->events_lock);
		vector_foreach_slot(waiter->events, dev_evt, i) {
			if (dev_evt->action != EVENT_NOTHING) {
				curr_dev = *dev_evt;
				if (dev_evt->action == EVENT_REMOVE) {
					vector_del_slot(waiter->events, i);
					free(dev_evt);
				} else
					dev_evt->action = EVENT_NOTHING;
				done = 0;
				break;
			}
		}
		pthread_mutex_unlock(&waiter->events_lock);
		if (done)
			return 1;

		condlog(3, "%s: devmap event #%i", curr_dev.name,
			curr_dev.evt_nr);

		/*
		 * event might be :
		 *
		 * 1) a table reload, which means our mpp structure is
		 *    obsolete : refresh it through update_multipath()
		 * 2) a path failed by DM : mark as such through
		 *    update_multipath()
		 * 3) map has gone away : stop the thread.
		 * 4) a path reinstate : nothing to do
		 * 5) a switch group : nothing to do
		 */
		pthread_cleanup_push(cleanup_lock, &waiter->vecs->lock);
		lock(&waiter->vecs->lock);
		pthread_testcancel();
		r = 0;
		if (curr_dev.action == EVENT_REMOVE)
			remove_map_by_alias(curr_dev.name, waiter->vecs, 1);
		else
			r = update_multipath(waiter->vecs, curr_dev.name, 1);
		pthread_cleanup_pop(1);

		if (r) {
			condlog(2, "%s: stopped watching dmevents",
				curr_dev.name);
			unwatch_dmevents(curr_dev.name);
		}
	}
	condlog(0, "dmevent waiter thread unexpectedly quit");
	return -1; /* never reach there */
}

static void rcu_unregister(void *param)
{
	rcu_unregister_thread();
}

void *wait_dmevents (void *unused)
{
	int r;


	if (!waiter) {
		condlog(0, "dmevents waiter not intialized");
		return NULL;
	}

	pthread_cleanup_push(rcu_unregister, NULL);
	rcu_register_thread();
	mlockall(MCL_CURRENT | MCL_FUTURE);

	while (1) {
		r = dmevent_loop();

		if (r < 0)
			break;

		sleep(r);
	}

	pthread_cleanup_pop(1);
	return NULL;
}
