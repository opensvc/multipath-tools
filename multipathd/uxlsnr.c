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
#include "checkers.h"
#include "memory.h"
#include "debug.h"
#include "vector.h"
#include "structs.h"
#include "structs_vec.h"
#include "uxsock.h"
#include "defaults.h"
#include "config.h"
#include "mpath_cmd.h"
#include "time-util.h"

#include "main.h"
#include "cli.h"
#include "uxlsnr.h"

struct timespec sleep_time = {5, 0};

struct client {
	struct list_head node;
	int fd;
};

#define MIN_POLLS 1023

LIST_HEAD(clients);
pthread_mutex_t client_lock = PTHREAD_MUTEX_INITIALIZER;
struct pollfd *polls;

static bool _socket_client_is_root(int fd);

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

	c = (struct client *)MALLOC(sizeof(*c));
	if (!c) {
		close(fd);
		return;
	}
	memset(c, 0, sizeof(*c));
	INIT_LIST_HEAD(&c->node);
	c->fd = fd;

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
	FREE(c);
	close(fd);
}

static void dead_client(struct client *c)
{
	pthread_cleanup_push(cleanup_lock, &client_lock);
	pthread_mutex_lock(&client_lock);
	_dead_client(c);
	pthread_cleanup_pop(1);
}

void free_polls (void)
{
	if (polls)
		FREE(polls);
}

void check_timeout(struct timespec start_time, char *inbuf,
		   unsigned int timeout)
{
	struct timespec diff_time, end_time;

	if (start_time.tv_sec) {
		unsigned long msecs;

		get_monotonic_time(&end_time);
		timespecsub(&end_time, &start_time, &diff_time);
		msecs = diff_time.tv_sec * 1000 +
			diff_time.tv_nsec / (1000 * 1000);
		if (msecs > timeout)
			condlog(2, "cli cmd '%s' timeout reached "
				"after %lu.%06lu secs", inbuf,
				diff_time.tv_sec, diff_time.tv_nsec / 1000);
	}
}

void uxsock_cleanup(void *arg)
{
	struct client *client_loop;
	struct client *client_tmp;
	long ux_sock = (long)arg;

	close(ux_sock);

	pthread_mutex_lock(&client_lock);
	list_for_each_entry_safe(client_loop, client_tmp, &clients, node) {
		_dead_client(client_loop);
	}
	pthread_mutex_unlock(&client_lock);

	cli_exit();
	free_polls();
}

/*
 * entry point
 */
void * uxsock_listen(uxsock_trigger_fn uxsock_trigger, long ux_sock,
		     void * trigger_data)
{
	int rlen;
	char *inbuf;
	char *reply;
	sigset_t mask;
	int old_clients = MIN_POLLS;

	condlog(3, "uxsock: startup listener");
	polls = (struct pollfd *)MALLOC((MIN_POLLS + 1) * sizeof(struct pollfd));
	if (!polls) {
		condlog(0, "uxsock: failed to allocate poll fds");
		exit_daemon();
	}
	sigfillset(&mask);
	sigdelset(&mask, SIGINT);
	sigdelset(&mask, SIGTERM);
	sigdelset(&mask, SIGHUP);
	sigdelset(&mask, SIGUSR1);
	while (1) {
		struct client *c, *tmp;
		int i, poll_count, num_clients;

		/* setup for a poll */
		pthread_mutex_lock(&client_lock);
		num_clients = 0;
		list_for_each_entry(c, &clients, node) {
			num_clients++;
		}
		if (num_clients != old_clients) {
			struct pollfd *new;
			if (num_clients <= MIN_POLLS && old_clients > MIN_POLLS) {
				new = REALLOC(polls, (1 + MIN_POLLS) *
						sizeof(struct pollfd));
			} else if (num_clients <= MIN_POLLS && old_clients <= MIN_POLLS) {
				new = polls;
			} else {
				new = REALLOC(polls, (1+num_clients) *
						sizeof(struct pollfd));
			}
			if (!new) {
				pthread_mutex_unlock(&client_lock);
				condlog(0, "%s: failed to realloc %d poll fds",
					"uxsock", 1 + num_clients);
				sched_yield();
				continue;
			}
			old_clients = num_clients;
			polls = new;
		}
		polls[0].fd = ux_sock;
		polls[0].events = POLLIN;

		/* setup the clients */
		i = 1;
		list_for_each_entry(c, &clients, node) {
			polls[i].fd = c->fd;
			polls[i].events = POLLIN;
			i++;
		}
		pthread_mutex_unlock(&client_lock);

		/* most of our life is spent in this call */
		poll_count = ppoll(polls, i, &sleep_time, &mask);

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

		/*
		 * Client connection. We shouldn't answer while we're
		 * configuring - nothing may be configured yet.
		 * But we can't wait forever either, because this thread
		 * must handle signals. So wait a short while only.
		 */
		if (wait_for_state_change_if(DAEMON_CONFIGURE, 10)
		    == DAEMON_CONFIGURE) {
			handle_signals(false);
			continue;
		}

		/* see if a client wants to speak to us */
		for (i = 1; i < num_clients + 1; i++) {
			if (polls[i].revents & POLLIN) {
				struct timespec start_time;

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
				get_monotonic_time(&start_time);
				if (recv_packet_from_client(c->fd, &inbuf,
							    uxsock_timeout)
				    != 0) {
					dead_client(c);
					continue;
				}
				if (!inbuf) {
					condlog(4, "recv_packet_from_client "
						"get null request");
					continue;
				}
				condlog(4, "cli[%d]: Got request [%s]",
					i, inbuf);
				uxsock_trigger(inbuf, &reply, &rlen,
					       _socket_client_is_root(c->fd),
					       trigger_data);
				if (reply) {
					if (send_packet(c->fd,
							reply) != 0) {
						dead_client(c);
					} else {
						condlog(4, "cli[%d]: "
							"Reply [%d bytes]",
							i, rlen);
					}
					FREE(reply);
					reply = NULL;
				}
				check_timeout(start_time, inbuf,
					      uxsock_timeout);
				FREE(inbuf);
			}
		}
		/* see if we got a non-fatal signal */
		handle_signals(true);

		/* see if we got a new client */
		if (polls[0].revents & POLLIN) {
			new_client(ux_sock);
		}
	}

	return NULL;
}
