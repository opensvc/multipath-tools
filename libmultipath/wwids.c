#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include <sys/types.h>

#include "checkers.h"
#include "vector.h"
#include "structs.h"
#include "debug.h"
#include "uxsock.h"
#include "file.h"
#include "wwids.h"
#include "defaults.h"
#include "config.h"

/*
 * Copyright (c) 2010 Benjamin Marzinski, Redhat
 */

static int
lookup_wwid(FILE *f, char *wwid) {
	int c;
	char buf[LINE_MAX];
	int count;

	while ((c = fgetc(f)) != EOF){
		if (c != '/') {
			if (fgets(buf, LINE_MAX, f) == NULL)
				return 0;
			else
				continue;
		}
		count = 0;
		while ((c = fgetc(f)) != '/') {
			if (c == EOF)
				return 0;
			if (count >= WWID_SIZE - 1)
				goto next;
			if (wwid[count] == '\0')
				goto next;
			if (c != wwid[count++])
				goto next;
		}
		if (wwid[count] == '\0')
			return 1;
next:
		if (fgets(buf, LINE_MAX, f) == NULL)
			return 0;
	}
	return 0;
}

static int
write_out_wwid(int fd, char *wwid) {
	int ret;
	off_t offset;
	char buf[WWID_SIZE + 3];

	ret = snprintf(buf, WWID_SIZE + 3, "/%s/\n", wwid);
	if (ret >= (WWID_SIZE + 3) || ret < 0){
		condlog(0, "can't format wwid for writing (%d) : %s",
			ret, strerror(errno));
		return -1;
	}
	offset = lseek(fd, 0, SEEK_END);
	if (offset < 0) {
		condlog(0, "can't seek to the end of wwids file : %s",
			strerror(errno));
		return -1;
	}
	if (write_all(fd, buf, strlen(buf)) != strlen(buf)) {
		condlog(0, "cannot write wwid to wwids file : %s",
			strerror(errno));
		if (ftruncate(fd, offset))
			condlog(0, "cannot truncate failed wwid write : %s",
				strerror(errno));
		return -1;
	}
	return 1;
}

int
replace_wwids(vector mp)
{
	int i, fd, can_write;
	struct multipath * mpp;
	size_t len;
	int ret = -1;

	fd = open_file(conf->wwids_file, &can_write, WWIDS_FILE_HEADER);
	if (fd < 0)
		goto out;
	if (!can_write) {
		condlog(0, "cannot replace wwids. wwids file is read-only");
		goto out_file;
	}
	if (ftruncate(fd, 0) < 0) {
		condlog(0, "cannot truncate wwids file : %s", strerror(errno));
		goto out_file;
	}
	if (lseek(fd, 0, SEEK_SET) < 0) {
		condlog(0, "cannot seek to the start of the file : %s",
			strerror(errno));
		goto out_file;
	}
	len = strlen(WWIDS_FILE_HEADER);
	if (write_all(fd, WWIDS_FILE_HEADER, len) != len) {
		condlog(0, "Can't write wwid file header : %s",
			strerror(errno));
		/* cleanup partially written header */
		if (ftruncate(fd, 0) < 0)
			condlog(0, "Cannot truncate header : %s",
				strerror(errno));
		goto out_file;
	}
	if (!mp || !mp->allocated) {
		ret = 0;
		goto out_file;
	}
	vector_foreach_slot(mp, mpp, i) {
		if (write_out_wwid(fd, mpp->wwid) < 0)
			goto out_file;
	}
	ret = 0;
out_file:
	close(fd);
out:
	return ret;
}

int
do_remove_wwid(int fd, char *str) {
	char buf[4097];
	char *ptr;
	off_t start = 0;
	int bytes;

	while (1) {
		if (lseek(fd, start, SEEK_SET) < 0) {
			condlog(0, "wwid file read lseek failed : %s",
				strerror(errno));
			return -1;
		}
		bytes = read(fd, buf, 4096);
		if (bytes < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			condlog(0, "failed to read from wwids file : %s",
				strerror(errno));
			return -1;
		}
		if (!bytes) /* didn't find wwid to remove */
			return 1;
		buf[bytes] = '\0';
		ptr = strstr(buf, str);
		if (ptr != NULL) {
			condlog(3, "found '%s'", str);
			if (lseek(fd, start + (ptr - buf), SEEK_SET) < 0) {
				condlog(0, "write lseek failed : %s",
						strerror(errno));
				return -1;
			}
			while (1) {
				if (write(fd, "#", 1) < 0) {
					if (errno == EINTR || errno == EAGAIN)
						continue;
					condlog(0, "failed to write to wwids file : %s", strerror(errno));
					return -1;
				}
				return 0;
			}
		}
		ptr = strrchr(buf, '\n');
		if (ptr == NULL) { /* shouldn't happen, assume it is EOF */
			condlog(4, "couldn't find newline, assuming end of file");
			return 1;
		}
		start = start + (ptr - buf) + 1;
	}
}


int
remove_wwid(char *wwid) {
	int fd, len, can_write;
	char *str;
	int ret = -1;

	len = strlen(wwid) + 4; /* two slashes the newline and a zero byte */
	str = malloc(len);
	if (str == NULL) {
		condlog(0, "can't allocate memory to remove wwid : %s",
			strerror(errno));
		return -1;
	}
	if (snprintf(str, len, "/%s/\n", wwid) >= len) {
		condlog(0, "string overflow trying to remove wwid");
		goto out;
	}
	condlog(3, "removing line '%s' from wwids file", str);
	fd = open_file(conf->wwids_file, &can_write, WWIDS_FILE_HEADER);
	if (fd < 0)
		goto out;
	if (!can_write) {
		condlog(0, "cannot remove wwid. wwids file is read-only");
		goto out_file;
	}
	ret = do_remove_wwid(fd, str);

out_file:
	close(fd);
out:
	free(str);
	return ret;
}

int
check_wwids_file(char *wwid, int write_wwid)
{
	int fd, can_write, found, ret;
	FILE *f;
	fd = open_file(conf->wwids_file, &can_write, WWIDS_FILE_HEADER);
	if (fd < 0)
		return -1;

	f = fdopen(fd, "r");
	if (!f) {
		condlog(0,"can't fdopen wwids file : %s", strerror(errno));
		close(fd);
		return -1;
	}
	found = lookup_wwid(f, wwid);
	if (found) {
		ret = 0;
		goto out;
	}
	if (!write_wwid) {
		ret = -1;
		goto out;
	}
	if (!can_write) {
		condlog(0, "wwids file is read-only. Can't write wwid");
		ret = -1;
		goto out;
	}

	if (fflush(f) != 0) {
		condlog(0, "cannot fflush wwids file stream : %s",
			strerror(errno));
		ret = -1;
		goto out;
	}

	ret = write_out_wwid(fd, wwid);
out:
	fclose(f);
	return ret;
}

int
remember_wwid(char *wwid)
{
	int ret = check_wwids_file(wwid, 1);
	if (ret < 0){
		condlog(3, "failed writing wwid %s to wwids file", wwid);
		return -1;
	}
	if (ret == 1)
		condlog(3, "wrote wwid %s to wwids file", wwid);
	else
		condlog(4, "wwid %s already in wwids file", wwid);
	return 0;
}
