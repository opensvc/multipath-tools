#include <sys/types.h> /* for pid_t */
#include <sys/stat.h>  /* for open */
#include <errno.h>     /* for EACCESS and EAGAIN */
#include <stdio.h>     /* for snprintf() */
#include <string.h>    /* for memset() */
#include <unistd.h>    /* for ftruncate() */
#include <fcntl.h>     /* for fcntl() */

#include "debug.h"

#include "pidfile.h"

int pidfile_create(const char *pidFile, pid_t pid)
{
	char buf[20];
	struct flock lock;
	int fd, value;

	if((fd = open(pidFile, O_WRONLY | O_CREAT,
		       (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH))) < 0) {
		condlog(0, "Cannot open pidfile [%s], error was [%s]",
			pidFile, strerror(errno));
		return -errno;
	}
	lock.l_type = F_WRLCK;
	lock.l_start = 0;
	lock.l_whence = SEEK_SET;
	lock.l_len = 0;

	if (fcntl(fd, F_SETLK, &lock) < 0) {
		if (errno != EACCES && errno != EAGAIN)
			condlog(0, "Cannot lock pidfile [%s], error was [%s]",
				pidFile, strerror(errno));
		else
			condlog(0, "process is already running");
		goto fail;
	}
	if (ftruncate(fd, 0) < 0) {
		condlog(0, "Cannot truncate pidfile [%s], error was [%s]",
			pidFile, strerror(errno));
		goto fail;
	}
	memset(buf, 0, sizeof(buf));
	snprintf(buf, sizeof(buf)-1, "%u", pid);
	if (write(fd, buf, strlen(buf)) != strlen(buf)) {
		condlog(0, "Cannot write pid to pidfile [%s], error was [%s]",
			pidFile, strerror(errno));
		goto fail;
	}
	if ((value = fcntl(fd, F_GETFD, 0)) < 0) {
		condlog(0, "Cannot get close-on-exec flag from pidfile [%s], "
			"error was [%s]", pidFile, strerror(errno));
		goto fail;
	}
	value |= FD_CLOEXEC;
	if (fcntl(fd, F_SETFD, value) < 0) {
		condlog(0, "Cannot set close-on-exec flag from pidfile [%s], "
			"error was [%s]", pidFile, strerror(errno));
		goto fail;
	}
	return fd;
fail:
	close(fd);
	return -errno;
}
