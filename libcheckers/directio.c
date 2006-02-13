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

#include "checkers.h"

#define MSG_DIRECTIO_UNKNOWN	"directio checker is not available"
#define MSG_DIRECTIO_UP		"directio checker reports path is up"
#define MSG_DIRECTIO_DOWN	"directio checker reports path is down"

struct directio_context {
	int blksize; 
	unsigned char *buf;
	unsigned char *ptr;
};

int directio_init (struct checker * c)
{
	unsigned long pgsize = getpagesize();
	struct directio_context * ct;

	ct = malloc(sizeof(struct directio_context));
	if (!ct)
		return 1;
	c->context = (void *)ct;

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
	ct->ptr = (unsigned char *)(((unsigned long)ct->buf + pgsize - 1) &
		  (~(pgsize - 1))); 

	return 0;
out:
	free(ct);
	return 1;
}

void directio_free (struct checker * c)
{
	struct directio_context * ct = (struct directio_context *)c->context;

	if (!ct)
		return;
	if (ct->buf)
		free(ct->buf);
	free(ct);
}

static int
direct_read (int fd, unsigned char * buff, int size)
{
	long flags;
	int reset_flags = 0;
	int res, retval;

	flags = fcntl(fd,F_GETFL);

	if (flags < 0) {
		return PATH_UNCHECKED;
	}

	if (!(flags & O_DIRECT)) {
		flags |= O_DIRECT;
		if (fcntl(fd,F_SETFL,flags) < 0) {
			return PATH_UNCHECKED;
		}
		reset_flags = 1;
	}

	while ( (res = read(fd,buff,size)) < 0 && errno == EINTR );
	if (res < 0) {
		if (errno == EINVAL) {
			/* O_DIRECT is not available */
			retval = PATH_UNCHECKED;
		} else if (errno == ENOMEM) {
			retval = PATH_UP;
		} else {
			retval = PATH_DOWN;
		}
	} else {
		retval = PATH_UP;
	}
	
	if (reset_flags) {
		flags &= ~O_DIRECT;
		/* No point in checking for errors */
		fcntl(fd,F_SETFL,flags);
	}

	return retval;
}

int directio (struct checker * c)
{
	int ret;
	struct directio_context * ct = (struct directio_context *)c->context;

	ret = direct_read(c->fd, ct->ptr, ct->blksize);

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
