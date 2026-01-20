// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 Brian Bunker <brian@purestorage.com>
 * Copyright (C) 2025 Krishna Kant <krishna.kant@purestorage.com>
 */

#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <libudev.h>
#include <urcu.h>

#include "vector.h"
#include "structs.h"
#include "structs_vec.h"
#include "debug.h"
#include "util.h"
#include "lock.h"
#include "sysfs.h"
#include "list.h"
#include "purge.h"

pthread_mutex_t purge_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t purge_cond = PTHREAD_COND_INITIALIZER;
LIST_HEAD(purge_queue);

/*
 * Information needed to purge a path. We copy this data while holding
 * vecs->lock, then release the lock before doing the actual sysfs write.
 * This prevents blocking other operations while waiting for sysfs I/O.
 *
 * The udev device reference captures the sysfs path (including H:C:T:L).
 * The duplicated fd prevents device name/number reuse: the kernel will not
 * reuse the device's minor number (which maps to the device name) for a new
 * device while we hold an open file descriptor, even if the original device
 * has been removed. This protects against deleting a new device that reused
 * the same name after the original was removed externally.
 */
struct purge_path_info {
	struct list_head node;	  /* List linkage */
	struct udev_device *udev; /* Udev device (refcounted) */
	int fd;			  /* Dup'd fd prevents device reuse */
};

/*
 * Attempt to delete a path by writing to the SCSI device's sysfs delete
 * attribute. This triggers kernel-level device removal. The actual cleanup
 * of the path structure from pathvec happens later when a uevent arrives
 * (handled by uev_remove_path).
 *
 * This function does NOT require vecs->lock to be held, as it operates on
 * copied data. This function may block while writing to sysfs, which is
 * why it's called without holding any locks.
 *
 * Protection against device reuse:
 * The duplicated fd in purge_path_info prevents the kernel from reusing
 * the device's minor number (and thus the device name like /dev/sdd) for
 * a new device, even if the original device has been removed externally.
 * This ensures we cannot accidentally delete a new device that reused the
 * same name. The kernel maintains this guarantee as long as we hold the
 * open file descriptor.
 */
static void delete_path_sysfs(struct purge_path_info *info)
{
	struct udev_device *ud;
	const char *devname;

	if (!info->udev)
		goto out;

	devname = udev_device_get_devnode(info->udev);

	/*
	 * Get the SCSI device parent. This is where we'll write to the
	 * "delete" attribute to trigger device removal.
	 */
	ud = udev_device_get_parent_with_subsystem_devtype(info->udev, "scsi",
							   "scsi_device");
	if (!ud) {
		condlog(3, "%s: failed to purge, no SCSI parent found", devname);
		goto out;
	}

	/*
	 * Write "1" to the SCSI device's delete attribute to trigger
	 * kernel-level device removal.
	 */
	if (sysfs_attr_set_value(ud, "delete", "1", 1) < 0)
		condlog(3, "%s: failed to purge", devname);
	else
		condlog(2, "%s: purged", devname);

out:
	return;
}

/*
 * Prepare purge info for a path while holding vecs->lock.
 * Takes a reference on the udev device and duplicates the fd.
 * Returns allocated purge_path_info on success, NULL on failure.
 *
 * We require a valid fd because it prevents the kernel from reusing
 * the device's minor number (and device name) for a new device while
 * we hold it open. This protects against accidentally deleting a new
 * device that reused the same name after the original was removed.
 */
static struct purge_path_info *prepare_purge_path_info(struct path *pp)
{
	struct purge_path_info *info = NULL;

	if (!pp->udev || !pp->mpp)
		goto out;

	/*
	 * We require a valid fd to prevent device name reuse.
	 * Without it, we cannot safely purge the device.
	 */
	if (pp->fd < 0) {
		condlog(3, "%s: no fd available, cannot safely purge", pp->dev);
		goto out;
	}

	info = calloc(1, sizeof(*info));
	if (!info)
		goto out;

	INIT_LIST_HEAD(&info->node);
	info->udev = udev_device_ref(pp->udev);
	if (!info->udev)
		goto out_free;

	info->fd = dup(pp->fd);
	if (info->fd < 0) {
		condlog(3, "%s: failed to dup fd: %s, cannot safely purge",
			pp->dev, strerror(errno));
		goto out_unref;
	}

	return info;

out_unref:
	udev_device_unref(info->udev);
out_free:
	free(info);
	info = NULL;
out:
	return info;
}

/*
 * Clean up and free purge info.
 */
static void free_purge_path_info(struct purge_path_info *info)
{
	if (!info)
		return;

	if (info->fd >= 0)
		close(info->fd);
	if (info->udev)
		udev_device_unref(info->udev);
	free(info);
}

/*
 * Build a list of purge_path_info for all paths marked for purge.
 * This should be called while holding vecs->lock. It clears the
 * disconnected flag and prepares purge info for each path, adding
 * them to tmpq.
 */
void build_purge_list(struct vectors *vecs, struct list_head *tmpq)
{
	struct path *pp;
	unsigned int i;

	vector_foreach_slot (vecs->pathvec, pp, i) {
		struct purge_path_info *info;

		if (pp->disconnected != DISCONNECTED_READY_FOR_PURGE)
			continue;

		/*
		 * Mark as queued whether we succeed or fail.
		 * On success, we're purging it now.
		 * On failure, retrying is unlikely to help until
		 * the checker re-evaluates the path.
		 */
		pp->disconnected = DISCONNECTED_QUEUED_FOR_PURGE;

		info = prepare_purge_path_info(pp);
		if (info) {
			condlog(2, "%s: queuing path for purge", pp->dev);
			list_add_tail(&info->node, tmpq);
		} else
			condlog(3, "%s: failed to prepare purge info", pp->dev);
	}
}

static void rcu_unregister(__attribute__((unused)) void *param)
{
	rcu_unregister_thread();
}

/*
 * Cleanup handler for a single purge_path_info.
 * Used to prevent memory leaks if thread is cancelled while processing.
 */
static void cleanup_purge_path_info(void *arg)
{
	struct purge_path_info *info = arg;

	free_purge_path_info(info);
}

/*
 * Cleanup handler for purge list. Frees all purge_path_info entries.
 * Can be called as a pthread cleanup handler or directly.
 */
void cleanup_purge_list(void *arg)
{
	struct list_head *purge_list = arg;
	struct purge_path_info *info, *tmp;

	list_for_each_entry_safe(info, tmp, purge_list, node)
	{
		list_del_init(&info->node);
		free_purge_path_info(info);
	}
}

/*
 * Cleanup handler for the global purge queue.
 * Used during shutdown to free any remaining queued items.
 */
static void cleanup_global_purge_queue(void *arg __attribute__((unused)))
{
	pthread_mutex_lock(&purge_mutex);
	cleanup_purge_list(&purge_queue);
	pthread_mutex_unlock(&purge_mutex);
}

/*
 * Main purge thread loop.
 *
 * This thread waits for purge_path_info structs to be queued by the checker
 * thread, then processes them by writing to their sysfs delete attributes.
 * The checker thread builds the list while holding vecs->lock, so this
 * thread doesn't need to grab that lock at all.
 *
 * Uses list_splice_tail_init() like uevent_dispatch() to safely transfer
 * items from the global queue to a local list for processing.
 *
 * Cleanup handlers are registered for both the local purge_list and the
 * global purge_queue (similar to uevent_listen), and for each individual
 * purge_path_info after it's popped off the list (similar to service_uevq).
 * This ensures no memory leaks if the thread is cancelled at any point.
 */
void *purgeloop(void *ap __attribute__((unused)))
{
	pthread_cleanup_push(rcu_unregister, NULL);
	rcu_register_thread();
	mlockall(MCL_CURRENT | MCL_FUTURE);

	/*
	 * Cleanup handler for global purge_queue.
	 * This handles items that were queued but not yet moved to purge_list.
	 */
	pthread_cleanup_push(cleanup_global_purge_queue, NULL);

	while (1) {
		LIST_HEAD(purge_list);
		struct purge_path_info *info;

		/*
		 * Cleanup handler for local purge_list.
		 * This handles items that were moved from purge_queue but
		 * not yet processed.
		 */
		pthread_cleanup_push(cleanup_purge_list, &purge_list);

		/*
		 * Cleanup handler for purge_mutex.
		 * Note: pthread_cond_wait() reacquires the mutex before
		 * returning, even on cancellation, so this cleanup handler
		 * will properly unlock it if we're cancelled.
		 */
		pthread_cleanup_push(cleanup_mutex, &purge_mutex);
		pthread_mutex_lock(&purge_mutex);
		pthread_testcancel();
		while (list_empty(&purge_queue)) {
			condlog(4, "purgeloop waiting for work");
			pthread_cond_wait(&purge_cond, &purge_mutex);
		}
		list_splice_tail_init(&purge_queue, &purge_list);
		pthread_cleanup_pop(1);

		/*
		 * Process all paths in the list without holding any locks.
		 * The sysfs operations may block, but that's fine since we're
		 * not holding vecs->lock.
		 *
		 * After popping each info off the list, we immediately push
		 * a cleanup handler for it. This ensures it gets freed even
		 * if we're cancelled inside delete_path_sysfs().
		 */
		while ((info = list_pop_entry(&purge_list, typeof(*info), node))) {
			pthread_cleanup_push(cleanup_purge_path_info, info);
			delete_path_sysfs(info);
			pthread_cleanup_pop(1);
		}

		/*
		 * Pop cleanup handler without executing it (0) since we've
		 * already freed everything above. The handler only runs if
		 * the thread is cancelled during processing.
		 */
		pthread_cleanup_pop(0);
	}

	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
	return NULL;
}
