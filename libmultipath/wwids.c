#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "util.h"
#include "checkers.h"
#include "vector.h"
#include "structs.h"
#include "debug.h"
#include "uxsock.h"
#include "file.h"
#include "wwids.h"
#include "defaults.h"
#include "config.h"
#include "devmapper.h"

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
	if (write(fd, buf, strlen(buf)) != (ssize_t)strlen(buf)) {
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
	int i, can_write;
	int fd = -1;
	struct multipath * mpp;
	size_t len;
	int ret = -1;

	fd = open_file(DEFAULT_WWIDS_FILE, &can_write, WWIDS_FILE_HEADER);
	if (fd < 0)
		goto out;

	pthread_cleanup_push(cleanup_fd_ptr, &fd);
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
	if (write(fd, WWIDS_FILE_HEADER, len) != (ssize_t)len) {
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
	pthread_cleanup_pop(1);
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
	int fd = -1;
	int len, can_write;
	char *str;
	int ret = -1;

	len = strlen(wwid) + 4; /* two slashes the newline and a zero byte */
	str = malloc(len);
	if (str == NULL) {
		condlog(0, "can't allocate memory to remove wwid : %s",
			strerror(errno));
		return -1;
	}
	pthread_cleanup_push(free, str);
	if (snprintf(str, len, "/%s/\n", wwid) >= len) {
		condlog(0, "string overflow trying to remove wwid");
		ret = -1;
		goto out;
	}
	condlog(3, "removing line '%s' from wwids file", str);
	fd = open_file(DEFAULT_WWIDS_FILE, &can_write, WWIDS_FILE_HEADER);

	if (fd < 0) {
		ret = -1;
		goto out;
	}

	pthread_cleanup_push(cleanup_fd_ptr, &fd);
	if (!can_write) {
		ret = -1;
		condlog(0, "cannot remove wwid. wwids file is read-only");
	} else
		ret = do_remove_wwid(fd, str);
	pthread_cleanup_pop(1);
out:
	/* free(str) */
	pthread_cleanup_pop(1);
	return ret;
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
should_multipath(struct path *pp1, vector pathvec, vector mpvec)
{
	int i, find_multipaths;
	struct path *pp2;
	struct config *conf;

	conf = get_multipath_config();
	find_multipaths = conf->find_multipaths;
	put_multipath_config(conf);
	if (find_multipaths == FIND_MULTIPATHS_OFF ||
	    find_multipaths == FIND_MULTIPATHS_GREEDY)
		return 1;

	condlog(4, "checking if %s should be multipathed", pp1->dev);
	if (find_multipaths != FIND_MULTIPATHS_STRICT) {
		char tmp_wwid[WWID_SIZE];
		struct multipath *mp = find_mp_by_wwid(mpvec, pp1->wwid);

		if (mp != NULL &&
		    dm_get_wwid(mp->alias, tmp_wwid, WWID_SIZE) == 0 &&
		    !strncmp(tmp_wwid, pp1->wwid, WWID_SIZE)) {
			condlog(3, "wwid %s is already multipathed, keeping it",
				pp1->wwid);
			return 1;
		}
		vector_foreach_slot(pathvec, pp2, i) {
			if (pp1->dev == pp2->dev)
				continue;
			if (strncmp(pp1->wwid, pp2->wwid, WWID_SIZE) == 0) {
				condlog(3, "found multiple paths with wwid %s, "
					"multipathing %s", pp1->wwid, pp1->dev);
				return 1;
			}
		}
	}
	if (check_wwids_file(pp1->wwid, 0) < 0) {
		condlog(3, "wwid %s not in wwids file, skipping %s",
			pp1->wwid, pp1->dev);
		return 0;
	}
	condlog(3, "found wwid %s in wwids file, multipathing %s", pp1->wwid,
		pp1->dev);
	return 1;
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
	return ret;
}

static const char shm_dir[] = MULTIPATH_SHM_BASE "failed_wwids";

static void print_failed_wwid_result(const char * msg, const char *wwid, int r)
{
	switch(r) {
	case WWID_FAILED_ERROR:
		condlog(1, "%s: %s: %m", msg, wwid);
		return;
	case WWID_IS_FAILED:
	case WWID_IS_NOT_FAILED:
		condlog(4, "%s: %s is %s", msg, wwid,
			r == WWID_IS_FAILED ? "failed" : "good");
		return;
	case WWID_FAILED_CHANGED:
		condlog(3, "%s: %s", msg, wwid);
	}
}

int is_failed_wwid(const char *wwid)
{
	struct stat st;
	char path[PATH_MAX];
	int r;

	if (safe_sprintf(path, "%s/%s", shm_dir, wwid)) {
		condlog(1, "%s: path name overflow", __func__);
		return WWID_FAILED_ERROR;
	}

	if (lstat(path, &st) == 0)
		r = WWID_IS_FAILED;
	else if (errno == ENOENT)
		r = WWID_IS_NOT_FAILED;
	else
		r = WWID_FAILED_ERROR;

	print_failed_wwid_result("is_failed", wwid, r);
	return r;
}

int mark_failed_wwid(const char *wwid)
{
	char tmpfile[WWID_SIZE + 2 * sizeof(long) + 1];
	int r = WWID_FAILED_ERROR, fd, dfd;

	dfd = open(shm_dir, O_RDONLY|O_DIRECTORY);
	if (dfd == -1 && errno == ENOENT) {
		char path[sizeof(shm_dir) + 2];

		/* arg for ensure_directories_exist() must not end with "/" */
		safe_sprintf(path, "%s/_", shm_dir);
		ensure_directories_exist(path, 0700);
		dfd = open(shm_dir, O_RDONLY|O_DIRECTORY);
	}
	if (dfd == -1) {
		condlog(1, "%s: can't setup %s: %m", __func__, shm_dir);
		return WWID_FAILED_ERROR;
	}

	safe_sprintf(tmpfile, "%s.%lx", wwid, (long)getpid());
	fd = openat(dfd, tmpfile, O_RDONLY | O_CREAT | O_EXCL, S_IRUSR);
	if (fd >= 0)
		close(fd);
	else
		goto out_closedir;

	if (linkat(dfd, tmpfile, dfd, wwid, 0) == 0)
		r = WWID_FAILED_CHANGED;
	else if (errno == EEXIST)
		r = WWID_FAILED_UNCHANGED;
	else
		r = WWID_FAILED_ERROR;

	if (unlinkat(dfd, tmpfile, 0) == -1)
		condlog(2, "%s: failed to unlink %s/%s: %m",
			__func__, shm_dir, tmpfile);

out_closedir:
	close(dfd);
	print_failed_wwid_result("mark_failed", wwid, r);
	return r;
}

int unmark_failed_wwid(const char *wwid)
{
	char path[PATH_MAX];
	int r;

	if (safe_sprintf(path, "%s/%s", shm_dir, wwid)) {
		condlog(1, "%s: path name overflow", __func__);
		return WWID_FAILED_ERROR;
	}

	if (unlink(path) == 0)
		r = WWID_FAILED_CHANGED;
	else if (errno == ENOENT)
		r = WWID_FAILED_UNCHANGED;
	else
		r = WWID_FAILED_ERROR;

	print_failed_wwid_result("unmark_failed", wwid, r);
	return r;
}
