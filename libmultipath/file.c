/*
 * Copyright (c) 2005 Christophe Varoqui
 * Copyright (c) 2005 Benjamin Marzinski, Redhat
 */
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include <signal.h>

#include "file.h"
#include "debug.h"
#include "uxsock.h"


/*
 * significant parts of this file were taken from iscsi-bindings.c of the
 * linux-iscsi project.
 * Copyright (C) 2002 Cisco Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * See the file COPYING included with this distribution for more details.
 */

int
ensure_directories_exist(const char *str, mode_t dir_mode)
{
	char *pathname;
	char *end;
	int err;

	pathname = strdup(str);
	if (!pathname){
		condlog(0, "Cannot copy file pathname %s : %s",
			str, strerror(errno));
		return -1;
	}
	end = pathname;
	/* skip leading slashes */
	while (end && *end && (*end == '/'))
		end++;

	while ((end = strchr(end, '/'))) {
		/* if there is another slash, make the dir. */
		*end = '\0';
		err = mkdir(pathname, dir_mode);
		if (err && errno != EEXIST) {
			condlog(0, "Cannot make directory [%s] : %s",
				pathname, strerror(errno));
			free(pathname);
			return -1;
		}
		if (!err)
			condlog(3, "Created dir [%s]", pathname);
		*end = '/';
		end++;
	}
	free(pathname);
	return 0;
}

static void
sigalrm(int sig)
{
	/* do nothing */
}

static int
lock_file(int fd, const char *file_name)
{
	struct sigaction act, oldact;
	sigset_t set, oldset;
	struct flock lock;
	int err;

	memset(&lock, 0, sizeof(lock));
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;

	act.sa_handler = sigalrm;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigemptyset(&set);
	sigaddset(&set, SIGALRM);

	sigaction(SIGALRM, &act, &oldact);
	pthread_sigmask(SIG_UNBLOCK, &set, &oldset);

	alarm(FILE_TIMEOUT);
	err = fcntl(fd, F_SETLKW, &lock);
	alarm(0);

	if (err) {
		if (errno != EINTR)
			condlog(0, "Cannot lock %s : %s", file_name,
				strerror(errno));
		else
			condlog(0, "%s is locked. Giving up.", file_name);
	}

	pthread_sigmask(SIG_SETMASK, &oldset, NULL);
	sigaction(SIGALRM, &oldact, NULL);
	return err;
}

int
open_file(const char *file, int *can_write, const char *header)
{
	int fd;
	struct stat s;

	if (ensure_directories_exist(file, 0700))
		return -1;
	*can_write = 1;
	fd = open(file, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		if (errno == EROFS) {
			*can_write = 0;
			condlog(3, "Cannot open file [%s] read/write. "
				" trying readonly", file);
			fd = open(file, O_RDONLY);
			if (fd < 0) {
				condlog(0, "Cannot open file [%s] "
					"readonly : %s", file, strerror(errno));
				return -1;
			}
		}
		else {
			condlog(0, "Cannot open file [%s] : %s", file,
				strerror(errno));
			return -1;
		}
	}
	if (*can_write && lock_file(fd, file) < 0)
		goto fail;

	memset(&s, 0, sizeof(s));
	if (fstat(fd, &s) < 0){
		condlog(0, "Cannot stat file %s : %s", file, strerror(errno));
		goto fail;
	}
	if (s.st_size == 0) {
		if (*can_write == 0)
			goto fail;
		/* If file is empty, write the header */
		size_t len = strlen(header);
		if (write(fd, header, len) != len) {
			condlog(0,
				"Cannot write header to file %s : %s", file,
				strerror(errno));
			/* cleanup partially written header */
			if (ftruncate(fd, 0))
				condlog(0, "Cannot truncate header : %s",
					strerror(errno));
			goto fail;
		}
		fsync(fd);
		condlog(3, "Initialized new file [%s]", file);
	}

	return fd;

fail:
	close(fd);
	return -1;
}
