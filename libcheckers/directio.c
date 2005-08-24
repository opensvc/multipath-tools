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

#include "path_state.h"
#include "checkers.h"

#define MSG_DIRECTIO_UNKNOWN	"directio checker is not available"
#define MSG_DIRECTIO_UP		"directio checker reports path is up"
#define MSG_DIRECTIO_DOWN	"directio checker reports path is down"

struct readsector0_checker_context {
	void * dummy;
};

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

extern int
directio (int fd, char *msg, void **context)
{
	unsigned char *buf, *ptr;
	struct readsector0_checker_context * ctxt = NULL;
	unsigned long pgsize, numsect;
	int ret, blksize;

	pgsize = getpagesize();
	
	/*
	 * caller passed in a context : use its address
	 */
	if (context)
		ctxt = (struct readsector0_checker_context *) (*context);

	/*
	 * passed in context is uninitialized or volatile context :
	 * initialize it
	 */
	if (!ctxt) {
		ctxt = malloc(sizeof(struct readsector0_checker_context));
		memset(ctxt, 0, sizeof(struct readsector0_checker_context));

		if (!ctxt) {
			MSG("cannot allocate context");
			return -1;
		}
		if (context)
			*context = ctxt;
	}
	if (fd <= 0) {
		MSG("no usable fd");
		ret = -1;
		goto out;
	}
	
	if (ioctl(fd, BLKGETSIZE, &numsect) < 0) {
		MSG("cannot get number of sectors, set default");
		numsect = 0;
	}

	if (ioctl(fd, BLKBSZGET, &blksize) < 0) {
		MSG("cannot get blocksize, set default");
		blksize = 512;
	}

	if (blksize > 4096) {
		/*
		 * Sanity check for DASD; BSZGET is broken
		 */
		blksize = 4096;
	}

	if (!blksize) {
		/*
		 * Blocksize is 0, assume we can't write
		 * to this device.
		 */
		MSG(MSG_DIRECTIO_DOWN);
		ret = PATH_DOWN;
		goto out;
	}

	buf = (unsigned char *)malloc(blksize + pgsize);
	if (!buf){
		goto out;
	}
	ptr = (unsigned char *)(((unsigned long)buf + pgsize - 1) &
				(~(pgsize - 1))); 
	ret = direct_read(fd, ptr, blksize);

	switch (ret)
	{
	case PATH_UNCHECKED:
		MSG(MSG_DIRECTIO_UNKNOWN);
		break;
	case PATH_DOWN:
		MSG(MSG_DIRECTIO_DOWN);
		break;
	case PATH_UP:
		MSG(MSG_DIRECTIO_UP);
		break;
	default:
		break;
	}
	free(buf);

out:
	/*
	 * caller told us he doesn't want to keep the context :
	 * free it
	 */
	if (!context)
		free(ctxt);

	return ret;
}
