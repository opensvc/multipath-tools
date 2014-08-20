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
#include <sys/poll.h>
#include <sys/time.h>
#include <signal.h>
#include <checkers.h>
#include <memory.h>
#include <debug.h>
#include <vector.h>
#include <structs.h>
#include <structs_vec.h>
#include <uxsock.h>
#include <defaults.h>
#include <config.h>

#include "main.h"
#include "cli.h"
#include "uxlsnr.h"

struct timespec sleep_time = {5, 0};

struct client {
	struct list_head node;
	int fd;
};

LIST_HEAD(clients);
pthread_mutex_t client_lock = PTHREAD_MUTEX_INITIALIZER;
struct pollfd *polls;
volatile sig_atomic_t reconfig_sig = 0;
volatile sig_atomic_t log_reset_sig = 0;

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
static void dead_client(struct client *c)
{
	pthread_mutex_lock(&client_lock);
	list_del_init(&c->node);
	pthread_mutex_unlock(&client_lock);
	close(c->fd);
	c->fd = -1;
	FREE(c);
}

void free_polls (void)
{
	if (polls)
		FREE(polls);
}

void check_timeout(struct timeval start_time, char *inbuf,
		   unsigned int timeout)
{
	struct timeval diff_time, end_time;

	if (start_time.tv_sec && gettimeofday(&end_time, NULL) == 0) {
		timersub(&end_time, &start_time, &diff_time);
		unsigned long msecs;

		msecs = diff_time.tv_sec * 1000 +
			diff_time.tv_usec / 1000;
		if (msecs > timeout)
			condlog(2, "cli cmd '%s' timeout reached "
				"after %lu.%06lu secs", inbuf,
				diff_time.tv_sec, diff_time.tv_usec);
	}
}

void uxsock_cleanup(void *arg)
{
	cli_exit();
	free_polls();
}

/*
 * entry point
 */
void * uxsock_listen(uxsock_trigger_fn uxsock_trigger, void * trigger_data)
{
	int ux_sock;
	size_t len;
	int rlen, timeout;
	char *inbuf;
	char *reply;
	sigset_t mask;

	ux_sock = ux_socket_listen(DEFAULT_SOCKET);

	if (ux_sock == -1) {
		condlog(1, "could not create uxsock: %d", errno);
		return NULL;
	}

	if (!conf) {
		condlog(1, "configuration changed");
		return NULL;
	}

	timeout = conf->uxsock_timeout;

	pthread_cleanup_push(uxsock_cleanup, NULL);

	polls = (struct pollfd *)MALLOC(0);
	pthread_sigmask(SIG_SETMASK, NULL, &mask);
	sigdelset(&mask, SIGHUP);
	sigdelset(&mask, SIGUSR1);
	while (1) {
		struct client *c, *tmp;
		int i, poll_count, num_clients;

		/*
		 * Store configuration timeout;
		 * configuration might change during
		 * the call to 'reconfigure'.
		 */
		if (conf)
			timeout = conf->uxsock_timeout;

		/* setup for a poll */
		pthread_mutex_lock(&client_lock);
		num_clients = 0;
		list_for_each_entry(c, &clients, node) {
			num_clients++;
		}
		polls = REALLOC(polls, (1+num_clients) * sizeof(*polls));
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

		if (poll_count == -1) {
			if (errno == EINTR) {
				handle_signals();
				continue;
			}

			/* something went badly wrong! */
			condlog(0, "poll");
			pthread_exit(NULL);
		}

		if (poll_count == 0)
			continue;

		/* see if a client wants to speak to us */
		for (i = 1; i < num_clients + 1; i++) {
			if (polls[i].revents & POLLIN) {
				struct timeval start_time;

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
					condlog(3, "cli%d: invalid fd %d",
						i, polls[i].fd);
					continue;
				}
				if (gettimeofday(&start_time, NULL) != 0)
					start_time.tv_sec = 0;

				if (recv_packet(c->fd, &inbuf, &len,
						timeout) != 0) {
					dead_client(c);
				} else {
					inbuf[len - 1] = 0;
					condlog(4, "Got request [%s]", inbuf);
					uxsock_trigger(inbuf, &reply, &rlen,
						       trigger_data);
					if (reply) {
						if (send_packet(c->fd, reply,
								rlen) != 0) {
							dead_client(c);
						}
						condlog(4, "Reply [%d bytes]",
							rlen);
						FREE(reply);
						reply = NULL;
					}
					check_timeout(start_time, inbuf,
						      timeout);
					FREE(inbuf);
				}
			}
		}

		/* see if we got a new client */
		if (polls[0].revents & POLLIN) {
			new_client(ux_sock);
		}
	}

	pthread_cleanup_pop(1);
	close(ux_sock);
	return NULL;
}
