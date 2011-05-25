/*
 * Original author : tridge@samba.org, January 2002
 *
 * Copyright (c) 2005 Christophe Varoqui
 * Copyright (c) 2005 Alasdair Kergon, Redhat
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/poll.h>
#include <signal.h>
#include <errno.h>

#include "memory.h"
#include "uxsock.h"

/*
 * connect to a unix domain socket
 */
int ux_socket_connect(const char *name)
{
	int fd;
	struct sockaddr_un addr;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, name, sizeof(addr.sun_path));

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		return -1;
	}

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		close(fd);
		return -1;
	}

	return fd;
}

/*
 * create a unix domain socket and start listening on it
 * return a file descriptor open on the socket
 */
int ux_socket_listen(const char *name)
{
	int fd;
	struct sockaddr_un addr;

	/* get rid of any old socket */
	unlink(name);

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) return -1;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, name, sizeof(addr.sun_path));

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		close(fd);
		return -1;
	}

	if (listen(fd, 10) == -1) {
		close(fd);
		return -1;
	}

	return fd;
}

/*
 * keep writing until it's all sent
 */
size_t write_all(int fd, const void *buf, size_t len)
{
	size_t total = 0;

	while (len) {
		ssize_t n = write(fd, buf, len);
		if (n < 0) {
			if ((errno == EINTR) || (errno == EAGAIN))
				continue;
			return total;
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
 * keep reading until its all read
 */
size_t read_all(int fd, void *buf, size_t len)
{
	size_t total = 0;

	while (len) {
		ssize_t n = read(fd, buf, len);
		if (n < 0) {
			if ((errno == EINTR) || (errno == EAGAIN))
				continue;
			return total;
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
 * send a packet in length prefix format
 */
int send_packet(int fd, const char *buf, size_t len)
{
	int ret = 0;
	sigset_t set, old;

	/* Block SIGPIPE */
	sigemptyset(&set);
	sigaddset(&set, SIGPIPE);
	pthread_sigmask(SIG_BLOCK, &set, &old);

	if (write_all(fd, &len, sizeof(len)) != sizeof(len))
		ret = -1;
	if (!ret && write_all(fd, buf, len) != len)
		ret = -1;

	/* And unblock it again */
	pthread_sigmask(SIG_SETMASK, &old, NULL);

	return ret;
}

/*
 * receive a packet in length prefix format
 */
int recv_packet(int fd, char **buf, size_t *len)
{
	if (read_all(fd, len, sizeof(*len)) != sizeof(*len)) {
		(*buf) = NULL;
		*len = 0;
		return -1;
	}
	if (len == 0) {
		(*buf) = NULL;
		return 0;
	}
	(*buf) = MALLOC(*len);
	if (!*buf)
		return -1;
	if (read_all(fd, *buf, *len) != *len) {
		FREE(*buf);
		(*buf) = NULL;
		*len = 0;
		return -1;
	}
	return 0;
}
