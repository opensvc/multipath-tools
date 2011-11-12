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

#include "debug.h"
#include "uxsock.h"
#include "alias.h"


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

static int
ensure_directories_exist(char *str, mode_t dir_mode)
{
	char *pathname;
	char *end;
	int err;

	pathname = strdup(str);
	if (!pathname){
		condlog(0, "Cannot copy bindings file pathname : %s",
			strerror(errno));
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
lock_bindings_file(int fd)
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
	sigprocmask(SIG_UNBLOCK, &set, &oldset);

	alarm(BINDINGS_FILE_TIMEOUT);
	err = fcntl(fd, F_SETLKW, &lock);
	alarm(0);

	if (err) {
		if (errno != EINTR)
			condlog(0, "Cannot lock bindings file : %s",
					strerror(errno));
		else
			condlog(0, "Bindings file is locked. Giving up.");
	}

	sigprocmask(SIG_SETMASK, &oldset, NULL);
	sigaction(SIGALRM, &oldact, NULL);
	return err;

}


static int
open_bindings_file(char *file, int *can_write)
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
			condlog(3, "Cannot open bindings file [%s] read/write. "
				" trying readonly", file);
			fd = open(file, O_RDONLY);
			if (fd < 0) {
				condlog(0, "Cannot open bindings file [%s] "
					"readonly : %s", file, strerror(errno));
				return -1;
			}
		}
		else {
			condlog(0, "Cannot open bindings file [%s] : %s", file,
				strerror(errno));
			return -1;
		}
	}
	if (*can_write && lock_bindings_file(fd) < 0)
		goto fail;

	memset(&s, 0, sizeof(s));
	if (fstat(fd, &s) < 0){
		condlog(0, "Cannot stat bindings file : %s", strerror(errno));
		goto fail;
	}
	if (s.st_size == 0) {
		if (*can_write == 0)
			goto fail;
		/* If bindings file is empty, write the header */
		size_t len = strlen(BINDINGS_FILE_HEADER);
		if (write_all(fd, BINDINGS_FILE_HEADER, len) != len) {
			condlog(0,
				"Cannot write header to bindings file : %s",
				strerror(errno));
			/* cleanup partially written header */
			ftruncate(fd, 0);
			goto fail;
		}
		fsync(fd);
		condlog(3, "Initialized new bindings file [%s]", file);
	}

	return fd;

fail:
	close(fd);
	return -1;
}

static int
format_devname(char *name, int id, int len, char *prefix)
{
	int pos;
	int prefix_len = strlen(prefix);

	memset(name,0, len);
	strcpy(name, prefix);
	for (pos = len - 1; pos >= prefix_len; pos--) {
		name[pos] = 'a' + id % 26;
		if (id < 26)
			break;
		id /= 26;
		id--;
	}
	memmove(name + prefix_len, name + pos, len - pos);
	name[prefix_len + len - pos] = '\0';
	return (prefix_len + len - pos);
}

static int
scan_devname(char *alias, char *prefix)
{
	char *c;
	int i, n = 0;

	if (!prefix || strncmp(alias, prefix, strlen(prefix)))
		return -1;

	c = alias + strlen(prefix);
	while (*c != '\0' && *c != ' ' && *c != '\t') {
		i = *c - 'a';
		n = ( n * 26 ) + i;
		c++;
		if (*c < 'a' || *c > 'z')
			break;
		n++;
	}

	return n;
}

static int
lookup_binding(FILE *f, char *map_wwid, char **map_alias, char *prefix)
{
	char buf[LINE_MAX];
	unsigned int line_nr = 0;
	int id = 0;

	*map_alias = NULL;

	while (fgets(buf, LINE_MAX, f)) {
		char *c, *alias, *wwid;
		int curr_id;

		line_nr++;
		c = strpbrk(buf, "#\n\r");
		if (c)
			*c = '\0';
		alias = strtok(buf, " \t");
		if (!alias) /* blank line */
			continue;
		curr_id = scan_devname(alias, prefix);
		if (curr_id >= id)
			id = curr_id + 1;
		wwid = strtok(NULL, "");
		if (!wwid){
			condlog(3,
				"Ignoring malformed line %u in bindings file",
				line_nr);
			continue;
		}
		if (strcmp(wwid, map_wwid) == 0){
			condlog(3, "Found matching wwid [%s] in bindings file."
				" Setting alias to %s", wwid, alias);
			*map_alias = strdup(alias);
			if (*map_alias == NULL)
				condlog(0, "Cannot copy alias from bindings "
					"file : %s", strerror(errno));
			return id;
		}
	}
	condlog(3, "No matching wwid [%s] in bindings file.", map_wwid);
	return id;
}

static int
rlookup_binding(FILE *f, char **map_wwid, char *map_alias)
{
	char buf[LINE_MAX];
	unsigned int line_nr = 0;
	int id = 0;

	*map_wwid = NULL;

	while (fgets(buf, LINE_MAX, f)) {
		char *c, *alias, *wwid;
		int curr_id;

		line_nr++;
		c = strpbrk(buf, "#\n\r");
		if (c)
			*c = '\0';
		alias = strtok(buf, " \t");
		if (!alias) /* blank line */
			continue;
		curr_id = scan_devname(alias, NULL); /* TBD: Why this call? */
		if (curr_id >= id)
			id = curr_id + 1;
		wwid = strtok(NULL, "");
		if (!wwid){
			condlog(3,
				"Ignoring malformed line %u in bindings file",
				line_nr);
			continue;
		}
		if (strcmp(alias, map_alias) == 0){
			condlog(3, "Found matching alias [%s] in bindings file."
				"\nSetting wwid to %s", alias, wwid);
			*map_wwid = strdup(wwid);
			if (*map_wwid == NULL)
				condlog(0, "Cannot copy alias from bindings "
					"file : %s", strerror(errno));
			return id;
		}
	}
	condlog(3, "No matching alias [%s] in bindings file.", map_alias);
	return id;
}

static char *
allocate_binding(int fd, char *wwid, int id, char *prefix)
{
	char buf[LINE_MAX];
	off_t offset;
	char *alias, *c;
	int i;

	if (id < 0) {
		condlog(0, "Bindings file full. Cannot allocate new binding");
		return NULL;
	}

	i = format_devname(buf, id, LINE_MAX, prefix);
	c = buf + i;
	snprintf(c,LINE_MAX - i, " %s\n", wwid);
	buf[LINE_MAX - 1] = '\0';

	offset = lseek(fd, 0, SEEK_END);
	if (offset < 0){
		condlog(0, "Cannot seek to end of bindings file : %s",
			strerror(errno));
		return NULL;
	}
	if (write_all(fd, buf, strlen(buf)) != strlen(buf)){
		condlog(0, "Cannot write binding to bindings file : %s",
			strerror(errno));
		/* clear partial write */
		ftruncate(fd, offset);
		return NULL;
	}
	c = strchr(buf, ' ');
	*c = '\0';
	alias = strdup(buf);
	if (alias == NULL)
		condlog(0, "cannot copy new alias from bindings file : %s",
			strerror(errno));
	else
		condlog(3, "Created new binding [%s] for WWID [%s]", alias,
			wwid);
	return alias;
}

char *
get_user_friendly_alias(char *wwid, char *file, char *prefix,
			int bindings_read_only)
{
	char *alias;
	int fd, scan_fd, id;
	FILE *f;
	int can_write;

	if (!wwid || *wwid == '\0') {
		condlog(3, "Cannot find binding for empty WWID");
		return NULL;
	}

	fd = open_bindings_file(file, &can_write);
	if (fd < 0)
		return NULL;

	scan_fd = dup(fd);
	if (scan_fd < 0) {
		condlog(0, "Cannot dup bindings file descriptor : %s",
			strerror(errno));
		close(fd);
		return NULL;
	}

	f = fdopen(scan_fd, "r");
	if (!f) {
		condlog(0, "cannot fdopen on bindings file descriptor : %s",
			strerror(errno));
		close(scan_fd);
		close(fd);
		return NULL;
	}

	id = lookup_binding(f, wwid, &alias, prefix);
	if (id < 0) {
		fclose(f);
		close(scan_fd);
		close(fd);
		return NULL;
	}

	if (!alias && can_write && !bindings_read_only)
		alias = allocate_binding(fd, wwid, id, prefix);

	fclose(f);
	close(scan_fd);
	close(fd);
	return alias;
}

char *
get_user_friendly_wwid(char *alias, char *file)
{
	char *wwid;
	int fd, scan_fd, id, unused;
	FILE *f;

	if (!alias || *alias == '\0') {
		condlog(3, "Cannot find binding for empty alias");
		return NULL;
	}

	fd = open_bindings_file(file, &unused);
	if (fd < 0)
		return NULL;

	scan_fd = dup(fd);
	if (scan_fd < 0) {
		condlog(0, "Cannot dup bindings file descriptor : %s",
			strerror(errno));
		close(fd);
		return NULL;
	}

	f = fdopen(scan_fd, "r");
	if (!f) {
		condlog(0, "cannot fdopen on bindings file descriptor : %s",
			strerror(errno));
		close(scan_fd);
		close(fd);
		return NULL;
	}

	id = rlookup_binding(f, &wwid, alias);
	if (id < 0) {
		fclose(f);
		close(scan_fd);
		close(fd);
		return NULL;
	}

	fclose(f);
	close(scan_fd);
	close(fd);
	return wwid;
}
