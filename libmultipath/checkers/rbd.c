/*
 * Copyright (c) 2016 Red Hat
 * Copyright (c) 2004 Christophe Varoqui
 *
 * Code based off of tur.c and ceph's krbd.cc
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <libudev.h>
#include <ifaddrs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/wait.h>

#include "rados/librados.h"

#include "structs.h"
#include "checkers.h"

#include "../libmultipath/debug.h"
#include "../libmultipath/uevent.h"
#include "../libmultipath/time-util.h"

struct rbd_checker_context;
typedef int (thread_fn)(struct rbd_checker_context *ct, char *msg);

#define RBD_MSG(msg, fmt, args...) snprintf(msg, CHECKER_MSG_LEN, fmt, ##args);

#define RBD_FEATURE_EXCLUSIVE_LOCK	(1 << 2)

struct rbd_checker_context {
	int rbd_bus_id;
	char *client_addr;
	char *config_info;
	char *snap;
	char *pool;
	char *image;
	char *username;
	int remapped;
	int blacklisted;

	rados_t cluster;

	int state;
	int running;
	time_t time;
	thread_fn *fn;
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t active;
	pthread_spinlock_t hldr_lock;
	int holders;
	char message[CHECKER_MSG_LEN];
};

int libcheck_init(struct checker * c)
{
	struct rbd_checker_context *ct;
	struct udev_device *block_dev;
	struct udev_device *bus_dev;
	struct udev *udev;
	struct stat sb;
	const char *block_name, *addr, *config_info, *features_str;
	const char *image, *pool, *snap, *username;
	uint64_t features = 0;
	char sysfs_path[PATH_SIZE];
	int ret;

	ct = malloc(sizeof(struct rbd_checker_context));
	if (!ct)
		return 1;
	memset(ct, 0, sizeof(struct rbd_checker_context));
	ct->holders = 1;
	pthread_cond_init_mono(&ct->active);
	pthread_mutex_init(&ct->lock, NULL);
	pthread_spin_init(&ct->hldr_lock, PTHREAD_PROCESS_PRIVATE);
	c->context = ct;

	/*
	 * The rbd block layer sysfs device is not linked to the rbd bus
	 * device that we interact with, so figure that out now.
	 */
	if (fstat(c->fd, &sb) != 0)
		goto free_ct;

	udev = udev_new();
	if (!udev)
		goto free_ct;

	block_dev = udev_device_new_from_devnum(udev, 'b', sb.st_rdev);
	if (!block_dev)
		goto free_udev;

	block_name  = udev_device_get_sysname(block_dev);
	ret = sscanf(block_name, "rbd%d", &ct->rbd_bus_id);

	udev_device_unref(block_dev);
	if (ret != 1)
		goto free_udev;

	snprintf(sysfs_path, sizeof(sysfs_path), "/sys/bus/rbd/devices/%d",
		 ct->rbd_bus_id);
	bus_dev = udev_device_new_from_syspath(udev, sysfs_path);
	if (!bus_dev)
		goto free_udev;

	addr = udev_device_get_sysattr_value(bus_dev, "client_addr");
	if (!addr) {
		condlog(0, "rbd%d: Could not find client_addr in rbd sysfs. "
			"Try updating kernel", ct->rbd_bus_id);
		goto free_dev;
	}

	ct->client_addr = strdup(addr);
	if (!ct->client_addr)
		goto free_dev;

	features_str = udev_device_get_sysattr_value(bus_dev, "features");
	if (!features_str)
		goto free_addr;
	features = strtoll(features_str, NULL, 16);
	if (!(features & RBD_FEATURE_EXCLUSIVE_LOCK)) {
		condlog(3, "rbd%d: Exclusive lock not set.", ct->rbd_bus_id);
		goto free_addr;
	}

	config_info = udev_device_get_sysattr_value(bus_dev, "config_info");
	if (!config_info)
		goto free_addr;

	if (!strstr(config_info, "noshare")) {
		condlog(3, "rbd%d: Only nonshared clients supported.",
			ct->rbd_bus_id);
		goto free_addr;
	}

	ct->config_info = strdup(config_info);
	if (!ct->config_info)
		goto free_addr;

	username = strstr(config_info, "name=");
	if (username) {
		char *end;
		int len;

		username += 5;
		end = strchr(username, ',');
		if (!end)
			goto free_info;
		len = end - username;

		ct->username = malloc(len + 1);
		if (!ct->username)
			goto free_info;
		strncpy(ct->username, username, len);
		ct->username[len] = '\0';
	}

	image = udev_device_get_sysattr_value(bus_dev, "name");
	if (!image)
		goto free_username;

	ct->image = strdup(image);
	if (!ct->image)
		goto free_info;

	pool = udev_device_get_sysattr_value(bus_dev, "pool");
	if (!pool)
		goto free_image;

	ct->pool = strdup(pool);
	if (!ct->pool)
		goto free_image;

	snap = udev_device_get_sysattr_value(bus_dev, "current_snap");
	if (!snap)
		goto free_pool;

	if (strcmp("-", snap)) {
		ct->snap = strdup(snap);
		if (!ct->snap)
			goto free_pool;
	}

	if (rados_create(&ct->cluster, NULL) < 0) {
		condlog(0, "rbd%d: Could not create rados cluster",
			ct->rbd_bus_id);
		goto free_snap;
	}

	if (rados_conf_read_file(ct->cluster, NULL) < 0) {
		condlog(0, "rbd%d: Could not read rados conf", ct->rbd_bus_id);
		goto shutdown_rados;
	}

	ret = rados_connect(ct->cluster);
	if (ret < 0) {
		condlog(0, "rbd%d: Could not connect to rados cluster",
			ct->rbd_bus_id);
		goto shutdown_rados;
	}

	udev_device_unref(bus_dev);
	udev_unref(udev);

	condlog(3, "rbd%d checker init %s %s/%s@%s %s", ct->rbd_bus_id,
		ct->client_addr, ct->pool, ct->image, ct->snap ? ct->snap : "-",
		ct->username ? ct->username : "none");
	return 0;

shutdown_rados:
	rados_shutdown(ct->cluster);
free_snap:
	if (ct->snap)
		free(ct->snap);
free_pool:
	free(ct->pool);
free_image:
	free(ct->image);
free_username:
	if (ct->username)
		free(ct->username);
free_info:
	free(ct->config_info);
free_addr:
	free(ct->client_addr);
free_dev:
	udev_device_unref(bus_dev);
free_udev:
	udev_unref(udev);
free_ct:
	free(ct);
	return 1;
}

static void cleanup_context(struct rbd_checker_context *ct)
{
	pthread_mutex_destroy(&ct->lock);
	pthread_cond_destroy(&ct->active);
	pthread_spin_destroy(&ct->hldr_lock);

	rados_shutdown(ct->cluster);

	if (ct->username)
		free(ct->username);
	if (ct->snap)
		free(ct->snap);
	free(ct->pool);
	free(ct->image);
	free(ct->config_info);
	free(ct->client_addr);
	free(ct);
}

void libcheck_free(struct checker * c)
{
	if (c->context) {
		struct rbd_checker_context *ct = c->context;
		int holders;
		pthread_t thread;

		pthread_spin_lock(&ct->hldr_lock);
		ct->holders--;
		holders = ct->holders;
		thread = ct->thread;
		pthread_spin_unlock(&ct->hldr_lock);
		if (holders)
			pthread_cancel(thread);
		else
			cleanup_context(ct);
		c->context = NULL;
	}
}

static int rbd_is_blacklisted(struct rbd_checker_context *ct, char *msg)
{
	char *addr_tok, *start, *save;
	char *cmd[2];
	char *blklist, *stat;
	size_t blklist_len, stat_len;
	int ret;
	char *end;

	cmd[0] = "{\"prefix\": \"osd blacklist ls\"}";
	cmd[1] = NULL;

	ret = rados_mon_command(ct->cluster, (const char **)cmd, 1, "", 0,
				&blklist, &blklist_len, &stat, &stat_len);
	if (ret < 0) {
		RBD_MSG(msg, "checker failed: mon command failed %d", ret);
		return ret;
	}

	if (!blklist || !blklist_len)
		goto free_bufs;

	/*
	 * parse list of addrs with the format
	 * ipv4:port/nonce date time\n
	 * or
	 * [ipv6]:port/nonce date time\n
	 */
	ret = 0;
	for (start = blklist; ; start = NULL) {
		addr_tok = strtok_r(start, "\n", &save);
		if (!addr_tok || !strlen(addr_tok))
			break;

		end = strchr(addr_tok, ' ');
		if (!end) {
			RBD_MSG(msg, "checker failed: invalid blacklist %s",
				 addr_tok);
			break;
		}
		*end = '\0';

		if (!strcmp(addr_tok, ct->client_addr)) {
			ct->blacklisted = 1;
			RBD_MSG(msg, "%s is blacklisted", ct->client_addr);
			ret = 1;
			break;
		}
	}

free_bufs:
	rados_buffer_free(blklist);
	rados_buffer_free(stat);
	return ret;
}

static int rbd_check(struct rbd_checker_context *ct, char *msg)
{
	if (ct->blacklisted || rbd_is_blacklisted(ct, msg) == 1)
		return PATH_DOWN;

	RBD_MSG(msg, "checker reports path is up");
	/*
	 * Path may have issues, but the ceph cluster is at least
	 * accepting IO, so we can attempt to do IO.
	 *
	 * TODO: in future versions, we can run other tests to
	 * verify OSDs and networks.
	 */
	return PATH_UP;
}

static int safe_write(int fd, const void *buf, size_t count)
{
	while (count > 0) {
		ssize_t r = write(fd, buf, count);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		count -= r;
		buf = (char *)buf + r;
	}
	return 0;
}

static int sysfs_write_rbd_bus(const char *which, const char *buf,
			       size_t buf_len)
{
	char sysfs_path[PATH_SIZE];
	int fd;
	int r;

	/* we require newer kernels so single_major should always be there */
	snprintf(sysfs_path, sizeof(sysfs_path),
		 "/sys/bus/rbd/%s_single_major", which);
	fd = open(sysfs_path, O_WRONLY);
	if (fd < 0)
		return -errno;

	r = safe_write(fd, buf, buf_len);
	close(fd);
	return r;
}

static int rbd_remap(struct rbd_checker_context *ct)
{
	char *argv[11];
	pid_t pid;
	int ret = 0, i = 0;
	int status;

	pid = fork();
	switch (pid) {
	case 0:
		argv[i++] = "rbd";
		argv[i++] = "map";
		argv[i++] = "-o noshare";
		if (ct->username) {
			argv[i++] = "--id";
			argv[i++] = ct->username;
		}
		argv[i++] = "--pool";
		argv[i++] = ct->pool;
		if (ct->snap) {
			argv[i++] = "--snap";
			argv[i++] = ct->snap;
		}
		argv[i++] = ct->image;
		argv[i] = NULL;

		ret = execvp(argv[0], argv);
		condlog(0, "rbd%d: Error executing rbd: %s", ct->rbd_bus_id,
			strerror(errno));
		exit(-1);
	case -1:
		condlog(0, "rbd%d: fork failed: %s", ct->rbd_bus_id,
			strerror(errno));
		return -1;
	default:
		ret = -1;
		wait(&status);
		if (WIFEXITED(status)) {
			status = WEXITSTATUS(status);
			if (status == 0)
				ret = 0;
			else
				condlog(0, "rbd%d: failed with %d",
					ct->rbd_bus_id, status);
		}
	}

	return ret;
}

static int sysfs_write_rbd_remove(const char *buf, int buf_len)
{
	return sysfs_write_rbd_bus("remove", buf, buf_len);
}

static int rbd_rm_blacklist(struct rbd_checker_context *ct)
{
	char *cmd[2];
	char *stat, *cmd_str;
	size_t stat_len;
	int ret;

	ret = asprintf(&cmd_str, "{\"prefix\": \"osd blacklist\", \"blacklistop\": \"rm\", \"addr\": \"%s\"}",
		       ct->client_addr);
	if (ret == -1)
		return -ENOMEM;

	cmd[0] = cmd_str;
	cmd[1] = NULL;

	ret = rados_mon_command(ct->cluster, (const char **)cmd, 1, "", 0,
				NULL, 0, &stat, &stat_len);
	if (ret < 0) {
		condlog(1, "rbd%d: repair failed to remove blacklist for %s %d",
			ct->rbd_bus_id, ct->client_addr, ret);
		goto free_cmd;
	}

	condlog(1, "rbd%d: repair rm blacklist for %s",
	       ct->rbd_bus_id, ct->client_addr);
	free(stat);
free_cmd:
	free(cmd_str);
	return ret;
}

static int rbd_repair(struct rbd_checker_context *ct, char *msg)
{
	char del[17];
	int ret;

	if (!ct->blacklisted)
		return PATH_UP;

	if (!ct->remapped) {
		ret = rbd_remap(ct);
		if (ret) {
			RBD_MSG(msg, "repair failed to remap. Err %d", ret);
			return PATH_DOWN;
		}
	}
	ct->remapped = 1;

	snprintf(del, sizeof(del), "%d force", ct->rbd_bus_id);
	ret = sysfs_write_rbd_remove(del, strlen(del) + 1);
	if (ret) {
		RBD_MSG(msg, "repair failed to clean up. Err %d", ret);
		return PATH_DOWN;
	}

	ret = rbd_rm_blacklist(ct);
	if (ret) {
		RBD_MSG(msg, "repair could not remove blacklist entry. Err %d",
			ret);
		return PATH_DOWN;
	}

	ct->remapped = 0;
	ct->blacklisted = 0;

	RBD_MSG(msg, "has been repaired");
	return PATH_UP;
}

#define rbd_thread_cleanup_push(ct) pthread_cleanup_push(cleanup_func, ct)
#define rbd_thread_cleanup_pop(ct) pthread_cleanup_pop(1)

static void cleanup_func(void *data)
{
	int holders;
	struct rbd_checker_context *ct = data;
	pthread_spin_lock(&ct->hldr_lock);
	ct->holders--;
	holders = ct->holders;
	ct->thread = 0;
	pthread_spin_unlock(&ct->hldr_lock);
	if (!holders)
		cleanup_context(ct);
}

static void *rbd_thread(void *ctx)
{
	struct rbd_checker_context *ct = ctx;
	int state;

	condlog(3, "rbd%d: thread starting up", ct->rbd_bus_id);

	ct->message[0] = '\0';
	/* This thread can be canceled, so setup clean up */
	rbd_thread_cleanup_push(ct)

	/* checker start up */
	pthread_mutex_lock(&ct->lock);
	ct->state = PATH_PENDING;
	pthread_mutex_unlock(&ct->lock);

	state = ct->fn(ct, ct->message);

	/* checker done */
	pthread_mutex_lock(&ct->lock);
	ct->state = state;
	pthread_cond_signal(&ct->active);
	pthread_mutex_unlock(&ct->lock);

	condlog(3, "rbd%d: thead finished, state %s", ct->rbd_bus_id,
		checker_state_name(state));
	rbd_thread_cleanup_pop(ct);
	return ((void *)0);
}

static void rbd_timeout(struct timespec *tsp)
{
	clock_gettime(CLOCK_MONOTONIC, tsp);
	tsp->tv_nsec += 1000 * 1000; /* 1 millisecond */
	normalize_timespec(tsp);
}

static int rbd_exec_fn(struct checker *c, thread_fn *fn)
{
	struct rbd_checker_context *ct = c->context;
	struct timespec tsp;
	pthread_attr_t attr;
	int rbd_status, r;

	if (c->sync)
		return fn(ct, c->message);
	/*
	 * Async mode
	 */
	r = pthread_mutex_lock(&ct->lock);
	if (r != 0) {
		condlog(2, "rbd%d: mutex lock failed with %d", ct->rbd_bus_id,
			r);
		MSG(c, "rbd%d: thread failed to initialize", ct->rbd_bus_id);
		return PATH_WILD;
	}

	if (ct->running) {
		/* Check if checker is still running */
		if (ct->thread) {
			condlog(3, "rbd%d: thread not finished",
				ct->rbd_bus_id);
			rbd_status = PATH_PENDING;
		} else {
			/* checker done */
			ct->running = 0;
			rbd_status = ct->state;
			strncpy(c->message, ct->message, CHECKER_MSG_LEN);
			c->message[CHECKER_MSG_LEN - 1] = '\0';
		}
		pthread_mutex_unlock(&ct->lock);
	} else {
		/* Start new checker */
		ct->state = PATH_UNCHECKED;
		ct->fn = fn;
		pthread_spin_lock(&ct->hldr_lock);
		ct->holders++;
		pthread_spin_unlock(&ct->hldr_lock);
		setup_thread_attr(&attr, 32 * 1024, 1);
		r = pthread_create(&ct->thread, &attr, rbd_thread, ct);
		if (r) {
			pthread_mutex_unlock(&ct->lock);
			ct->thread = 0;
			ct->holders--;
			condlog(3, "rbd%d failed to start rbd thread, using sync mode",
				ct->rbd_bus_id);
			return fn(ct, c->message);
		}
		pthread_attr_destroy(&attr);
		rbd_timeout(&tsp);
		r = pthread_cond_timedwait(&ct->active, &ct->lock, &tsp);
		rbd_status = ct->state;
		strncpy(c->message, ct->message,CHECKER_MSG_LEN);
		c->message[CHECKER_MSG_LEN -1] = '\0';
		pthread_mutex_unlock(&ct->lock);

		if (ct->thread &&
		    (rbd_status == PATH_PENDING || rbd_status == PATH_UNCHECKED)) {
			condlog(3, "rbd%d: thread still running",
				ct->rbd_bus_id);
			ct->running = 1;
			rbd_status = PATH_PENDING;
		}
	}

	return rbd_status;
}

void libcheck_repair(struct checker * c)
{
	struct rbd_checker_context *ct = c->context;

	if (!ct || !ct->blacklisted)
		return;
	rbd_exec_fn(c, rbd_repair);
}

int libcheck_check(struct checker * c)
{
	struct rbd_checker_context *ct = c->context;

	if (!ct)
		return PATH_UNCHECKED;

	if (ct->blacklisted)
		return PATH_DOWN;

	return rbd_exec_fn(c, rbd_check);
}
