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
#include <poll.h>
#include <signal.h>
#include <errno.h>
#ifdef USE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif
#include "mpath_cmd.h"

#include "memory.h"
#include "uxsock.h"
#include "debug.h"

/*
 * Code is similar with mpath_recv_reply() with data size limitation
 * and debug-able malloc.
 * When limit == 0, it means no limit on data size, used for socket client
 * to receiving data from multipathd.
 */
static int _recv_packet(int fd, char **buf, unsigned int timeout,
			ssize_t limit);

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
	len = strlen(name) + 1;
	if (len >= sizeof(addr.sun_path))
		len = sizeof(addr.sun_path) - 1;
	memcpy(&addr.sun_path[1], name, len);

	len += sizeof(sa_family_t);
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
 * send a packet in length prefix format
 */
int send_packet(int fd, const char *buf)
{
	if (mpath_send_cmd(fd, buf) < 0)
		return -errno;
	return 0;
}

static int _recv_packet(int fd, char **buf, unsigned int timeout, ssize_t limit)
{
	int err = 0;
	ssize_t len = 0;

	*buf = NULL;
	len = mpath_recv_reply_len(fd, timeout);
	if (len == 0)
		return len;
	if (len < 0)
		return -errno;
	if ((limit > 0) && (len > limit))
		return -EINVAL;
	(*buf) = MALLOC(len);
	if (!*buf)
		return -ENOMEM;
	err = mpath_recv_reply_data(fd, *buf, len, timeout);
	if (err != 0) {
		FREE(*buf);
		(*buf) = NULL;
		return -errno;
	}
	return err;
}

/*
 * receive a packet in length prefix format
 */
int recv_packet(int fd, char **buf, unsigned int timeout)
{
	return _recv_packet(fd, buf, timeout, 0 /* no limit */);
}

int recv_packet_from_client(int fd, char **buf, unsigned int timeout)
{
	return _recv_packet(fd, buf, timeout, _MAX_CMD_LEN);
}
