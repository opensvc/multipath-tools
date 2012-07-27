#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>

#include "checkers.h"
#include "vector.h"
#include "structs.h"
#include "debug.h"
#include "uxsock.h"
#include "file.h"
#include "wwids.h"
#include "defaults.h"

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
check_wwids_file(char *wwid, int write_wwid)
{
	int fd, can_write, found, ret;
	FILE *f;
	fd = open_file(DEFAULT_WWIDS_FILE, &can_write, WWIDS_FILE_HEADER);
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
