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
#include <sys/eventfd.h>
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
#include "alias.h"

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
	POLLFD_IDLE,
	POLLFDS_BASE,
};

#define POLLFD_CHUNK (4096 / sizeof(struct pollfd))
/* Minimum number of pollfds to reserve for clients */
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
static struct pollfd *polls;
static int notify_fd = -1;
static int idle_fd = -1;
static bool clients_need_lock = false;

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
	list_add_tail(&c->node, &clients);
}

/*
 * kill off a dead client
 */
static void dead_client(struct client *c)
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

	list_for_each_entry_safe(client_loop, client_tmp, &clients, node) {
		dead_client(client_loop);
	}

	cli_exit();
	free_polls();
}

void wakeup_cleanup(void *arg)
{
	struct mutex_lock *lck = arg;
	int fd = idle_fd;

	idle_fd = -1;
	set_wakeup_fn(lck, NULL);
	if (fd != -1)
		close(fd);
}

struct watch_descriptors {
	int conf_wd;
	int dir_wd;
	int mp_wd; /* /etc/multipath; for bindings file */
};

/* failing to set the watch descriptor is o.k. we just miss a warning
 * message */
static void reset_watch(int notify_fd, struct watch_descriptors *wds,
			unsigned int *sequence_nr)
{
	struct config *conf;
	int dir_reset = 0;
	int conf_reset = 0;
	int mp_reset = 0;

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
		if (wds->dir_wd == -1)
			dir_reset = 1;
		if (wds->mp_wd == -1)
			mp_reset = 1;
	}
	put_multipath_config(conf);

	if (dir_reset) {
		if (wds->dir_wd != -1) {
			inotify_rm_watch(notify_fd, wds->dir_wd);
			wds->dir_wd = -1;
		}
		wds->dir_wd = inotify_add_watch(notify_fd,
						CONFIG_DIR,
						IN_CLOSE_WRITE |
						IN_DELETE | IN_ONLYDIR);
		if (wds->dir_wd == -1)
			condlog(3, "didn't set up notifications on %s: %m", CONFIG_DIR);
	}
	if (conf_reset) {
		wds->conf_wd = inotify_add_watch(notify_fd, DEFAULT_CONFIGFILE,
						 IN_CLOSE_WRITE);
		if (wds->conf_wd == -1)
			condlog(3, "didn't set up notifications on /etc/multipath.conf: %m");
	}
	if (mp_reset) {
		wds->mp_wd = inotify_add_watch(notify_fd, STATE_DIR,
					       IN_MOVED_TO|IN_ONLYDIR);
		if (wds->mp_wd == -1)
				condlog(3, "didn't set up notifications on %s: %m",
					STATE_DIR);
	}
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
				if (wds->mp_wd != -1)
					inotify_rm_watch(fd, wds->mp_wd);
				wds->conf_wd = wds->dir_wd = wds->mp_wd = -1;
			}
			break;
		}

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
				else if (wds->mp_wd == event->wd)
					wds->mp_wd = -1;
			}
			if (wds->mp_wd != -1 && wds->mp_wd == event->wd)
				handle_bindings_file_inotify(event);
			else
				got_notify = 1;
		}
	}
	if (got_notify)
		condlog(1, "Multipath configuration updated.\nReload multipathd for changes to take effect");
}

static const struct timespec ts_zero = { .tv_sec = 0, };
static const struct timespec ts_max = { .tv_sec = LONG_MAX, .tv_nsec = 999999999 };

static struct timespec *get_soonest_timeout(struct timespec *ts)
{
	struct timespec ts_min = ts_max, now;
	bool any = false;
	struct client *c;

	list_for_each_entry(c, &clients, node) {
		if (timespeccmp(&c->expires, &ts_zero) != 0 &&
		    timespeccmp(&c->expires, &ts_min) < 0) {
			ts_min = c->expires;
			any = true;
		}
	}

	if (!any)
		return NULL;

	get_monotonic_time(&now);
	timespecsub(&ts_min, &now, ts);
	if (timespeccmp(ts, &ts_zero) < 0)
		*ts = ts_zero;

	condlog(4, "%s: next client expires in %ld.%03lds", __func__,
		(long)ts->tv_sec, ts->tv_nsec / 1000000);
	return ts;
}

bool waiting_clients(void)
{
	return clients_need_lock;
}

static void check_for_locked_work(struct client *skip)
{
	struct client *c;

	list_for_each_entry(c, &clients, node) {
		if (c != skip && c->state == CLT_LOCKED_WORK) {
			clients_need_lock = true;
			return;
		}
	}
	clients_need_lock = false;
}

static int parse_cmd(struct client *c)
{
	int r;

	r = get_cmdvec(c->cmd, &c->cmdvec, false);

	if (r)
		return -r;

	c->handler = find_handler_for_cmdvec(c->cmdvec);

	if (!c->handler || !c->handler->fn)
		return -EINVAL;

	return r;
}

static int execute_handler(struct client *c, struct vectors *vecs)
{

	if (!c->handler || !c->handler->fn)
		return -EINVAL;

	return c->handler->fn(c->cmdvec, &c->reply, vecs);
}

static void wakeup_listener(void)
{
	uint64_t one = 1;

	if (idle_fd != -1 &&
	    write(idle_fd, &one, sizeof(one)) != sizeof(one))
		condlog(1, "%s: failed", __func__);
}

static void drain_idle_fd(int fd)
{
	uint64_t val;
	int rc;

	rc = read(fd, &val, sizeof(val));
	condlog(4, "%s: %d, %"PRIu64, __func__, rc, val);
}

void default_reply(struct client *c, int r)
{
	if (r == 0) {
		append_strbuf_str(&c->reply, "ok\n");
		return;
	}
	append_strbuf_str(&c->reply, "fail\n");
	switch(r) {
	case -EINVAL:
	case -ESRCH:
	case -ENOMEM:
		/* return codes from get_cmdvec() */
		genhelp_handler(c->cmd, -r, &c->reply);
		break;
	case -EPERM:
		append_strbuf_str(&c->reply,
				  "permission deny: need to be root\n");
		break;
	case -ETIMEDOUT:
		append_strbuf_str(&c->reply, "timeout\n");
		break;
	}
}

static void set_client_state(struct client *c, int state)
{
	switch(state)
	{
	case CLT_RECV:
		reset_strbuf(&c->reply);
		memset(c->cmd, '\0', sizeof(c->cmd));
		c->error = 0;
		/* fallthrough */
	case CLT_SEND:
		/* no timeout while waiting for the client or sending a reply */
		c->expires = ts_zero;
		/* reuse these fields for next data transfer */
		c->len = c->cmd_len = 0;
		/* cmdvec isn't needed any more */
		if (c->cmdvec) {
			free_keys(c->cmdvec);
			c->cmdvec = NULL;
		}
		break;
	default:
		break;
	}
	c->state = state;
}

enum {
	STM_CONT,
	STM_BREAK,
};

static int client_state_machine(struct client *c, struct vectors *vecs,
				short revents)
{
	ssize_t n;

	condlog(4, "%s: cli[%d] poll=%x state=%d cmd=\"%s\" repl \"%s\"", __func__,
		c->fd, revents, c->state, c->cmd, get_strbuf_str(&c->reply));

	switch (c->state) {
	case CLT_RECV:
		if (!(revents & POLLIN))
			return STM_BREAK;
		if (c->cmd_len == 0) {
			size_t len;
			/*
			 * We got POLLIN; assume that at least the length can
			 * be read immediately.
			 */
			get_monotonic_time(&c->expires);
			c->expires.tv_sec += uxsock_timeout / 1000;
			c->expires.tv_nsec += (uxsock_timeout % 1000) * 1000000;
			normalize_timespec(&c->expires);
			n = recv(c->fd, &len, sizeof(len), 0);
			if (n < (ssize_t)sizeof(len)) {
				condlog(1, "%s: cli[%d]: failed to receive reply len: %zd",
					__func__, c->fd, n);
				c->error = -ECONNRESET;
			} else if (len <= 0 || len > _MAX_CMD_LEN) {
				condlog(1, "%s: cli[%d]: invalid command length (%zu bytes)",
					__func__, c->fd, len);
				c->error = -ECONNRESET;
			} else {
				c->cmd_len = len;
				condlog(4, "%s: cli[%d]: connected", __func__, c->fd);
			}
			/* poll for data */
			return STM_BREAK;
		} else if (c->len < c->cmd_len) {
			n = recv(c->fd, c->cmd + c->len, c->cmd_len - c->len, 0);
			if (n <= 0 && errno != EINTR && errno != EAGAIN) {
				condlog(1, "%s: cli[%d]: error in recv: %m",
					__func__, c->fd);
				c->error = -ECONNRESET;
				return STM_BREAK;
			}
			c->len += n;
			if (c->len < c->cmd_len)
				/* continue polling */
				return STM_BREAK;
		}
		condlog(4, "cli[%d]: Got request [%s]", c->fd, c->cmd);
		set_client_state(c, CLT_PARSE);
		return STM_CONT;

	case CLT_PARSE:
		c->error = parse_cmd(c);
		if (!c->error) {
			/* Permission check */
			struct key *kw = VECTOR_SLOT(c->cmdvec, 0);

			if (!c->is_root && kw->code != VRB_LIST) {
				c->error = -EPERM;
				condlog(0, "%s: cli[%d]: unauthorized cmd \"%s\"",
					__func__, c->fd, c->cmd);
			}
		}
		if (c->error)
			set_client_state(c, CLT_SEND);
		else if (c->handler->locked)
			set_client_state(c, CLT_LOCKED_WORK);
		else
			set_client_state(c, CLT_WORK);
		return STM_CONT;

	case CLT_LOCKED_WORK:
		if (trylock(&vecs->lock) == 0) {
			/* don't use cleanup_lock(), lest we wakeup ourselves */
			pthread_cleanup_push_cast(__unlock, &vecs->lock);
			c->error = execute_handler(c, vecs);
			check_for_locked_work(c);
			pthread_cleanup_pop(1);
			condlog(4, "%s: cli[%d] grabbed lock", __func__, c->fd);
			set_client_state(c, CLT_SEND);
			/* Wait for POLLOUT */
			return STM_BREAK;
		} else {
			condlog(4, "%s: cli[%d] waiting for lock", __func__, c->fd);
			return STM_BREAK;
		}

	case CLT_WORK:
		c->error = execute_handler(c, vecs);
		set_client_state(c, CLT_SEND);
		/* Wait for POLLOUT */
		return STM_BREAK;

	case CLT_SEND:
		if (get_strbuf_len(&c->reply) == 0)
			default_reply(c, c->error);

		if (c->cmd_len == 0) {
			size_t len = get_strbuf_len(&c->reply) + 1;

			if (send(c->fd, &len, sizeof(len), MSG_NOSIGNAL)
			    != sizeof(len))
				c->error = -ECONNRESET;
			c->cmd_len = len;
			return STM_BREAK;
		}

		if (c->len < c->cmd_len) {
			const char *buf = get_strbuf_str(&c->reply);

			n = send(c->fd, buf + c->len, c->cmd_len - c->len, MSG_NOSIGNAL);
			if (n == -1) {
				if (!(errno == EAGAIN || errno == EINTR))
					c->error = -ECONNRESET;
			} else
				c->len += n;
		}

		if (c->len >= c->cmd_len) {
			condlog(4, "cli[%d]: Reply [%zu bytes]", c->fd, c->cmd_len);
			set_client_state(c, CLT_RECV);
		}
		return STM_BREAK;

	default:
		return STM_BREAK;
	}
}

static void check_timeout(struct client *c)
{
	struct timespec now;

	if (timespeccmp(&c->expires, &ts_zero) == 0)
		return;

	get_monotonic_time(&now);
	if (timespeccmp(&c->expires, &now) > 0)
		return;

	condlog(2, "%s: cli[%d]: timed out at %ld.%03ld", __func__,
		c->fd, (long)c->expires.tv_sec, c->expires.tv_nsec / 1000000);

	c->error = -ETIMEDOUT;
	set_client_state(c, CLT_SEND);
}

static void handle_client(struct client *c, struct vectors *vecs, short revents)
{
	if (revents & (POLLHUP|POLLERR)) {
		c->error = -ECONNRESET;
		return;
	}

	check_timeout(c);
	while (client_state_machine(c, vecs, revents) == STM_CONT);
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
	struct watch_descriptors wds = { .conf_wd = -1, .dir_wd = -1, .mp_wd = -1, };
	struct vectors *vecs = trigger_data;

	condlog(3, "uxsock: startup listener");
	polls = calloc(1, max_pfds * sizeof(*polls));
	if (!polls) {
		condlog(0, "uxsock: failed to allocate poll fds");
		exit_daemon();
		return NULL;
	}
	notify_fd = inotify_init1(IN_NONBLOCK);
	if (notify_fd == -1) /* it's fine if notifications fail */
		condlog(3, "failed to start up configuration notifications");

	pthread_cleanup_push(wakeup_cleanup, &vecs->lock);
	idle_fd = eventfd(0, EFD_NONBLOCK|EFD_CLOEXEC);
	if (idle_fd == -1) {
		condlog(1, "failed to create idle fd");
		exit_daemon();
	} else
		set_wakeup_fn(&vecs->lock, wakeup_listener);

	sigfillset(&mask);
	sigdelset(&mask, SIGINT);
	sigdelset(&mask, SIGTERM);
	sigdelset(&mask, SIGHUP);
	sigdelset(&mask, SIGUSR1);
	while (1) {
		struct client *c, *tmp;
		int i, n_pfds, poll_count, num_clients;
		struct timespec __timeout, *timeout;

		/* setup for a poll */
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
		if (notify_fd == -1 || (wds.conf_wd == -1 && wds.dir_wd == -1
					&& wds.mp_wd == -1))
			polls[POLLFD_NOTIFY].events = 0;
		else
			polls[POLLFD_NOTIFY].events = POLLIN;

		polls[POLLFD_IDLE].fd = idle_fd;
		check_for_locked_work(NULL);
		if (clients_need_lock)
			polls[POLLFD_IDLE].events = POLLIN;
		else
			polls[POLLFD_IDLE].events = 0;

		/* setup the clients */
		i = POLLFDS_BASE;
		list_for_each_entry(c, &clients, node) {
			switch(c->state) {
			case CLT_RECV:
				polls[i].events = POLLIN;
				break;
			case CLT_SEND:
				polls[i].events = POLLOUT;
				break;
			default:
				/* don't poll for this client */
				continue;
			}
			polls[i].fd = c->fd;
			i++;
			if (i >= max_pfds)
				break;
		}
		n_pfds = i;
		timeout = get_soonest_timeout(&__timeout);

		/* most of our life is spent in this call */
		poll_count = ppoll(polls, n_pfds, timeout, &mask);

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

		if (polls[POLLFD_IDLE].fd != -1 &&
		    polls[POLLFD_IDLE].revents & POLLIN)
			drain_idle_fd(idle_fd);

		/* see if a client needs handling */
		list_for_each_entry_safe(c, tmp, &clients, node) {
			short revents = 0;

			for (i = POLLFDS_BASE; i < n_pfds; i++) {
				if (polls[i].fd == c->fd) {
					revents = polls[i].revents;
					break;
				}
			}

			handle_client(c, trigger_data, revents);

			if (c->error == -ECONNRESET) {
				condlog(4, "cli[%d]: disconnected", c->fd);
				dead_client(c);
				if (i < n_pfds)
					polls[i].fd = -1;
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

	pthread_cleanup_pop(1);
	return NULL;
}
