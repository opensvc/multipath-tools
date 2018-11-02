/*
 * Copyright (c) 2005 Hannes Reinecke, Suse
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <errno.h>
#include <unistd.h>
#include <libaio.h>

#include "checkers.h"
#include "../libmultipath/debug.h"

enum {
	MSG_DIRECTIO_UNKNOWN = CHECKER_FIRST_MSGID,
	MSG_DIRECTIO_PENDING,
	MSG_DIRECTIO_BLOCKSIZE,
};

#define _IDX(x) (MSG_DIRECTIO_##x - CHECKER_FIRST_MSGID)
const char *libcheck_msgtable[] = {
	[_IDX(UNKNOWN)] = " is not available",
	[_IDX(PENDING)] = " is waiting on aio",
	[_IDX(BLOCKSIZE)] = " cannot get blocksize, set default",
	NULL,
};

#define LOG(prio, fmt, args...) condlog(prio, "directio: " fmt, ##args)

struct directio_context {
	int		running;
	int		reset_flags;
	int		blksize;
	unsigned char *	buf;
	unsigned char * ptr;
	io_context_t	ioctx;
	struct iocb	io;
};


int libcheck_init (struct checker * c)
{
	unsigned long pgsize = getpagesize();
	struct directio_context * ct;
	long flags;

	ct = malloc(sizeof(struct directio_context));
	if (!ct)
		return 1;
	memset(ct, 0, sizeof(struct directio_context));

	if (io_setup(1, &ct->ioctx) != 0) {
		condlog(1, "io_setup failed");
		free(ct);
		return 1;
	}

	if (ioctl(c->fd, BLKBSZGET, &ct->blksize) < 0) {
		c->msgid = MSG_DIRECTIO_BLOCKSIZE;
		ct->blksize = 512;
	}
	if (ct->blksize > 4096) {
		/*
		 * Sanity check for DASD; BSZGET is broken
		 */
		ct->blksize = 4096;
	}
	if (!ct->blksize)
		goto out;
	ct->buf = (unsigned char *)malloc(ct->blksize + pgsize);
	if (!ct->buf)
		goto out;

	flags = fcntl(c->fd, F_GETFL);
	if (flags < 0)
		goto out;
	if (!(flags & O_DIRECT)) {
		flags |= O_DIRECT;
		if (fcntl(c->fd, F_SETFL, flags) < 0)
			goto out;
		ct->reset_flags = 1;
	}

	ct->ptr = (unsigned char *) (((unsigned long)ct->buf + pgsize - 1) &
		  (~(pgsize - 1)));

	/* Successfully initialized, return the context. */
	c->context = (void *) ct;
	return 0;

out:
	if (ct->buf)
		free(ct->buf);
	io_destroy(ct->ioctx);
	free(ct);
	return 1;
}

void libcheck_free (struct checker * c)
{
	struct directio_context * ct = (struct directio_context *)c->context;
	long flags;

	if (!ct)
		return;

	if (ct->reset_flags) {
		if ((flags = fcntl(c->fd, F_GETFL)) >= 0) {
			int ret __attribute__ ((unused));

			flags &= ~O_DIRECT;
			/* No point in checking for errors */
			ret = fcntl(c->fd, F_SETFL, flags);
		}
	}

	if (ct->buf)
		free(ct->buf);
	io_destroy(ct->ioctx);
	free(ct);
}

static int
check_state(int fd, struct directio_context *ct, int sync, int timeout_secs)
{
	struct timespec	timeout = { .tv_nsec = 5 };
	struct io_event event;
	struct stat	sb;
	int		rc = PATH_UNCHECKED;
	long		r;

	if (fstat(fd, &sb) == 0) {
		LOG(4, "called for %x", (unsigned) sb.st_rdev);
	}
	if (sync > 0) {
		LOG(4, "called in synchronous mode");
		timeout.tv_sec  = timeout_secs;
		timeout.tv_nsec = 0;
	}

	if (!ct->running) {
		struct iocb *ios[1] = { &ct->io };

		LOG(3, "starting new request");
		memset(&ct->io, 0, sizeof(struct iocb));
		io_prep_pread(&ct->io, fd, ct->ptr, ct->blksize, 0);
		if (io_submit(ct->ioctx, 1, ios) != 1) {
			LOG(3, "io_submit error %i", errno);
			return PATH_UNCHECKED;
		}
	}
	ct->running++;

	errno = 0;
	r = io_getevents(ct->ioctx, 1L, 1L, &event, &timeout);

	if (r < 0 ) {
		LOG(3, "async io getevents returned %li (errno=%s)", r,
		    strerror(errno));
		ct->running = 0;
		rc = PATH_UNCHECKED;
	} else if (r < 1L) {
		if (ct->running > timeout_secs || sync) {
			struct iocb *ios[1] = { &ct->io };

			LOG(3, "abort check on timeout");
			r = io_cancel(ct->ioctx, ios[0], &event);
			/*
			 * Only reset ct->running if we really
			 * could abort the pending I/O
			 */
			if (r)
				LOG(3, "io_cancel error %i", errno);
			else
				ct->running = 0;
			rc = PATH_DOWN;
		} else {
			LOG(3, "async io pending");
			rc = PATH_PENDING;
		}
	} else {
		LOG(3, "io finished %lu/%lu", event.res, event.res2);
		ct->running = 0;
		rc = (event.res == ct->blksize) ? PATH_UP : PATH_DOWN;
	}

	return rc;
}

int libcheck_check (struct checker * c)
{
	int ret;
	struct directio_context * ct = (struct directio_context *)c->context;

	if (!ct)
		return PATH_UNCHECKED;

	ret = check_state(c->fd, ct, checker_is_sync(c), c->timeout);

	switch (ret)
	{
	case PATH_UNCHECKED:
		c->msgid = MSG_DIRECTIO_UNKNOWN;
		break;
	case PATH_DOWN:
		c->msgid = CHECKER_MSGID_DOWN;
		break;
	case PATH_UP:
		c->msgid = CHECKER_MSGID_UP;
		break;
	case PATH_PENDING:
		c->msgid = MSG_DIRECTIO_PENDING;
		break;
	default:
		break;
	}
	return ret;
}
