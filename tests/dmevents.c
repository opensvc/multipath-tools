/*
 * Copyright (c) 2018 Benjamin Marzinski, Redhat
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <cmocka.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "structs.h"
#include "structs_vec.h"

#include "globals.c"
/* I have to do this to get at the static variables */
#include "../multipathd/dmevents.c"

/* pretend dm device */
struct dm_device {
	char name[WWID_SIZE];
	/* is this a mpath device, or other dm device */
	int is_mpath;
	uint32_t evt_nr;
	/* tracks the event number when the multipath device was updated */
	uint32_t update_nr;
};

struct test_data {
	struct vectors vecs;
	vector dm_devices;
	struct dm_names *names;
};

struct test_data data;

/* Add a pretend dm device, or update its event number. This is used to build
 * up the dm devices that the dmevents code queries with dm_task_get_names,
 * dm_geteventnr, and dm_is_mpath */
int add_dm_device_event(char *name, int is_mpath, uint32_t evt_nr)
{
	struct dm_device *dev;
	int i;

	vector_foreach_slot(data.dm_devices, dev, i) {
		if (strcmp(name, dev->name) == 0) {
			dev->evt_nr = evt_nr;
			return 0;
		}
	}
	dev = (struct dm_device *)malloc(sizeof(struct dm_device));
	if (!dev){
		condlog(0, "Testing error mallocing dm_device");
		return -1;
	}
	strncpy(dev->name, name, WWID_SIZE);
	dev->name[WWID_SIZE - 1] = 0;
	dev->is_mpath = is_mpath;
	dev->evt_nr = evt_nr;
	if (!vector_alloc_slot(data.dm_devices)) {
		condlog(0, "Testing error setting dm_devices slot");
		free(dev);
		return -1;
	}
	vector_set_slot(data.dm_devices, dev);
	return 0;
}

/* helper function for pretend dm devices */
struct dm_device *find_dm_device(const char *name)
{
	struct dm_device *dev;
	int i;

	vector_foreach_slot(data.dm_devices, dev, i)
		if (strcmp(name, dev->name) == 0)
			return dev;
	return NULL;
}

/* helper function for pretend dm devices */
int remove_dm_device_event(const char *name)
{
	struct dm_device *dev;
	int i;

	vector_foreach_slot(data.dm_devices, dev, i) {
		if (strcmp(name, dev->name) == 0) {
			vector_del_slot(data.dm_devices, i);
				free(dev);
			return 0;
		}
	}
	return -1;
}

/* helper function for pretend dm devices */
void remove_all_dm_device_events(void)
{
	struct dm_device *dev;
	int i;

	vector_foreach_slot(data.dm_devices, dev, i)
		free(dev);
	vector_reset(data.dm_devices);
}

static inline size_t align_val(size_t val)
{
        return (val + 7) & ~7;
}
static inline void *align_ptr(void *ptr)
{
	return (void *)align_val((size_t)ptr);
}

/* copied off of list_devices in dm-ioctl.c except that it uses
 * the pretend dm devices, and saves the output to the test_data
 * structure */
struct dm_names *build_dm_names(void)
{
	struct dm_names *names, *np, *old_np = NULL;
	uint32_t *event_nr;
	struct dm_device *dev;
	int i, size = 0;

	if (VECTOR_SIZE(data.dm_devices) == 0) {
		names = (struct dm_names *)malloc(sizeof(struct dm_names));
		if (!names) {
			condlog(0, "Testing error allocating empty dm_names");
			return NULL;
		}
		names->dev = 0;
		names->next = 0;
		return names;
	}
	vector_foreach_slot(data.dm_devices, dev, i) {
		size += align_val(offsetof(struct dm_names, name) +
				  strlen(dev->name) + 1);
		size += align_val(sizeof(uint32_t));
	}
	names = (struct dm_names *)malloc(size);
	if (!names) {
		condlog(0, "Testing error allocating dm_names");
		return NULL;
	}
	np = names;
	vector_foreach_slot(data.dm_devices, dev, i) {
		if (old_np)
			old_np->next = (uint32_t) ((uintptr_t) np -
						   (uintptr_t) old_np);
		np->dev = 1;
		np->next = 0;
		strcpy(np->name, dev->name);

		old_np = np;
		event_nr = align_ptr(np->name + strlen(dev->name) + 1);
		*event_nr = dev->evt_nr;
		np = align_ptr(event_nr + 1);
	}
	assert_int_equal((char *)np - (char *)names, size);
	return names;
}

static int setup(void **state)
{
	if (dmevent_poll_supported()) {
		data.dm_devices = vector_alloc();
		*state = &data;
	} else
		*state = NULL;
	return 0;
}

static int teardown(void **state)
{
	struct dm_device *dev;
	int i;
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		return 0;
	vector_foreach_slot(datap->dm_devices, dev, i)
		free(dev);
	vector_free(datap->dm_devices);
	datap = NULL;
	return 0;
}

int __wrap_open(const char *pathname, int flags)
{
	assert_ptr_equal(pathname, "/dev/mapper/control");
	assert_int_equal(flags, O_RDWR);
	return mock_type(int);
}

/* We never check the result of the close(), so there's no need to
 * to mock a return value */
int __wrap_close(int fd)
{
	assert_int_equal(fd, waiter->fd);
	return 0;
}

/* the pretend dm device code checks the input and supplies the
 * return value, so there's no need to do that here */
int __wrap_dm_is_mpath(const char *name)
{
	struct dm_device *dev;
	int i;

	vector_foreach_slot(data.dm_devices, dev, i)
		if (strcmp(name, dev->name) == 0)
			return dev->is_mpath;
	return 0;
}

/* either get return info from the pretend dm device, or
 * override it to test -1 return */
int __wrap_dm_geteventnr(const char *name)
{
	struct dm_device *dev;
	int fail = mock_type(int);

	if (fail)
		return -1;
	dev = find_dm_device(name);
	if (dev) {
		/* simulate updating device state after adding it */
		dev->update_nr = dev->evt_nr;
		return dev->evt_nr;
	}
	return -1;
}

int __wrap_ioctl(int fd, unsigned long request, void *argp)
{
	assert_int_equal(fd, waiter->fd);
	assert_int_equal(request, DM_DEV_ARM_POLL);
	return mock_type(int);
}

struct dm_task *__wrap_libmp_dm_task_create(int task)
{
	assert_int_equal(task, DM_DEVICE_LIST);
	return mock_type(struct dm_task *);
}

int __wrap_dm_task_no_open_count(struct dm_task *dmt)
{
	assert_ptr_equal((struct test_data *)dmt, &data);
	return mock_type(int);
}

int __wrap_dm_task_run(struct dm_task *dmt)
{
	assert_ptr_equal((struct test_data *)dmt, &data);
	return mock_type(int);
}

/* either get return info from the pretend dm device, or
 * override it to test NULL return */
struct dm_names * __wrap_dm_task_get_names(struct dm_task *dmt)
{
	int good = mock_type(int);
	assert_ptr_equal((struct test_data *)dmt, &data);

	if (data.names) {
		condlog(0, "Testing error. data.names already allocated");
		return NULL;
	}
	if (!good)
		return NULL;
	data.names = build_dm_names();
	return data.names;
}

void __wrap_dm_task_destroy(struct dm_task *dmt)
{
	assert_ptr_equal((struct test_data *)dmt, &data);

	if (data.names) {
		free(data.names);
		data.names = NULL;
	}
}

int __wrap_poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	assert_int_equal(nfds, 1);
	assert_int_equal(timeout, -1);
	assert_int_equal(fds->fd, waiter->fd);
	assert_int_equal(fds->events, POLLIN);
	return mock_type(int);
}

void __wrap_remove_map_by_alias(const char *alias, struct vectors * vecs,
				int purge_vec)
{
	check_expected(alias);
	assert_ptr_equal(vecs, waiter->vecs);
	assert_int_equal(purge_vec, 1);
}

/* pretend update the pretend dm devices. If fail is set, it
 * simulates having the dm device removed. Otherwise it just sets
 * update_nr to record when the update happened */
int __wrap_update_multipath(struct vectors *vecs, char *mapname, int reset)
{
	int fail;

	check_expected(mapname);
	assert_ptr_equal(vecs, waiter->vecs);
	assert_int_equal(reset, 1);
	fail = mock_type(int);
	if (fail) {
		assert_int_equal(remove_dm_device_event(mapname), 0);
		return fail;
	} else {
		struct dm_device *dev;
		int i;

		vector_foreach_slot(data.dm_devices, dev, i) {
			if (strcmp(mapname, dev->name) == 0) {
				dev->update_nr = dev->evt_nr;
				return 0;
			}
		}
		fail();
	}
	return fail;
}

/* helper function used to check if the dmevents list of devices
 * includes a specific device. To make sure that dmevents is
 * in the correct state after running a function */
struct dev_event *find_dmevents(const char *name)
{
	struct dev_event *dev_evt;
	int i;

	vector_foreach_slot(waiter->events, dev_evt, i)
		if (!strcmp(dev_evt->name, name))
			return dev_evt;
	return NULL;
}

/* null vecs pointer when initialized dmevents */
static void test_init_waiter_bad0(void **state)
{
	/* this boilerplate code just skips the test if
	 * dmevents polling is not supported */
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();

	assert_int_equal(init_dmevent_waiter(NULL), -1);
}

/* fail to open /dev/mapper/control */
static void test_init_waiter_bad1(void **state)
{
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();
	will_return(__wrap_open, -1);
	assert_int_equal(init_dmevent_waiter(&datap->vecs), -1);
	assert_ptr_equal(waiter, NULL);
}

/* waiter remains initialized after this test */
static void test_init_waiter_good0(void **state)
{
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();
	will_return(__wrap_open, 2);
	assert_int_equal(init_dmevent_waiter(&datap->vecs), 0);
	assert_ptr_not_equal(waiter, NULL);
}

/* No dm device named foo */
static void test_watch_dmevents_bad0(void **state)
{
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();
	assert_int_equal(watch_dmevents("foo"), -1);
	assert_ptr_equal(find_dmevents("foo"), NULL);
}

/* foo is not a multipath device */
static void test_watch_dmevents_bad1(void **state)
{
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();

	assert_int_equal(add_dm_device_event("foo", 0, 5), 0);
	assert_int_equal(watch_dmevents("foo"), -1);
	assert_ptr_equal(find_dmevents("foo"), NULL);
}

/* failed getting the dmevent number */
static void test_watch_dmevents_bad2(void **state)
{
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();

	remove_all_dm_device_events();
	assert_int_equal(add_dm_device_event("foo", 1, 5), 0);
	will_return(__wrap_dm_geteventnr, -1);
	assert_int_equal(watch_dmevents("foo"), -1);
	assert_ptr_equal(find_dmevents("foo"), NULL);
}

/* verify that you can watch and unwatch dm multipath device "foo" */
static void test_watch_dmevents_good0(void **state)
{
	struct dev_event *dev_evt;
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();

	remove_all_dm_device_events();
	assert_int_equal(add_dm_device_event("foo", 1, 5), 0);
	will_return(__wrap_dm_geteventnr, 0);
	assert_int_equal(watch_dmevents("foo"), 0);
	/* verify foo is being watched */
	dev_evt = find_dmevents("foo");
	assert_ptr_not_equal(dev_evt, NULL);
	assert_int_equal(dev_evt->evt_nr, 5);
	assert_int_equal(dev_evt->action, EVENT_NOTHING);
	assert_int_equal(VECTOR_SIZE(waiter->events), 1);
	unwatch_dmevents("foo");
	/* verify foo is no longer being watched */
	assert_int_equal(VECTOR_SIZE(waiter->events), 0);
	assert_ptr_equal(find_dmevents("foo"), NULL);
}

/* verify that if you try to watch foo multiple times, it only
 * is placed on the waiter list once */
static void test_watch_dmevents_good1(void **state)
{
	struct dev_event *dev_evt;
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();

	remove_all_dm_device_events();
	assert_int_equal(add_dm_device_event("foo", 1, 5), 0);
	will_return(__wrap_dm_geteventnr, 0);
	assert_int_equal(watch_dmevents("foo"), 0);
	dev_evt = find_dmevents("foo");
	assert_ptr_not_equal(dev_evt, NULL);
	assert_int_equal(dev_evt->evt_nr, 5);
	assert_int_equal(dev_evt->action, EVENT_NOTHING);
	assert_int_equal(add_dm_device_event("foo", 1, 6), 0);
	will_return(__wrap_dm_geteventnr, 0);
	assert_int_equal(watch_dmevents("foo"), 0);
	dev_evt = find_dmevents("foo");
	assert_ptr_not_equal(dev_evt, NULL);
	assert_int_equal(dev_evt->evt_nr, 6);
	assert_int_equal(dev_evt->action, EVENT_NOTHING);
	assert_int_equal(VECTOR_SIZE(waiter->events), 1);
	unwatch_dmevents("foo");
	assert_int_equal(VECTOR_SIZE(waiter->events), 0);
	assert_ptr_equal(find_dmevents("foo"), NULL);
}

/* watch and then unwatch multiple devices */
static void test_watch_dmevents_good2(void **state)
{
	struct dev_event *dev_evt;
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();

	unwatch_all_dmevents();
	remove_all_dm_device_events();
	assert_int_equal(add_dm_device_event("foo", 1, 5), 0);
	assert_int_equal(add_dm_device_event("bar", 1, 7), 0);
	will_return(__wrap_dm_geteventnr, 0);
	assert_int_equal(watch_dmevents("foo"), 0);
	dev_evt = find_dmevents("foo");
	assert_ptr_not_equal(dev_evt, NULL);
	assert_int_equal(dev_evt->evt_nr, 5);
	assert_int_equal(dev_evt->action, EVENT_NOTHING);
	assert_ptr_equal(find_dmevents("bar"), NULL);
	will_return(__wrap_dm_geteventnr, 0);
	assert_int_equal(watch_dmevents("bar"), 0);
	dev_evt = find_dmevents("foo");
	assert_ptr_not_equal(dev_evt, NULL);
	assert_int_equal(dev_evt->evt_nr, 5);
	assert_int_equal(dev_evt->action, EVENT_NOTHING);
	dev_evt = find_dmevents("bar");
	assert_ptr_not_equal(dev_evt, NULL);
	assert_int_equal(dev_evt->evt_nr, 7);
	assert_int_equal(dev_evt->action, EVENT_NOTHING);
	assert_int_equal(VECTOR_SIZE(waiter->events), 2);
	unwatch_all_dmevents();
	assert_int_equal(VECTOR_SIZE(waiter->events), 0);
	assert_ptr_equal(find_dmevents("foo"), NULL);
	assert_ptr_equal(find_dmevents("bar"), NULL);
}

/* dm_task_create fails */
static void test_get_events_bad0(void **state)
{
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();

	unwatch_all_dmevents();
	remove_all_dm_device_events();

	will_return(__wrap_libmp_dm_task_create, NULL);
	assert_int_equal(dm_get_events(), -1);
}

/* dm_task_run fails */
static void test_get_events_bad1(void **state)
{
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();

	will_return(__wrap_libmp_dm_task_create, &data);
	will_return(__wrap_dm_task_no_open_count, 1);
	will_return(__wrap_dm_task_run, 0);
	assert_int_equal(dm_get_events(), -1);
}

/* dm_task_get_names fails */
static void test_get_events_bad2(void **state)
{
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();

	will_return(__wrap_libmp_dm_task_create, &data);
	will_return(__wrap_dm_task_no_open_count, 1);
	will_return(__wrap_dm_task_run, 1);
	will_return(__wrap_dm_task_get_names, 0);
	assert_int_equal(dm_get_events(), -1);
}

/* If the device isn't being watched, dm_get_events returns NULL */
static void test_get_events_good0(void **state)
{
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();

	assert_int_equal(add_dm_device_event("foo", 1, 5), 0);
	will_return(__wrap_libmp_dm_task_create, &data);
	will_return(__wrap_dm_task_no_open_count, 1);
	will_return(__wrap_dm_task_run, 1);
	will_return(__wrap_dm_task_get_names, 1);
	assert_int_equal(dm_get_events(), 0);
	assert_ptr_equal(find_dmevents("foo"), NULL);
	assert_int_equal(VECTOR_SIZE(waiter->events), 0);
}

/* There are 5 dm devices. 4 of them are multipath devices.
 * Only 3 of them are being watched. "foo" has a new event
 * "xyzzy" gets removed. Nothing happens to bar. Verify
 * that all the events are properly set, and that nothing
 * happens with the two devices that aren't being watched */
static void test_get_events_good1(void **state)
{
	struct dev_event *dev_evt;
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();

	remove_all_dm_device_events();
	assert_int_equal(add_dm_device_event("foo", 1, 5), 0);
	assert_int_equal(add_dm_device_event("bar", 1, 7), 0);
	assert_int_equal(add_dm_device_event("baz", 1, 12), 0);
	assert_int_equal(add_dm_device_event("qux", 0, 4), 0);
	assert_int_equal(add_dm_device_event("xyzzy", 1, 8), 0);
	will_return(__wrap_dm_geteventnr, 0);
	assert_int_equal(watch_dmevents("foo"), 0);
	will_return(__wrap_dm_geteventnr, 0);
	assert_int_equal(watch_dmevents("bar"), 0);
	will_return(__wrap_dm_geteventnr, 0);
	assert_int_equal(watch_dmevents("xyzzy"), 0);
	assert_int_equal(add_dm_device_event("foo", 1, 6), 0);
	assert_int_equal(remove_dm_device_event("xyzzy"), 0);
	will_return(__wrap_libmp_dm_task_create, &data);
	will_return(__wrap_dm_task_no_open_count, 1);
	will_return(__wrap_dm_task_run, 1);
	will_return(__wrap_dm_task_get_names, 1);
	assert_int_equal(dm_get_events(), 0);
	dev_evt = find_dmevents("foo");
	assert_ptr_not_equal(dev_evt, NULL);
	assert_int_equal(dev_evt->evt_nr, 6);
	assert_int_equal(dev_evt->action, EVENT_UPDATE);
	dev_evt = find_dmevents("bar");
	assert_ptr_not_equal(dev_evt, NULL);
	assert_int_equal(dev_evt->evt_nr, 7);
	assert_int_equal(dev_evt->action, EVENT_NOTHING);
	dev_evt = find_dmevents("xyzzy");
	assert_ptr_not_equal(dev_evt, NULL);
	assert_int_equal(dev_evt->evt_nr, 8);
	assert_int_equal(dev_evt->action, EVENT_REMOVE);
	assert_ptr_equal(find_dmevents("baz"), NULL);
	assert_ptr_equal(find_dmevents("qux"), NULL);
	assert_int_equal(VECTOR_SIZE(waiter->events), 3);
}

/* poll does not return an event. nothing happens. The
 * devices remain after this test */
static void test_dmevent_loop_bad0(void **state)
{
	struct dm_device *dev;
	struct dev_event *dev_evt;
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();

	remove_all_dm_device_events();
	unwatch_all_dmevents();
	assert_int_equal(add_dm_device_event("foo", 1, 5), 0);
	will_return(__wrap_dm_geteventnr, 0);
	assert_int_equal(watch_dmevents("foo"), 0);
	assert_int_equal(add_dm_device_event("foo", 1, 6), 0);
	will_return(__wrap_poll, 0);
	assert_int_equal(dmevent_loop(), 1);
	dev_evt = find_dmevents("foo");
	assert_ptr_not_equal(dev_evt, NULL);
	assert_int_equal(dev_evt->evt_nr, 5);
	assert_int_equal(dev_evt->action, EVENT_NOTHING);
	dev = find_dm_device("foo");
	assert_ptr_not_equal(dev, NULL);
	assert_int_equal(dev->evt_nr, 6);
	assert_int_equal(dev->update_nr, 5);
}

/* arm_dm_event_poll's ioctl fails. Nothing happens */
static void test_dmevent_loop_bad1(void **state)
{
	struct dm_device *dev;
	struct dev_event *dev_evt;
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();

	will_return(__wrap_poll, 1);
	will_return(__wrap_ioctl, -1);
	assert_int_equal(dmevent_loop(), 1);
	dev_evt = find_dmevents("foo");
	assert_ptr_not_equal(dev_evt, NULL);
	assert_int_equal(dev_evt->evt_nr, 5);
	assert_int_equal(dev_evt->action, EVENT_NOTHING);
	dev = find_dm_device("foo");
	assert_ptr_not_equal(dev, NULL);
	assert_int_equal(dev->evt_nr, 6);
	assert_int_equal(dev->update_nr, 5);
}

/* dm_get_events fails. Nothing happens */
static void test_dmevent_loop_bad2(void **state)
{
	struct dm_device *dev;
	struct dev_event *dev_evt;
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();

	will_return(__wrap_poll, 1);
	will_return(__wrap_ioctl, 0);
	will_return(__wrap_libmp_dm_task_create, NULL);
	assert_int_equal(dmevent_loop(), 1);
	dev_evt = find_dmevents("foo");
	assert_ptr_not_equal(dev_evt, NULL);
	assert_int_equal(dev_evt->evt_nr, 5);
	assert_int_equal(dev_evt->action, EVENT_NOTHING);
	dev = find_dm_device("foo");
	assert_ptr_not_equal(dev, NULL);
	assert_int_equal(dev->evt_nr, 6);
	assert_int_equal(dev->update_nr, 5);
}

/* verify dmevent_loop runs successfully when no devices are being
 * watched */
static void test_dmevent_loop_good0(void **state)
{
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();

	remove_all_dm_device_events();
	unwatch_all_dmevents();
	will_return(__wrap_poll, 1);
	will_return(__wrap_ioctl, 0);
	will_return(__wrap_libmp_dm_task_create, &data);
	will_return(__wrap_dm_task_no_open_count, 1);
	will_return(__wrap_dm_task_run, 1);
	will_return(__wrap_dm_task_get_names, 1);
	assert_int_equal(dmevent_loop(), 1);
}

/* Watch 3 devices, where one device has an event (foo), one device is
 * removed (xyzzy), and one device does nothing (bar). Verify that
 * the device with the event gets updated, the device that is removed
 * gets unwatched, and the device with no events stays the same.
 * The devices remain after this test */
static void test_dmevent_loop_good1(void **state)
{
	struct dm_device *dev;
	struct dev_event *dev_evt;
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();

	remove_all_dm_device_events();
	unwatch_all_dmevents();
	assert_int_equal(add_dm_device_event("foo", 1, 5), 0);
	assert_int_equal(add_dm_device_event("bar", 1, 7), 0);
	assert_int_equal(add_dm_device_event("baz", 1, 12), 0);
	assert_int_equal(add_dm_device_event("xyzzy", 1, 8), 0);
	will_return(__wrap_dm_geteventnr, 0);
	assert_int_equal(watch_dmevents("foo"), 0);
	will_return(__wrap_dm_geteventnr, 0);
	assert_int_equal(watch_dmevents("bar"), 0);
	will_return(__wrap_dm_geteventnr, 0);
	assert_int_equal(watch_dmevents("xyzzy"), 0);
	assert_int_equal(add_dm_device_event("foo", 1, 6), 0);
	assert_int_equal(remove_dm_device_event("xyzzy"), 0);
	will_return(__wrap_poll, 1);
	will_return(__wrap_ioctl, 0);
	will_return(__wrap_libmp_dm_task_create, &data);
	will_return(__wrap_dm_task_no_open_count, 1);
	will_return(__wrap_dm_task_run, 1);
	will_return(__wrap_dm_task_get_names, 1);
	expect_string(__wrap_update_multipath, mapname, "foo");
	will_return(__wrap_update_multipath, 0);
	expect_string(__wrap_remove_map_by_alias, alias, "xyzzy");
	assert_int_equal(dmevent_loop(), 1);
	assert_int_equal(VECTOR_SIZE(waiter->events), 2);
	assert_int_equal(VECTOR_SIZE(data.dm_devices), 3);
	dev_evt = find_dmevents("foo");
	assert_ptr_not_equal(dev_evt, NULL);
	assert_int_equal(dev_evt->evt_nr, 6);
	assert_int_equal(dev_evt->action, EVENT_NOTHING);
	dev = find_dm_device("foo");
	assert_ptr_not_equal(dev, NULL);
	assert_int_equal(dev->evt_nr, 6);
	assert_int_equal(dev->update_nr, 6);
	dev_evt = find_dmevents("bar");
	assert_ptr_not_equal(dev_evt, NULL);
	assert_int_equal(dev_evt->evt_nr, 7);
	assert_int_equal(dev_evt->action, EVENT_NOTHING);
	dev = find_dm_device("bar");
	assert_ptr_not_equal(dev, NULL);
	assert_int_equal(dev->evt_nr, 7);
	assert_int_equal(dev->update_nr, 7);
	assert_ptr_equal(find_dmevents("xyzzy"), NULL);
	assert_ptr_equal(find_dm_device("xyzzy"), NULL);
}

/* watch another dm device and add events to two of them, so bar and
 * baz have new events, and foo doesn't. Set update_multipath to
 * fail for baz. Verify that baz is unwatched, bar is updated, and
 * foo stays the same. */
static void test_dmevent_loop_good2(void **state)
{
	struct dm_device *dev;
	struct dev_event *dev_evt;
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();

	assert_int_equal(add_dm_device_event("bar", 1, 9), 0);
	will_return(__wrap_dm_geteventnr, 0);
	assert_int_equal(watch_dmevents("baz"), 0);
	assert_int_equal(add_dm_device_event("baz", 1, 14), 0);
	will_return(__wrap_poll, 1);
	will_return(__wrap_ioctl, 0);
	will_return(__wrap_libmp_dm_task_create, &data);
	will_return(__wrap_dm_task_no_open_count, 1);
	will_return(__wrap_dm_task_run, 1);
	will_return(__wrap_dm_task_get_names, 1);
	expect_string(__wrap_update_multipath, mapname, "bar");
	will_return(__wrap_update_multipath, 0);
	expect_string(__wrap_update_multipath, mapname, "baz");
	will_return(__wrap_update_multipath, 1);
	assert_int_equal(dmevent_loop(), 1);
	assert_int_equal(VECTOR_SIZE(waiter->events), 2);
	assert_int_equal(VECTOR_SIZE(data.dm_devices), 2);
	dev_evt = find_dmevents("foo");
	assert_ptr_not_equal(dev_evt, NULL);
	assert_int_equal(dev_evt->evt_nr, 6);
	assert_int_equal(dev_evt->action, EVENT_NOTHING);
	dev = find_dm_device("foo");
	assert_ptr_not_equal(dev, NULL);
	assert_int_equal(dev->evt_nr, 6);
	assert_int_equal(dev->update_nr, 6);
	dev_evt = find_dmevents("bar");
	assert_ptr_not_equal(dev_evt, NULL);
	assert_int_equal(dev_evt->evt_nr, 9);
	assert_int_equal(dev_evt->action, EVENT_NOTHING);
	dev = find_dm_device("bar");
	assert_ptr_not_equal(dev, NULL);
	assert_int_equal(dev->evt_nr, 9);
	assert_int_equal(dev->update_nr, 9);
	assert_ptr_equal(find_dmevents("baz"), NULL);
	assert_ptr_equal(find_dm_device("baz"), NULL);
}

/* remove dm device foo, and unwatch events on bar. Verify that
 * foo is cleaned up and unwatched, and bar is no longer updated */
static void test_dmevent_loop_good3(void **state)
{
	struct dm_device *dev;
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();

	assert_int_equal(remove_dm_device_event("foo"), 0);
	unwatch_dmevents("bar");
	will_return(__wrap_poll, 1);
	will_return(__wrap_ioctl, 0);
	will_return(__wrap_libmp_dm_task_create, &data);
	will_return(__wrap_dm_task_no_open_count, 1);
	will_return(__wrap_dm_task_run, 1);
	will_return(__wrap_dm_task_get_names, 1);
	expect_string(__wrap_remove_map_by_alias, alias, "foo");
	assert_int_equal(dmevent_loop(), 1);
	assert_int_equal(VECTOR_SIZE(waiter->events), 0);
	assert_int_equal(VECTOR_SIZE(data.dm_devices), 1);
	dev = find_dm_device("bar");
	assert_ptr_not_equal(dev, NULL);
	assert_int_equal(dev->evt_nr, 9);
	assert_int_equal(dev->update_nr, 9);
	assert_ptr_equal(find_dmevents("foo"), NULL);
	assert_ptr_equal(find_dmevents("bar"), NULL);
	assert_ptr_equal(find_dm_device("foo"), NULL);
}


/* verify that rearming the dmevents polling works */
static void test_arm_poll(void **state)
{
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();
	will_return(__wrap_ioctl, 0);
	assert_int_equal(arm_dm_event_poll(waiter->fd), 0);
}

/* verify that the waiter is cleaned up */
static void test_cleanup_waiter(void **state)
{
	struct test_data *datap = (struct test_data *)(*state);
	if (datap == NULL)
		skip();
	cleanup_dmevent_waiter();
	assert_ptr_equal(waiter, NULL);
}

int test_dmevents(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_init_waiter_bad0),
		cmocka_unit_test(test_init_waiter_bad1),
		cmocka_unit_test(test_init_waiter_good0),
		cmocka_unit_test(test_watch_dmevents_bad0),
		cmocka_unit_test(test_watch_dmevents_bad1),
		cmocka_unit_test(test_watch_dmevents_bad2),
		cmocka_unit_test(test_watch_dmevents_good0),
		cmocka_unit_test(test_watch_dmevents_good1),
		cmocka_unit_test(test_watch_dmevents_good2),
		cmocka_unit_test(test_get_events_bad0),
		cmocka_unit_test(test_get_events_bad1),
		cmocka_unit_test(test_get_events_bad2),
		cmocka_unit_test(test_get_events_good0),
		cmocka_unit_test(test_get_events_good1),
		cmocka_unit_test(test_arm_poll),
		cmocka_unit_test(test_dmevent_loop_bad0),
		cmocka_unit_test(test_dmevent_loop_bad1),
		cmocka_unit_test(test_dmevent_loop_bad2),
		cmocka_unit_test(test_dmevent_loop_good0),
		cmocka_unit_test(test_dmevent_loop_good1),
		cmocka_unit_test(test_dmevent_loop_good2),
		cmocka_unit_test(test_dmevent_loop_good3),
		cmocka_unit_test(test_cleanup_waiter),
	};
	return cmocka_run_group_tests(tests, setup, teardown);
}

int main(void)
{
	int ret = 0;

	ret += test_dmevents();
	return ret;
}
