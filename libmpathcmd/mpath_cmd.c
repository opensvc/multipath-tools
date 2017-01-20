#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <string.h>
#include <errno.h>

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
		buf = n + (char *)buf;
		len -= n;
		total += n;
	}
	return total;
}

/*
 * connect to a unix domain socket
 */
int mpath_connect(void)
{
	int fd, len;
	struct sockaddr_un addr;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_LOCAL;
	addr.sun_path[0] = '\0';
	len = strlen(DEFAULT_SOCKET) + 1 + sizeof(sa_family_t);
	strncpy(&addr.sun_path[1], DEFAULT_SOCKET, len);

	fd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (fd == -1)
		return -1;

	if (connect(fd, (struct sockaddr *)&addr, len) == -1) {
		close(fd);
		return -1;
	}

	return fd;
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
		return err;
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
