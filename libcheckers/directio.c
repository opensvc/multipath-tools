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
#include <linux/kdev_t.h>
#include <asm/unistd.h>
#include <libaio.h>

#include "checkers.h"
#include "../libmultipath/debug.h"

#define MSG_DIRECTIO_UNKNOWN	"directio checker is not available"
#define MSG_DIRECTIO_UP		"directio checker reports path is up"
#define MSG_DIRECTIO_DOWN	"directio checker reports path is down"

struct directio_context {
	int		running;
	int		reset_flags;
	int		blksize;
	unsigned char *	buf;
	unsigned char * ptr;
	io_context_t	ioctx;
	struct iocb	io;
};


int directio_init (struct checker * c)
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
		MSG(c, "cannot get blocksize, set default");
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

	/* Sucessfully initialized, return the context. */
	c->context = (void *) ct;
	return 0;

out:
	if (ct->buf)
		free(ct->buf);
	io_destroy(ct->ioctx);
	free(ct);
	return 1;
}

void directio_free (struct checker * c)
{
	struct directio_context * ct = (struct directio_context *)c->context;
	long flags;

	if (!ct)
		return;

	if (ct->reset_flags) {
		if ((flags = fcntl(c->fd, F_GETFL)) >= 0) {
			flags &= ~O_DIRECT;
			/* No point in checking for errors */
			fcntl(c->fd, F_SETFL, flags);
		}
	}

	if (ct->buf)
		free(ct->buf);
	io_destroy(ct->ioctx);
	free(ct);
}

static int
check_state(int fd, struct directio_context *ct)
{
	struct timespec	timeout = { .tv_sec = 2 };
	struct io_event event;
	struct stat	sb;
	int		rc = PATH_UNCHECKED;
	long		r;

	if (fstat(fd, &sb) == 0) {
		condlog(4, "directio: called for %x", (unsigned) sb.st_rdev);
	}

	if (!ct->running) {
		struct iocb *ios[1] = { &ct->io };

		condlog(3, "directio: starting new request");
		memset(&ct->io, 0, sizeof(struct iocb));
		io_prep_pread(&ct->io, fd, ct->ptr, ct->blksize, 0);
		if (io_submit(ct->ioctx, 1, ios) != 1) {
			condlog(3, "directio: io_submit error %i", errno);
			return PATH_UNCHECKED;
		}
	}
	ct->running = 1;

	r = io_getevents(ct->ioctx, 1L, 1L, &event, &timeout);
	if (r < 1L) {
		condlog(3, "directio: timeout r=%li errno=%i", r, errno);
		rc = PATH_DOWN;
	} else {
		condlog(3, "directio: io finished %lu/%lu", event.res,
			event.res2);
		ct->running = 0;
		rc = (event.res == ct->blksize) ? PATH_UP : PATH_DOWN;
	}

	return rc;
}

int directio (struct checker * c)
{
	int ret;
	struct directio_context * ct = (struct directio_context *)c->context;

	if (!ct)
		return PATH_UNCHECKED;

	ret = check_state(c->fd, ct);

	switch (ret)
	{
	case PATH_UNCHECKED:
		MSG(c, MSG_DIRECTIO_UNKNOWN);
		break;
	case PATH_DOWN:
		MSG(c, MSG_DIRECTIO_DOWN);
		break;
	case PATH_UP:
		MSG(c, MSG_DIRECTIO_UP);
		break;
	default:
		break;
	}
	return ret;
}
