#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

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
	if (write(fd, buf, strlen(buf)) != strlen(buf)) {
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
	struct config *conf;

	conf = get_multipath_config();
	pthread_cleanup_push(put_multipath_config, conf);
	fd = open_file(conf->wwids_file, &can_write, WWIDS_FILE_HEADER);
	pthread_cleanup_pop(1);
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
	if (write(fd, WWIDS_FILE_HEADER, len) != len) {
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
	struct config *conf;

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
	conf = get_multipath_config();
	pthread_cleanup_push(put_multipath_config, conf);
	fd = open_file(conf->wwids_file, &can_write, WWIDS_FILE_HEADER);
	pthread_cleanup_pop(1);
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
	struct config *conf;

	conf = get_multipath_config();
	pthread_cleanup_push(put_multipath_config, conf);
	fd = open_file(conf->wwids_file, &can_write, WWIDS_FILE_HEADER);
	pthread_cleanup_pop(1);
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
	int i, ignore_new_devs, find_multipaths;
	struct path *pp2;
	struct config *conf;

	conf = get_multipath_config();
	ignore_new_devs = ignore_new_devs_on(conf);
	find_multipaths = find_multipaths_on(conf);
	put_multipath_config(conf);
	if (!find_multipaths && !ignore_new_devs)
		return 1;

	condlog(4, "checking if %s should be multipathed", pp1->dev);
	if (!ignore_new_devs) {
		char tmp_wwid[WWID_SIZE];
		struct multipath *mp = find_mp_by_wwid(mpvec, pp1->wwid);

		if (mp != NULL &&
		    dm_get_uuid(mp->alias, tmp_wwid, WWID_SIZE) == 0 &&
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
static const char shm_lock[] = ".lock";
static const char shm_header[] = "multipath shm lock file, don't edit";
static char _shm_lock_path[sizeof(shm_dir)+sizeof(shm_lock)];
static const char *shm_lock_path = &_shm_lock_path[0];

static void init_shm_paths(void)
{
	snprintf(_shm_lock_path, sizeof(_shm_lock_path),
		 "%s/%s", shm_dir, shm_lock);
}

static pthread_once_t shm_path_once = PTHREAD_ONCE_INIT;

static int multipath_shm_open(bool rw)
{
	int fd;
	int can_write;

	pthread_once(&shm_path_once, init_shm_paths);
	fd = open_file(shm_lock_path, &can_write, shm_header);

	if (fd >= 0 && rw && !can_write) {
		close(fd);
		condlog(1, "failed to open %s for writing", shm_dir);
		return -1;
	}

	return fd;
}

static void multipath_shm_close(void *arg)
{
	long fd = (long)arg;

	close(fd);
	unlink(shm_lock_path);
}

static int _failed_wwid_op(const char *wwid, bool rw,
			   int (*func)(const char *), const char *msg)
{
	char path[PATH_MAX];
	long lockfd;
	int r = -1;

	if (snprintf(path, sizeof(path), "%s/%s", shm_dir, wwid)
	    >= sizeof(path)) {
		condlog(1, "%s: path name overflow", __func__);
		return -1;
	}

	lockfd = multipath_shm_open(rw);
	if (lockfd == -1)
		return -1;

	pthread_cleanup_push(multipath_shm_close, (void *)lockfd);
	r = func(path);
	pthread_cleanup_pop(1);

	if (r == WWID_FAILED_ERROR)
		condlog(1, "%s: %s: %s", msg, wwid, strerror(errno));
	else if (r == WWID_FAILED_CHANGED)
		condlog(3, "%s: %s", msg, wwid);
	else if (!rw)
		condlog(4, "%s: %s is %s", msg, wwid,
			r == WWID_IS_FAILED ? "failed" : "good");

	return r;
}

static int _is_failed(const char *path)
{
	struct stat st;

	if (lstat(path, &st) == 0)
		return WWID_IS_FAILED;
	else if (errno == ENOENT)
		return WWID_IS_NOT_FAILED;
	else
		return WWID_FAILED_ERROR;
}

static int _mark_failed(const char *path)
{
	/* Called from _failed_wwid_op: we know that shm_lock_path exists */
	if (_is_failed(path) == WWID_IS_FAILED)
		return WWID_FAILED_UNCHANGED;
	return (link(shm_lock_path, path) == 0 ? WWID_FAILED_CHANGED :
		WWID_FAILED_ERROR);
}

static int _unmark_failed(const char *path)
{
	if (_is_failed(path) == WWID_IS_NOT_FAILED)
		return WWID_FAILED_UNCHANGED;
	return (unlink(path) == 0 ? WWID_FAILED_CHANGED : WWID_FAILED_ERROR);
}

#define declare_failed_wwid_op(op, rw) \
int op ## _wwid(const char *wwid) \
{ \
	return _failed_wwid_op(wwid, (rw), _ ## op, #op); \
}

declare_failed_wwid_op(is_failed, false)
declare_failed_wwid_op(mark_failed, true)
declare_failed_wwid_op(unmark_failed, true)
