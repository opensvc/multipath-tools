/*
 * uevent.c - trigger upon netlink uevents from the kernel
 *
 *	Only kernels from version 2.6.10* on provide the uevent netlink socket.
 *	Until the libc-kernel-headers are updated, you need to compile with:
 *
 *	  gcc -I /lib/modules/`uname -r`/build/include -o uevent_listen uevent_listen.c
 *
 * Copyright (C) 2004 Kay Sievers <kay.sievers@vrfy.org>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation version 2 of the License.
 *
 *	This program is distributed in the hope that it will be useful, but
 *	WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *	General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License along
 *	with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/user.h>
#include <sys/un.h>
#include <poll.h>
#include <linux/types.h>
#include <linux/netlink.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <libudev.h>

#include "debug.h"
#include "list.h"
#include "uevent.h"
#include "vector.h"
#include "structs.h"
#include "util.h"
#include "config.h"
#include "blacklist.h"
#include "devmapper.h"

typedef int (uev_trigger)(struct uevent *, void * trigger_data);

static LIST_HEAD(uevq);
static pthread_mutex_t uevq_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t *uevq_lockp = &uevq_lock;
static pthread_cond_t uev_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t *uev_condp = &uev_cond;
static uev_trigger *my_uev_trigger;
static void *my_trigger_data;
static int servicing_uev;

struct uevent_filter_state {
	struct list_head uevq;
	struct list_head *old_tail;
	struct config *conf;
	unsigned long added;
	unsigned long discarded;
	unsigned long filtered;
	unsigned long merged;
};

static void reset_filter_state(struct uevent_filter_state *st)
{
	st->added = st->discarded = st->filtered = st->merged = 0;
}

int is_uevent_busy(void)
{
	int empty;

	pthread_mutex_lock(uevq_lockp);
	empty = list_empty(&uevq);
	pthread_mutex_unlock(uevq_lockp);
	return (!empty || servicing_uev);
}

struct uevent * alloc_uevent (void)
{
	struct uevent *uev = calloc(1, sizeof(struct uevent));

	if (uev) {
		INIT_LIST_HEAD(&uev->node);
		INIT_LIST_HEAD(&uev->merge_node);
	}

	return uev;
}

static void uevq_cleanup(struct list_head *tmpq);

static void cleanup_uev(void *arg)
{
	struct uevent *uev = arg;

	uevq_cleanup(&uev->merge_node);
	if (uev->udev)
		udev_device_unref(uev->udev);
	free(uev);
}

static void uevq_cleanup(struct list_head *tmpq)
{
	struct uevent *uev, *tmp;

	list_for_each_entry_safe(uev, tmp, tmpq, node) {
		list_del_init(&uev->node);
		cleanup_uev(uev);
	}
}

static const char* uevent_get_env_var(const struct uevent *uev,
				      const char *attr)
{
	int i;
	size_t len;
	const char *p = NULL;

	if (attr == NULL)
		goto invalid;

	len = strlen(attr);
	if (len == 0)
		goto invalid;

	for (i = 0; uev->envp[i] != NULL; i++) {
		const char *var = uev->envp[i];

		if (strlen(var) > len &&
		    !memcmp(var, attr, len) && var[len] == '=') {
			p = var + len + 1;
			break;
		}
	}

	condlog(4, "%s: %s -> '%s'", __func__, attr, p ?: "(null)");
	return p;

invalid:
	condlog(2, "%s: empty variable name", __func__);
	return NULL;
}

int uevent_get_env_positive_int(const struct uevent *uev,
				       const char *attr)
{
	const char *p = uevent_get_env_var(uev, attr);
	char *q;
	int ret;

	if (p == NULL || *p == '\0')
		return -1;

	ret = strtoul(p, &q, 10);
	if (*q != '\0' || ret < 0) {
		condlog(2, "%s: invalid %s: '%s'", __func__, attr, p);
		return -1;
	}
	return ret;
}

void
uevent_get_wwid(struct uevent *uev, const struct config *conf)
{
	const char *uid_attribute;
	const char *val;

	uid_attribute = get_uid_attribute_by_attrs(conf, uev->kernel);
	val = uevent_get_env_var(uev, uid_attribute);
	if (val)
		uev->wwid = val;
}

static bool uevent_need_merge(const struct config *conf)
{
	return VECTOR_SIZE(&conf->uid_attrs) > 0;
}

static bool uevent_can_discard(struct uevent *uev, const struct config *conf)
{
	/*
	 * do not filter dm devices by devnode
	 */
	if (!strncmp(uev->kernel, "dm-", 3))
		return false;
	/*
	 * filter paths devices by devnode
	 */
	if (filter_devnode(conf->blist_devnode, conf->elist_devnode,
			   uev->kernel) > 0)
		return true;

	return false;
}

static bool
uevent_can_filter(struct uevent *earlier, struct uevent *later)
{

	if (!strncmp(later->kernel, "dm-", 3) ||
	    strcmp(earlier->kernel, later->kernel))
		return false;

	/*
	 * filter earlier uvents if path has removed later. Eg:
	 * "add path1 |chang path1 |add path2 |remove path1"
	 * can filter as:
	 * "add path2 |remove path1"
	 * uevents "add path1" and "chang path1" are filtered out
	 */
	if (!strcmp(later->action, "remove"))
		return true;

	/*
	 * filter change uvents if add uevents exist. Eg:
	 * "change path1| add path1 |add path2"
	 * can filter as:
	 * "add path1 |add path2"
	 * uevent "chang path1" is filtered out
	 */
	if (!strcmp(earlier->action, "change") &&
	    !strcmp(later->action, "add"))
		return true;

	return false;
}

static bool
merge_need_stop(struct uevent *earlier, struct uevent *later)
{
	/*
	 * dm uevent do not try to merge with left uevents
	 */
	if (!strncmp(later->kernel, "dm-", 3))
		return true;

	/*
	 * we can not make a jugement without wwid,
	 * so it is sensible to stop merging
	 */
	if (!earlier->wwid || !later->wwid)
		return true;
	/*
	 * uevents merging stopped
	 * when we meet an opposite action uevent from the same LUN to AVOID
	 * "add path1 |remove path1 |add path2 |remove path2 |add path3"
	 * to merge as "remove path1, path2" and "add path1, path2, path3"
	 * OR
	 * "remove path1 |add path1 |remove path2 |add path2 |remove path3"
	 * to merge as "add path1, path2" and "remove path1, path2, path3"
	 * SO
	 * when we meet a non-change uevent from the same LUN
	 * with the same wwid and different action
	 * it would be better to stop merging.
	 */
	if (strcmp(earlier->action, later->action) &&
	    strcmp(earlier->action, "change") &&
	    strcmp(later->action, "change") &&
	    !strcmp(earlier->wwid, later->wwid))
		return true;

	return false;
}

static bool
uevent_can_merge(struct uevent *earlier, struct uevent *later)
{
	/* merge paths uevents
	 * whose wwids exist and are same
	 * and actions are same,
	 * and actions are addition or deletion
	 */
	if (earlier->wwid && later->wwid &&
	    strncmp(earlier->kernel, "dm-", 3) &&
	    !strcmp(earlier->action, later->action) &&
	    (!strcmp(earlier->action, "add") ||
	     !strcmp(earlier->action, "remove")) &&
	    !strcmp(earlier->wwid, later->wwid))
		return true;

	return false;
}

static void uevent_delete_from_list(struct uevent *to_delete,
				    struct uevent **previous,
				    struct list_head **old_tail)
{
	/*
	 * "old_tail" is the list_head before the last list element to which
	 * the caller iterates (the list anchor if the caller iterates over
	 * the entire list). If this element is removed (which can't happen
	 * for the anchor), "old_tail" must be moved. It can happen that
	 * "old_tail" ends up pointing at the anchor.
	 */
	if (*old_tail == &to_delete->node)
		*old_tail = to_delete->node.prev;

	list_del_init(&to_delete->node);

	/*
	 * The "to_delete" uevent has been merged with other uevents
	 * previously. Re-insert them into the list, at the point we're
	 * currently at. This must be done after the list_del_init() above,
	 * otherwise previous->next would still point to to_delete.
	 */
	if (!list_empty(&to_delete->merge_node)) {
		struct uevent *last = list_entry(to_delete->merge_node.prev,
						 typeof(*last), node);

		condlog(3, "%s: deleted uevent \"%s %s\" with merged uevents",
			__func__, to_delete->action, to_delete->kernel);
		list_splice(&to_delete->merge_node, &(*previous)->node);
		*previous = last;
	}
	if (to_delete->udev)
		udev_device_unref(to_delete->udev);

	free(to_delete);
}

/*
 * Use this function to delete events that are known not to
 * be equal to old_tail, and have an empty merge_node list.
 * For others, use uevent_delete_from_list().
 */
static void uevent_delete_simple(struct uevent *to_delete)
{
	list_del_init(&to_delete->node);

	if (to_delete->udev)
		udev_device_unref(to_delete->udev);

	free(to_delete);
}

static void uevent_prepare(struct uevent_filter_state *st)
{
	struct uevent *uev, *tmp;

	list_for_some_entry_reverse_safe(uev, tmp, &st->uevq, st->old_tail, node) {

		st->added++;
		if (uevent_can_discard(uev, st->conf)) {
			uevent_delete_simple(uev);
			st->discarded++;
			continue;
		}

		if (strncmp(uev->kernel, "dm-", 3) &&
		    uevent_need_merge(st->conf))
			uevent_get_wwid(uev, st->conf);
	}
}

static void
uevent_filter(struct uevent *later, struct uevent_filter_state *st)
{
	struct uevent *earlier, *tmp;

	list_for_some_entry_reverse_safe(earlier, tmp, &later->node, &st->uevq, node) {
		/*
		 * filter unnessary earlier uevents
		 * by the later uevent
		 */
		if (!list_empty(&earlier->merge_node)) {
			struct uevent *mn, *t;

			list_for_each_entry_reverse_safe(mn, t, &earlier->merge_node, node) {
				if (uevent_can_filter(mn, later)) {
					condlog(4, "uevent: \"%s %s\" (merged into \"%s %s\") filtered by \"%s %s\"",
						mn->action, mn->kernel,
						earlier->action, earlier->kernel,
						later->action, later->kernel);
					uevent_delete_simple(mn);
					st->filtered++;
				}
			}
		}
		if (uevent_can_filter(earlier, later)) {
			condlog(3, "uevent: %s-%s has filtered by uevent: %s-%s",
				earlier->kernel, earlier->action,
				later->kernel, later->action);

			uevent_delete_from_list(earlier, &tmp, &st->old_tail);
			st->filtered++;
		}
	}
}

static void uevent_merge(struct uevent *later, struct uevent_filter_state *st)
{
	struct uevent *earlier, *tmp;

	list_for_some_entry_reverse_safe(earlier, tmp, &later->node, &st->uevq, node) {
		if (merge_need_stop(earlier, later))
			break;
		/*
		 * merge earlier uevents to the later uevent
		 */
		if (uevent_can_merge(earlier, later)) {
			condlog(3, "merged uevent: %s-%s-%s with uevent: %s-%s-%s",
				earlier->action, earlier->kernel, earlier->wwid,
				later->action, later->kernel, later->wwid);

			/* See comment in uevent_delete_from_list() */
			if (&earlier->node == st->old_tail)
				st->old_tail = earlier->node.prev;

			list_move(&earlier->node, &later->merge_node);
			list_splice_init(&earlier->merge_node,
					 &later->merge_node);
			st->merged++;
		}
	}
}

static void merge_uevq(struct uevent_filter_state *st)
{
	struct uevent *later;

	uevent_prepare(st);

	list_for_some_entry_reverse(later, &st->uevq, st->old_tail, node)
		uevent_filter(later, st);

	if(uevent_need_merge(st->conf))
		list_for_some_entry_reverse(later, &st->uevq, st->old_tail, node)
			uevent_merge(later, st);
}

static void
service_uevq(struct list_head *tmpq)
{
	struct uevent *uev = list_pop_entry(tmpq, typeof(*uev), node);

	if (uev == NULL)
		return;
	condlog(4, "servicing uevent '%s %s'", uev->action, uev->kernel);
	pthread_cleanup_push(cleanup_uev, uev);
	if (my_uev_trigger && my_uev_trigger(uev, my_trigger_data))
		condlog(0, "uevent trigger error");
	pthread_cleanup_pop(1);
}

static void uevent_cleanup(void *arg)
{
	struct udev *udev = arg;

	condlog(3, "Releasing uevent_listen() resources");
	udev_unref(udev);
}

static void monitor_cleanup(void *arg)
{
	struct udev_monitor *monitor = arg;

	condlog(3, "Releasing uevent_monitor() resources");
	udev_monitor_unref(monitor);
}

static void cleanup_uevq(void *arg)
{
	uevq_cleanup(arg);
}

static void cleanup_global_uevq(void *arg __attribute__((unused)))
{
	pthread_mutex_lock(uevq_lockp);
	uevq_cleanup(&uevq);
	pthread_mutex_unlock(uevq_lockp);
}

static void log_filter_state(const struct uevent_filter_state *st)
{
	if (st->added == 0 && st->filtered == 0 && st->merged == 0)
		return;

	condlog(3, "uevents: %lu added, %lu discarded, %lu filtered, %lu merged",
		st->added, st->discarded, st->filtered, st->merged);
}

/*
 * Service the uevent queue.
 */
int uevent_dispatch(int (*uev_trigger)(struct uevent *, void * trigger_data),
		    void * trigger_data)
{
	struct uevent_filter_state filter_state;

	INIT_LIST_HEAD(&filter_state.uevq);
	my_uev_trigger = uev_trigger;
	my_trigger_data = trigger_data;

	mlockall(MCL_CURRENT | MCL_FUTURE);

	pthread_cleanup_push(cleanup_uevq, &filter_state.uevq);
	while (1) {
		pthread_cleanup_push(cleanup_mutex, uevq_lockp);
		pthread_mutex_lock(uevq_lockp);

		servicing_uev = !list_empty(&filter_state.uevq);

		while (list_empty(&filter_state.uevq) && list_empty(&uevq)) {
			condlog(4, "%s: waiting for events", __func__);
			pthread_cond_wait(uev_condp, uevq_lockp);
			condlog(4, "%s: waking up", __func__);
		}

		servicing_uev = 1;
		/*
		 * "old_tail" is the list element towards which merge_uevq()
		 * will iterate: the last element of uevq before
		 * appending new uevents. If uveq  empty, uevq.prev
		 * equals &uevq, which is what we need.
		 */
		filter_state.old_tail = filter_state.uevq.prev;
		list_splice_tail_init(&uevq, &filter_state.uevq);
		pthread_cleanup_pop(1);

		if (!my_uev_trigger)
			break;

		reset_filter_state(&filter_state);
		pthread_cleanup_push(put_multipath_config, filter_state.conf);
		filter_state.conf = get_multipath_config();
		merge_uevq(&filter_state);
		pthread_cleanup_pop(1);
		log_filter_state(&filter_state);

		service_uevq(&filter_state.uevq);
	}
	pthread_cleanup_pop(1);
	condlog(3, "Terminating uev service queue");
	return 0;
}

static struct uevent *uevent_from_udev_device(struct udev_device *dev)
{
	struct uevent *uev;
	int i = 0;
	char *pos, *end;
	struct udev_list_entry *list_entry;

	uev = alloc_uevent();
	if (!uev) {
		udev_device_unref(dev);
		condlog(1, "lost uevent, oom");
		return NULL;
	}
	pos = uev->buffer;
	end = pos + HOTPLUG_BUFFER_SIZE + OBJECT_SIZE - 1;
	udev_list_entry_foreach(list_entry, udev_device_get_properties_list_entry(dev)) {
		const char *name, *value;
		int bytes;

		name = udev_list_entry_get_name(list_entry);
		if (!name)
			name = "(null)";
		value = udev_list_entry_get_value(list_entry);
		if (!value)
			value = "(null)";
		bytes = snprintf(pos, end - pos, "%s=%s", name, value);
		if (pos + bytes >= end) {
			condlog(2, "buffer overflow for uevent");
			break;
		}
		uev->envp[i] = pos;
		pos += bytes;
		*pos = '\0';
		pos++;
		if (strcmp(name, "DEVPATH") == 0)
			uev->devpath = uev->envp[i] + 8;
		if (strcmp(name, "ACTION") == 0)
			uev->action = uev->envp[i] + 7;
		i++;
		if (i == HOTPLUG_NUM_ENVP - 1)
			break;
	}
	if (!uev->devpath || ! uev->action) {
		udev_device_unref(dev);
		condlog(1, "uevent missing necessary fields");
		free(uev);
		return NULL;
	}
	uev->udev = dev;
	uev->envp[i] = NULL;

	condlog(3, "uevent '%s' from '%s'", uev->action, uev->devpath);
	uev->kernel = strrchr(uev->devpath, '/');
	if (uev->kernel)
		uev->kernel++;

	/* print payload environment */
	for (i = 0; uev->envp[i] != NULL; i++)
		condlog(5, "%s", uev->envp[i]);
	return uev;
}

#define MAX_UEVENTS 1000
static int uevent_receive_events(int fd, struct list_head *tmpq,
				 struct udev_monitor *monitor)
{
	struct pollfd ev_poll;
	int n = 0;

	do {
		struct uevent *uev;
		struct udev_device *dev;

		dev = udev_monitor_receive_device(monitor);
		if (!dev) {
			condlog(0, "failed getting udev device");
			break;
		}
		uev = uevent_from_udev_device(dev);
		if (!uev)
			break;

		list_add_tail(&uev->node, tmpq);
		n++;
		condlog(4, "received uevent \"%s %s\"", uev->action, uev->kernel);

		ev_poll.fd = fd;
		ev_poll.events = POLLIN;

	} while (n < MAX_UEVENTS && poll(&ev_poll, 1, 0) > 0);

	return n;
}

int uevent_listen(struct udev *udev)
{
	int err = 2;
	struct udev_monitor *monitor = NULL;
	int fd, socket_flags;
	LIST_HEAD(uevlisten_tmp);

	/*
	 * Queue uevents for service by dedicated thread so that the uevent
	 * listening thread does not block on multipathd locks (vecs->lock)
	 * thereby not getting to empty the socket's receive buffer queue
	 * often enough.
	 */
	if (!udev) {
		condlog(1, "no udev context");
		return 1;
	}
	udev_ref(udev);
	pthread_cleanup_push(uevent_cleanup, udev);

	monitor = udev_monitor_new_from_netlink(udev, "udev");
	if (!monitor) {
		condlog(2, "failed to create udev monitor");
		goto out_udev;
	}
	pthread_cleanup_push(monitor_cleanup, monitor);
#ifdef LIBUDEV_API_RECVBUF
	if (udev_monitor_set_receive_buffer_size(monitor, 128 * 1024 * 1024) < 0)
		condlog(2, "failed to increase buffer size");
#endif
	fd = udev_monitor_get_fd(monitor);
	if (fd < 0) {
		condlog(2, "failed to get monitor fd");
		goto out;
	}
	socket_flags = fcntl(fd, F_GETFL);
	if (socket_flags < 0) {
		condlog(2, "failed to get monitor socket flags : %s",
			strerror(errno));
		goto out;
	}
	if (fcntl(fd, F_SETFL, socket_flags & ~O_NONBLOCK) < 0) {
		condlog(2, "failed to set monitor socket flags : %s",
			strerror(errno));
		goto out;
	}
	err = udev_monitor_filter_add_match_subsystem_devtype(monitor, "block",
							      "disk");
	if (err)
		condlog(2, "failed to create filter : %s", strerror(-err));
	err = udev_monitor_enable_receiving(monitor);
	if (err) {
		condlog(2, "failed to enable receiving : %s", strerror(-err));
		goto out;
	}

	pthread_cleanup_push(cleanup_global_uevq, NULL);
	pthread_cleanup_push(cleanup_uevq, &uevlisten_tmp);
	while (1) {
		int fdcount, events;
		struct pollfd ev_poll = { .fd = fd, .events = POLLIN, };

		fdcount = poll(&ev_poll, 1, -1);
		if (fdcount < 0) {
			if (errno == EINTR)
				continue;

			condlog(0, "error receiving uevent message: %m");
			err = -errno;
			break;
		}
		events = uevent_receive_events(fd, &uevlisten_tmp, monitor);
		if (events <= 0)
			continue;

		condlog(4, "Forwarding %d uevents", events);
		pthread_mutex_lock(uevq_lockp);
		list_splice_tail_init(&uevlisten_tmp, &uevq);
		pthread_cond_signal(uev_condp);
		pthread_mutex_unlock(uevq_lockp);
	}
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
out:
	pthread_cleanup_pop(1);
out_udev:
	pthread_cleanup_pop(1);
	return err;
}

char *uevent_get_dm_str(const struct uevent *uev, char *attr)
{
	const char *tmp = uevent_get_env_var(uev, attr);

	if (tmp == NULL)
		return NULL;
	return strdup(tmp);
}

bool uevent_is_mpath(const struct uevent *uev)
{
	const char *uuid = uevent_get_env_var(uev, "DM_UUID");

	if (uuid == NULL)
		return false;
	if (strncmp(uuid, UUID_PREFIX, UUID_PREFIX_LEN))
		return false;
	return uuid[UUID_PREFIX_LEN] != '\0';
}
