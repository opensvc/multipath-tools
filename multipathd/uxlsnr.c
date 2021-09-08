/*
 * Original author : tridge@samba.org, January 2002
 *
 * Copyright (c) 2005 Christophe Varoqui
 * Copyright (c) 2005 Benjamin Marzinski, Redhat
 */

/*
 * A simple domain socket listener
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <sys/time.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/inotify.h>
#include "checkers.h"
#include "debug.h"
#include "vector.h"
#include "structs.h"
#include "structs_vec.h"
#include "uxsock.h"
#include "defaults.h"
#include "config.h"
#include "mpath_cmd.h"
#include "time-util.h"
#include "util.h"

#include "main.h"
#include "cli.h"
#include "uxlsnr.h"
#include "strbuf.h"

/* state of client connection */
enum {
	CLT_RECV,
	CLT_PARSE,
	CLT_LOCKED_WORK,
	CLT_WORK,
	CLT_SEND,
};

struct client {
	struct list_head node;
	struct timespec expires;
	int state;
	int fd;
	vector cmdvec;
	/* NUL byte at end */
	char cmd[_MAX_CMD_LEN + 1];
	struct strbuf reply;
	struct handler *handler;
	size_t cmd_len, len;
	int error;
	bool is_root;
};

/* Indices for array of poll fds */
enum {
	POLLFD_UX = 0,
	POLLFD_NOTIFY,
	POLLFDS_BASE,
};

#define POLLFD_CHUNK (4096 / sizeof(struct pollfd))
/* Minimum mumber of pollfds to reserve for clients */
#define MIN_POLLS (POLLFD_CHUNK - POLLFDS_BASE)
/*
 * Max number of client connections allowed
 * During coldplug, there may be a large number of "multipath -u"
 * processes connecting.
 */
#define MAX_CLIENTS (16384 - POLLFDS_BASE)

/* Compile-time error if POLLFD_CHUNK is too small */
static __attribute__((unused)) char ___a[-(MIN_POLLS <= 0)];

static LIST_HEAD(clients);
static pthread_mutex_t client_lock = PTHREAD_MUTEX_INITIALIZER;
static struct pollfd *polls;
static int notify_fd = -1;
static char *watch_config_dir;

static bool _socket_client_is_root(int fd)
{
	socklen_t len = 0;
	struct ucred uc;

	len = sizeof(struct ucred);
	if ((fd >= 0) &&
	    (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &uc, &len) == 0) &&
	    (uc.uid == 0))
			return true;

	/* Treat error as not root client */
	return false;
}

/*
 * handle a new client joining
 */
static void new_client(int ux_sock)
{
	struct client *c;
	struct sockaddr addr;
	socklen_t len = sizeof(addr);
	int fd;

	fd = accept(ux_sock, &addr, &len);

	if (fd == -1)
		return;

	c = (struct client *)calloc(1, sizeof(*c));
	if (!c) {
		close(fd);
		return;
	}
	INIT_LIST_HEAD(&c->node);
	c->fd = fd;
	c->state = CLT_RECV;
	c->is_root = _socket_client_is_root(c->fd);

	/* put it in our linked list */
	pthread_mutex_lock(&client_lock);
	list_add_tail(&c->node, &clients);
	pthread_mutex_unlock(&client_lock);
}

/*
 * kill off a dead client
 */
static void _dead_client(struct client *c)
{
	int fd = c->fd;
	list_del_init(&c->node);
	c->fd = -1;
	reset_strbuf(&c->reply);
	if (c->cmdvec)
		free_keys(c->cmdvec);
	free(c);
	close(fd);
}

static void dead_client(struct client *c)
{
	pthread_cleanup_push(cleanup_mutex, &client_lock);
	pthread_mutex_lock(&client_lock);
	_dead_client(c);
	pthread_cleanup_pop(1);
}

static void free_polls (void)
{
	if (polls)
		free(polls);
	polls = NULL;
}

void uxsock_cleanup(void *arg)
{
	struct client *client_loop;
	struct client *client_tmp;
	long ux_sock = (long)arg;

	close(ux_sock);
	close(notify_fd);
	free(watch_config_dir);

	pthread_mutex_lock(&client_lock);
	list_for_each_entry_safe(client_loop, client_tmp, &clients, node) {
		_dead_client(client_loop);
	}
	pthread_mutex_unlock(&client_lock);

	cli_exit();
	free_polls();
}

struct watch_descriptors {
	int conf_wd;
	int dir_wd;
};

/* failing to set the watch descriptor is o.k. we just miss a warning
 * message */
static void reset_watch(int notify_fd, struct watch_descriptors *wds,
			unsigned int *sequence_nr)
{
	struct config *conf;
	int dir_reset = 0;
	int conf_reset = 0;

	if (notify_fd == -1)
		return;

	conf = get_multipath_config();
	/* instead of repeatedly try to reset the inotify watch if
	 * the config directory or multipath.conf isn't there, just
	 * do it once per reconfigure */
	if (*sequence_nr != conf->sequence_nr) {
		*sequence_nr = conf->sequence_nr;
		if (wds->conf_wd == -1)
			conf_reset = 1;
		if (!watch_config_dir || !conf->config_dir ||
		    strcmp(watch_config_dir, conf->config_dir)) {
			dir_reset = 1;
			if (watch_config_dir)
				free(watch_config_dir);
			if (conf->config_dir)
				watch_config_dir = strdup(conf->config_dir);
			else
				watch_config_dir = NULL;
		} else if (wds->dir_wd == -1)
			dir_reset = 1;
	}
	put_multipath_config(conf);

	if (dir_reset) {
		if (wds->dir_wd != -1) {
			inotify_rm_watch(notify_fd, wds->dir_wd);
			wds->dir_wd = -1;
		}
		if (watch_config_dir) {
			wds->dir_wd = inotify_add_watch(notify_fd,
							watch_config_dir,
							IN_CLOSE_WRITE |
							IN_DELETE | IN_ONLYDIR);
			if (wds->dir_wd == -1)
				condlog(3, "didn't set up notifications on %s: %m", watch_config_dir);
		}
	}
	if (conf_reset) {
		wds->conf_wd = inotify_add_watch(notify_fd, DEFAULT_CONFIGFILE,
						 IN_CLOSE_WRITE);
		if (wds->conf_wd == -1)
			condlog(3, "didn't set up notifications on /etc/multipath.conf: %m");
	}
	return;
}

static void handle_inotify(int fd, struct watch_descriptors *wds)
{
	char buff[1024]
		__attribute__ ((aligned(__alignof__(struct inotify_event))));
	const struct inotify_event *event;
	ssize_t len;
	char *ptr;
	int got_notify = 0;

	for (;;) {
		len = read(fd, buff, sizeof(buff));
		if (len <= 0) {
			if (len < 0 && errno != EAGAIN) {
				condlog(3, "error reading from inotify_fd");
				if (wds->conf_wd != -1)
					inotify_rm_watch(fd, wds->conf_wd);
				if (wds->dir_wd != -1)
					inotify_rm_watch(fd, wds->dir_wd);
				wds->conf_wd = wds->dir_wd = -1;
			}
			break;
		}

		got_notify = 1;
		for (ptr = buff; ptr < buff + len;
		     ptr += sizeof(struct inotify_event) + event->len) {
			event = (const struct inotify_event *) ptr;

			if (event->mask & IN_IGNORED) {
				/* multipathd.conf may have been overwritten.
				 * Try once to reset the notification */
				if (wds->conf_wd == event->wd)
					wds->conf_wd = inotify_add_watch(notify_fd, DEFAULT_CONFIGFILE, IN_CLOSE_WRITE);
				else if (wds->dir_wd == event->wd)
					wds->dir_wd = -1;
			}
		}
	}
	if (got_notify)
		condlog(1, "Multipath configuration updated.\nReload multipathd for changes to take effect");
}

static const struct timespec ts_zero = { .tv_sec = 0, };

static int parse_cmd(struct client *c)
{
	int r;

	r = get_cmdvec(c->cmd, &c->cmdvec);

	if (r) {
		genhelp_handler(c->cmd, r, &c->reply);
		if (get_strbuf_len(&c->reply) == 0)
			return EINVAL;
		return 0;
	}

	c->handler = find_handler_for_cmdvec(c->cmdvec);

	if (!c->handler || !c->handler->fn) {
		genhelp_handler(c->cmd, EINVAL, &c->reply);
		if (get_strbuf_len(&c->reply) == 0)
			r = EINVAL;
		else
			r = 0;
	}

	return r;
}

static int execute_handler(struct client *c, struct vectors *vecs, int timeout)
{
	int r;
	struct timespec tmo;

	if (!c->handler)
		return EINVAL;

	if (clock_gettime(CLOCK_REALTIME, &tmo) == 0) {
		tmo.tv_sec += timeout;
	} else {
		tmo.tv_sec = 0;
	}

	if (c->handler->locked) {
		int locked = 0;

		pthread_cleanup_push(cleanup_lock, &vecs->lock);
		if (tmo.tv_sec) {
			r = timedlock(&vecs->lock, &tmo);
		} else {
			lock(&vecs->lock);
			r = 0;
		}
		if (r == 0) {
			locked = 1;
			pthread_testcancel();
			r = c->handler->fn(c->cmdvec, &c->reply, vecs);
		}
		pthread_cleanup_pop(locked);
	} else
		r = c->handler->fn(c->cmdvec, &c->reply, vecs);

	return r;
}

static int uxsock_trigger(struct client *c, void *trigger_data)
{
	struct vectors * vecs;
	int r = 1;

	vecs = (struct vectors *)trigger_data;

	r = parse_cmd(c);

	if (r == 0 && c->cmdvec && VECTOR_SIZE(c->cmdvec) > 0) {
		struct key *kw = VECTOR_SLOT(c->cmdvec, 0);

		if (!c->is_root && kw->code != LIST)
			r = EPERM;
	}

	if (r == 0 && c->handler)
		r = execute_handler(c, vecs, uxsock_timeout / 1000);

	if (c->cmdvec) {
		free_keys(c->cmdvec);
		c->cmdvec = NULL;
	}

	if (r > 0) {
		switch(r) {
		case ETIMEDOUT:
			append_strbuf_str(&c->reply, "timeout\n");
			break;
		case EPERM:
			append_strbuf_str(&c->reply,
					  "permission deny: need to be root\n");
			break;
		default:
			append_strbuf_str(&c->reply, "fail\n");
			break;
		}
	}
	else if (!r && get_strbuf_len(&c->reply) == 0) {
		append_strbuf_str(&c->reply, "ok\n");
		r = 0;
	}
	/* else if (r < 0) leave *reply alone */
	return r;
}

static void set_client_state(struct client *c, int state)
{
	switch(state)
	{
	case CLT_RECV:
		reset_strbuf(&c->reply);
		memset(c->cmd, '\0', sizeof(c->cmd));
		c->expires = ts_zero;
		/* fallthrough */
	case CLT_SEND:
		/* reuse these fields for next data transfer */
		c->len = c->cmd_len = 0;
		break;
	default:
		break;
	}
	c->state = state;
}

static void handle_client(struct client *c, void *trigger_data)
{
	ssize_t n;

	switch (c->state) {
	case CLT_RECV:
		if (c->cmd_len == 0) {
			/*
			 * We got POLLIN; assume that at least the length can
			 * be read immediately.
			 */
			get_monotonic_time(&c->expires);
			c->expires.tv_sec += uxsock_timeout / 1000;
			c->expires.tv_nsec += (uxsock_timeout % 1000) * 1000000;
			normalize_timespec(&c->expires);
			n = mpath_recv_reply_len(c->fd, 0);
			if (n == -1) {
				condlog(1, "%s: cli[%d]: failed to receive reply len",
					__func__, c->fd);
				c->error = -ECONNRESET;
			} else if (n > _MAX_CMD_LEN) {
				condlog(1, "%s: cli[%d]: overlong command (%zd bytes)",
					__func__, c->fd, n);
				c->error = -ECONNRESET;
			} else {
				c->cmd_len = n;
				condlog(4, "%s: cli[%d]: connected", __func__, c->fd);
			}
			/* poll for data */
			return;
		} else if (c->len < c->cmd_len) {
			n = recv(c->fd, c->cmd + c->len, c->cmd_len - c->len, 0);
			if (n <= 0 && errno != EINTR && errno != EAGAIN) {
				condlog(1, "%s: cli[%d]: error in recv: %m",
					__func__, c->fd);
				c->error = -ECONNRESET;
				return;
			}
			c->len += n;
			if (c->len < c->cmd_len)
				/* continue polling */
				return;
			set_client_state(c, CLT_PARSE);
		}
		break;
	default:
		break;
	}

	condlog(4, "cli[%d]: Got request [%s]", c->fd, c->cmd);
	uxsock_trigger(c, trigger_data);

	if (get_strbuf_len(&c->reply) > 0) {
		const char *buf = get_strbuf_str(&c->reply);

		if (send_packet(c->fd, buf) != 0)
			dead_client(c);
		else
			condlog(4, "cli[%d]: Reply [%zu bytes]", c->fd,
				get_strbuf_len(&c->reply) + 1);
		reset_strbuf(&c->reply);
	}

	set_client_state(c, CLT_RECV);
}

/*
 * entry point
 */
void *uxsock_listen(long ux_sock, void *trigger_data)
{
	sigset_t mask;
	int max_pfds = MIN_POLLS + POLLFDS_BASE;
	/* conf->sequence_nr will be 1 when uxsock_listen is first called */
	unsigned int sequence_nr = 0;
	struct watch_descriptors wds = { .conf_wd = -1, .dir_wd = -1 };

	condlog(3, "uxsock: startup listener");
	polls = calloc(1, max_pfds * sizeof(*polls));
	if (!polls) {
		condlog(0, "uxsock: failed to allocate poll fds");
		exit_daemon();
	}
	notify_fd = inotify_init1(IN_NONBLOCK);
	if (notify_fd == -1) /* it's fine if notifications fail */
		condlog(3, "failed to start up configuration notifications");
	sigfillset(&mask);
	sigdelset(&mask, SIGINT);
	sigdelset(&mask, SIGTERM);
	sigdelset(&mask, SIGHUP);
	sigdelset(&mask, SIGUSR1);
	while (1) {
		struct client *c, *tmp;
		int i, n_pfds, poll_count, num_clients;

		/* setup for a poll */
		pthread_mutex_lock(&client_lock);
		pthread_cleanup_push(cleanup_mutex, &client_lock);
		num_clients = 0;
		list_for_each_entry(c, &clients, node) {
			num_clients++;
		}
		if (num_clients + POLLFDS_BASE > max_pfds) {
			struct pollfd *new;
			int n_new = max_pfds + POLLFD_CHUNK;

			new = realloc(polls, n_new * sizeof(*polls));
			if (new) {
				max_pfds = n_new;
				polls = new;
			} else {
				condlog(1, "%s: realloc failure, %d clients not served",
					__func__,
					num_clients + POLLFDS_BASE - max_pfds);
				num_clients = max_pfds - POLLFDS_BASE;
			}
		}
		if (num_clients < MAX_CLIENTS) {
			polls[POLLFD_UX].fd = ux_sock;
			polls[POLLFD_UX].events = POLLIN;
		} else {
			/*
			 * New clients can't connect, num_clients won't grow
			 * to MAX_CLIENTS or higher
			 */
			condlog(1, "%s: max client connections reached, pausing polling",
				__func__);
			polls[POLLFD_UX].fd = -1;
		}

		reset_watch(notify_fd, &wds, &sequence_nr);
		polls[POLLFD_NOTIFY].fd = notify_fd;
		if (notify_fd == -1 || (wds.conf_wd == -1 && wds.dir_wd == -1))
			polls[POLLFD_NOTIFY].events = 0;
		else
			polls[POLLFD_NOTIFY].events = POLLIN;

		/* setup the clients */
		i = POLLFDS_BASE;
		list_for_each_entry(c, &clients, node) {
			polls[i].fd = c->fd;
			polls[i].events = POLLIN;
			i++;
			if (i >= max_pfds)
				break;
		}
		n_pfds = i;
		pthread_cleanup_pop(1);

		/* most of our life is spent in this call */
		poll_count = ppoll(polls, n_pfds, NULL, &mask);

		handle_signals(false);
		if (poll_count == -1) {
			if (errno == EINTR) {
				handle_signals(true);
				continue;
			}

			/* something went badly wrong! */
			condlog(0, "uxsock: poll failed with %d", errno);
			exit_daemon();
			break;
		}

		if (poll_count == 0) {
			handle_signals(true);
			continue;
		}

		/* see if a client wants to speak to us */
		for (i = POLLFDS_BASE; i < n_pfds; i++) {
			if (polls[i].revents & (POLLIN|POLLHUP|POLLERR)) {
				c = NULL;
				pthread_mutex_lock(&client_lock);
				list_for_each_entry(tmp, &clients, node) {
					if (tmp->fd == polls[i].fd) {
						c = tmp;
						break;
					}
				}
				pthread_mutex_unlock(&client_lock);
				if (!c) {
					condlog(4, "cli%d: new fd %d",
						i, polls[i].fd);
					continue;
				}
				if (polls[i].revents & (POLLHUP|POLLERR)) {
					condlog(4, "cli[%d]: Disconnected",
						c->fd);
					dead_client(c);
					continue;
				}
				handle_client(c, trigger_data);
				if (c->error == -ECONNRESET)
					dead_client(c);
			}
		}
		/* see if we got a non-fatal signal */
		handle_signals(true);

		/* see if we got a new client */
		if (polls[POLLFD_UX].revents & POLLIN) {
			new_client(ux_sock);
		}

		/* handle inotify events on config files */
		if (polls[POLLFD_NOTIFY].revents & POLLIN)
			handle_inotify(notify_fd, &wds);
	}

	return NULL;
}
