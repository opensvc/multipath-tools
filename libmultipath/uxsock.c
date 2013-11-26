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
#ifdef USE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#include "memory.h"
#include "uxsock.h"
#include "debug.h"

/*
 * connect to a unix domain socket
 */
int ux_socket_connect(const char *name)
{
	int fd, len;
	struct sockaddr_un addr;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_LOCAL;
	addr.sun_path[0] = '\0';
	len = strlen(name) + 1 + sizeof(sa_family_t);
	strncpy(&addr.sun_path[1], name, len);

	fd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (fd == -1) {
		condlog(3, "Couldn't create ux_socket, error %d", errno);
		return -1;
	}

	if (connect(fd, (struct sockaddr *)&addr, len) == -1) {
		condlog(3, "Couldn't connect to ux_socket, error %d", errno);
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
	int fd, len;
#ifdef USE_SYSTEMD
	int num;
#endif
	struct sockaddr_un addr;

#ifdef USE_SYSTEMD
	num = sd_listen_fds(0);
	if (num > 1) {
		condlog(3, "sd_listen_fds returned %d fds", num);
		return -1;
	} else if (num == 1) {
		fd = SD_LISTEN_FDS_START + 0;
		condlog(3, "using fd %d from sd_listen_fds", fd);
		return fd;
	}
#endif
	fd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (fd == -1) {
		condlog(3, "Couldn't create ux_socket, error %d", errno);
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_LOCAL;
	addr.sun_path[0] = '\0';
	len = strlen(name) + 1 + sizeof(sa_family_t);
	strncpy(&addr.sun_path[1], name, len);

	if (bind(fd, (struct sockaddr *)&addr, len) == -1) {
		condlog(3, "Couldn't bind to ux_socket, error %d", errno);
		close(fd);
		return -1;
	}

	if (listen(fd, 10) == -1) {
		condlog(3, "Couldn't listen to ux_socket, error %d", errno);
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
	ssize_t n;
	int ret;
	struct pollfd pfd;

	while (len) {
		pfd.fd = fd;
		pfd.events = POLLIN;
		ret = poll(&pfd, 1, 1000);
		if (!ret) {
			errno = ETIMEDOUT;
			return total;
		} else if (ret < 0) {
			if (errno == EINTR)
				continue;
			return total;
		} else if (!pfd.revents & POLLIN)
			continue;
		n = read(fd, buf, len);
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
