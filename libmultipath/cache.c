#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>

#include "vector.h"
#include "structs.h"
#include "debug.h"
#include "cache.h"

static void
revoke_cache_info(struct path * pp)
{
	pp->fd = 0;
}

static int
lock_fd (int fd, int flag)
{
	struct flock fl;

	fl.l_type = flag;
	fl.l_whence = 0;
	fl.l_start = 0;
	fl.l_len = 0;

	alarm(MAX_WAIT);

	if (fcntl(fd, F_SETLKW, &fl) == -1) {
		condlog(0, "can't take a write lease on cache file\n");
		return 1;
	}
	alarm(0);
	return 0;
}

int
cache_load (vector pathvec)
{
	int fd;
	int r = 1;
	off_t record_len;
	struct path record;
	struct path * pp;

	fd = open(CACHE_FILE, O_RDONLY);

	if (fd < 0)
		return 1;

	if (lock_fd(fd, F_RDLCK))
		goto out;

	record_len = sizeof(struct path);

	while (read(fd, &record, record_len)) {
		pp = alloc_path();

		if (!pp)
			goto out;

		if (!vector_alloc_slot(pathvec)) {
			free_path(pp);
			goto out;
		}
		vector_set_slot(pathvec, pp);
		memcpy(pp, &record, record_len);
		revoke_cache_info(pp);
	}
	r = 0;
	lock_fd(fd, F_UNLCK);
out:
	close(fd);
	return r;
}

int
cache_dump (vector pathvec)
{
	int i;
	int fd;
	int r = 1;
	off_t record_len;
	struct path * pp;

	fd = open(CACHE_TMPFILE, O_RDWR|O_CREAT, 0600);

	if (fd < 0)
		return 1;

	if (lock_fd(fd, F_WRLCK))
		goto out;

	ftruncate(fd, 0); 
	record_len = sizeof(struct path);

	vector_foreach_slot (pathvec, pp, i) {
		if (write(fd, pp, record_len) < record_len)
			goto out1;
	}
	rename(CACHE_TMPFILE, CACHE_FILE);
	r = 0;
out1:
	lock_fd(fd, F_UNLCK);
out:
	close(fd);
	return r;
}

int
cache_cold (int expire)
{
	time_t t;
	struct stat s;

	if (time(&t) < 0)
		return 1;

	if(stat(CACHE_FILE, &s))
		return 1;

	if ((t - s.st_mtime) < expire)
		return 0;

	return 1;
}
