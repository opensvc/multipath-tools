/*
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * This file is part of the device-mapper multipath userspace tools.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include "mpath_cmd.h"

/*
 * keep reading until its all read
 */
static ssize_t read_all(int fd, void *buf, size_t len, unsigned int timeout)
{
	size_t total = 0;
	ssize_t n;
	int ret;
	struct pollfd pfd;

	while (len) {
		pfd.fd = fd;
		pfd.events = POLLIN;
		ret = poll(&pfd, 1, timeout);
		if (!ret) {
			errno = ETIMEDOUT;
			return -1;
		} else if (ret < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		} else if (!(pfd.revents & POLLIN))
			continue;
		n = recv(fd, buf, len, 0);
		if (n < 0) {
			if ((errno == EINTR) || (errno == EAGAIN))
				continue;
			return -1;
		}
		if (!n)
			return total;
		buf = n + (char *)buf;
		len -= n;
		total += n;
	}
	return total;
}

/*
 * keep writing until it's all sent
 */
static size_t write_all(int fd, const void *buf, size_t len)
{
	size_t total = 0;

	while (len) {
		ssize_t n = send(fd, buf, len, MSG_NOSIGNAL);
		if (n < 0) {
			if ((errno == EINTR) || (errno == EAGAIN))
				continue;
			return total;
		}
		if (!n)
			return total;
		buf = n + (const char *)buf;
		len -= n;
		total += n;
	}
	return total;
}

/*
 * connect to a unix domain socket
 */
int __mpath_connect(int nonblocking)
{
	int fd, len;
	struct sockaddr_un addr;
	int flags = 0;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_LOCAL;
	addr.sun_path[0] = '\0';
	strncpy(&addr.sun_path[1], DEFAULT_SOCKET, sizeof(addr.sun_path) - 1);
	len = strlen(DEFAULT_SOCKET) + 1 + sizeof(sa_family_t);
	if (len > sizeof(struct sockaddr_un))
		len = sizeof(struct sockaddr_un);

	fd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (fd == -1)
		return -1;

	if (nonblocking) {
		flags = fcntl(fd, F_GETFL, 0);
		if (flags != -1)
			(void)fcntl(fd, F_SETFL, flags|O_NONBLOCK);
	}

	if (connect(fd, (struct sockaddr *)&addr, len) == -1) {
		int err = errno;

		close(fd);
		errno = err;
		return -1;
	}

	if (nonblocking && flags != -1)
		(void)fcntl(fd, F_SETFL, flags);

	return fd;
}

/*
 * connect to a unix domain socket
 */
int mpath_connect(void)
{
	return __mpath_connect(0);
}

int mpath_disconnect(int fd)
{
	return close(fd);
}

ssize_t mpath_recv_reply_len(int fd, unsigned int timeout)
{
	size_t len;
	ssize_t ret;

	ret = read_all(fd, &len, sizeof(len), timeout);
	if (ret < 0)
		return ret;
	if (ret != sizeof(len)) {
		errno = EIO;
		return -1;
	}
	if (len <= 0 || len >= MAX_REPLY_LEN) {
		errno = ERANGE;
		return -1;
	}
	return len;
}

int mpath_recv_reply_data(int fd, char *reply, size_t len,
			  unsigned int timeout)
{
	ssize_t ret;

	ret = read_all(fd, reply, len, timeout);
	if (ret < 0)
		return ret;
	if (ret != len) {
		errno = EIO;
		return -1;
	}
	reply[len - 1] = '\0';
	return 0;
}

int mpath_recv_reply(int fd, char **reply, unsigned int timeout)
{
	int err;
	ssize_t len;

	*reply = NULL;
	len = mpath_recv_reply_len(fd, timeout);
	if (len <= 0)
		return len;
	*reply = malloc(len);
	if (!*reply)
		return -1;
	err = mpath_recv_reply_data(fd, *reply, len, timeout);
	if (err) {
		free(*reply);
		*reply = NULL;
		return -1;
	}
	return 0;
}

int mpath_send_cmd(int fd, const char *cmd)
{
	size_t len;

	if (cmd != NULL)
		len = strlen(cmd) + 1;
	else
		len = 0;
	if (write_all(fd, &len, sizeof(len)) != sizeof(len))
		return -1;
	if (len && write_all(fd, cmd, len) != len)
		return -1;
	return 0;
}

int mpath_process_cmd(int fd, const char *cmd, char **reply,
		      unsigned int timeout)
{
	if (mpath_send_cmd(fd, cmd) != 0)
		return -1;
	return mpath_recv_reply(fd, reply, timeout);
}
