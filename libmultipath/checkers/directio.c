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
#include <libaio.h>

#include "checkers.h"
#include "debug.h"
#include "time-util.h"

#define AIO_GROUP_SIZE 1024

/* Note: This checker type relies on the fact that only one checker can be run
 * at a time, since multiple checkers share the same aio_group, and must be
 * able to modify other checker's async_reqs. If multiple checkers become able
 * to be run at the same time, this checker will need to add locking, and
 * probably polling on event fds, to deal with that */

struct aio_group {
	struct list_head node;
	int holders;
	io_context_t ioctx;
	struct list_head orphans;
};

struct async_req {
	struct iocb io;
	unsigned int blksize;
	unsigned char *	buf;
	struct list_head node;
	int state; /* PATH_REMOVED means this is an orphan */
};

static LIST_HEAD(aio_grp_list);

enum {
	MSG_DIRECTIO_UNKNOWN = CHECKER_FIRST_MSGID,
	MSG_DIRECTIO_PENDING,
	MSG_DIRECTIO_BLOCKSIZE,
};

#define IDX_(x) (MSG_DIRECTIO_##x - CHECKER_FIRST_MSGID)
const char *libcheck_msgtable[] = {
	[IDX_(UNKNOWN)] = " is not available",
	[IDX_(PENDING)] = " is waiting on aio",
	[IDX_(BLOCKSIZE)] = " cannot get blocksize, set default",
	NULL,
};

#define LOG(prio, fmt, args...) condlog(prio, "directio: " fmt, ##args)

struct directio_context {
	int		running;
	int		reset_flags;
	struct aio_group *aio_grp;
	struct async_req *req;
};

static struct aio_group *
add_aio_group(void)
{
	struct aio_group *aio_grp;
	int rc;

	aio_grp = malloc(sizeof(struct aio_group));
	if (!aio_grp)
		return NULL;
	memset(aio_grp, 0, sizeof(struct aio_group));
	INIT_LIST_HEAD(&aio_grp->orphans);

	if ((rc = io_setup(AIO_GROUP_SIZE, &aio_grp->ioctx)) != 0) {
		LOG(1, "io_setup failed");
		if (rc == -EAGAIN)
			LOG(1, "global number of io events too small. Increase fs.aio-max-nr with sysctl");
		free(aio_grp);
		return NULL;
	}
	list_add(&aio_grp->node, &aio_grp_list);
	return aio_grp;
}

static int
set_aio_group(struct directio_context *ct)
{
	struct aio_group *aio_grp = NULL;

	list_for_each_entry(aio_grp, &aio_grp_list, node)
		if (aio_grp->holders < AIO_GROUP_SIZE)
			goto found;
	aio_grp = add_aio_group();
	if (!aio_grp) {
		ct->aio_grp = NULL;
		return -1;
	}
found:
	aio_grp->holders++;
	ct->aio_grp = aio_grp;
	return 0;
}

static void
remove_aio_group(struct aio_group *aio_grp)
{
	struct async_req *req, *tmp;

	io_destroy(aio_grp->ioctx);
	list_for_each_entry_safe(req, tmp, &aio_grp->orphans, node) {
		list_del(&req->node);
		free(req->buf);
		free(req);
	}
	list_del(&aio_grp->node);
	free(aio_grp);
}

/* If an aio_group is completely full of orphans, then no checkers can
 * use it, which means that no checkers can clear out the orphans. To
 * avoid keeping the useless group around, simply remove the
 * group */
static void
check_orphaned_group(struct aio_group *aio_grp)
{
	int count = 0;
	struct list_head *item;

	if (aio_grp->holders < AIO_GROUP_SIZE)
		return;
	list_for_each(item, &aio_grp->orphans)
		count++;
	if (count >= AIO_GROUP_SIZE)
		remove_aio_group(aio_grp);
}

void libcheck_reset (void)
{
	struct aio_group *aio_grp, *tmp;

	list_for_each_entry_safe(aio_grp, tmp, &aio_grp_list, node)
		remove_aio_group(aio_grp);
}

int libcheck_init (struct checker * c)
{
	unsigned long pgsize = getpagesize();
	struct directio_context * ct;
	struct async_req *req = NULL;
	long flags;

	ct = malloc(sizeof(struct directio_context));
	if (!ct)
		return 1;
	memset(ct, 0, sizeof(struct directio_context));

	if (set_aio_group(ct) < 0)
		goto out;

	req = malloc(sizeof(struct async_req));
	if (!req) {
		goto out;
	}
	memset(req, 0, sizeof(struct async_req));
	INIT_LIST_HEAD(&req->node);

	if (ioctl(c->fd, BLKBSZGET, &req->blksize) < 0) {
		c->msgid = MSG_DIRECTIO_BLOCKSIZE;
		req->blksize = 4096;
	}
	if (req->blksize > 4096) {
		/*
		 * Sanity check for DASD; BSZGET is broken
		 */
		req->blksize = 4096;
	}
	if (!req->blksize)
		goto out;

	if (posix_memalign((void **)&req->buf, pgsize, req->blksize) != 0)
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

	/* Successfully initialized, return the context. */
	ct->req = req;
	c->context = (void *) ct;
	return 0;

out:
	if (req) {
		if (req->buf)
			free(req->buf);
		free(req);
	}
	if (ct->aio_grp)
		ct->aio_grp->holders--;
	free(ct);
	return 1;
}

void libcheck_free (struct checker * c)
{
	struct directio_context * ct = (struct directio_context *)c->context;
	struct io_event event;
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

	if (ct->running && ct->req->state != PATH_PENDING)
		ct->running = 0;
	if (!ct->running) {
		free(ct->req->buf);
		free(ct->req);
		ct->aio_grp->holders--;
	} else {
		/* Currently a no-op */
		io_cancel(ct->aio_grp->ioctx, &ct->req->io, &event);
		ct->req->state = PATH_REMOVED;
		list_add(&ct->req->node, &ct->aio_grp->orphans);
		check_orphaned_group(ct->aio_grp);
	}

	free(ct);
	c->context = NULL;
}

static int
get_events(struct aio_group *aio_grp, struct timespec *timeout)
{
	struct io_event events[128];
	int i, nr, got_events = 0;
	struct timespec zero_timeout = { .tv_sec = 0, };
	struct timespec *timep = timeout;

	do {
		nr = io_getevents(aio_grp->ioctx, 1, 128, events, timep);
		got_events |= (nr > 0);

		for (i = 0; i < nr; i++) {
			struct async_req *req = container_of(events[i].obj, struct async_req, io);

			LOG(4, "io finished %lu/%lu", events[i].res,
			    events[i].res2);

			/* got an orphaned request */
			if (req->state == PATH_REMOVED) {
				list_del(&req->node);
				free(req->buf);
				free(req);
				aio_grp->holders--;
			} else
				req->state = (events[i].res == req->blksize) ?
					      PATH_UP : PATH_DOWN;
		}
		timep = &zero_timeout;
	} while (nr == 128); /* assume there are more events and try again */

	if (nr < 0)
		LOG(4, "async io getevents returned %s", strerror(-nr));

	return got_events;
}

static int
check_state(int fd, struct directio_context *ct, int sync, int timeout_secs)
{
	struct timespec	timeout = { .tv_nsec = 1000 };
	struct stat	sb;
	int		rc;
	long		r;
	struct timespec currtime, endtime;

	if (fstat(fd, &sb) == 0) {
		LOG(4, "called for %x", (unsigned) sb.st_rdev);
	}
	if (sync > 0) {
		LOG(4, "called in synchronous mode");
		timeout.tv_sec  = timeout_secs;
		timeout.tv_nsec = 0;
	}

	if (ct->running) {
		if (ct->req->state != PATH_PENDING) {
			ct->running = 0;
			return ct->req->state;
		}
	} else {
		struct iocb *ios[1] = { &ct->req->io };

		LOG(4, "starting new request");
		memset(&ct->req->io, 0, sizeof(struct iocb));
		io_prep_pread(&ct->req->io, fd, ct->req->buf,
			      ct->req->blksize, 0);
		ct->req->state = PATH_PENDING;
		if ((rc = io_submit(ct->aio_grp->ioctx, 1, ios)) != 1) {
			LOG(3, "io_submit error %i", -rc);
			return PATH_UNCHECKED;
		}
	}
	ct->running++;

	get_monotonic_time(&endtime);
	endtime.tv_sec += timeout.tv_sec;
	endtime.tv_nsec += timeout.tv_nsec;
	normalize_timespec(&endtime);
	while(1) {
		r = get_events(ct->aio_grp, &timeout);

		if (ct->req->state != PATH_PENDING) {
			ct->running = 0;
			return ct->req->state;
		} else if (r == 0 ||
			   (timeout.tv_sec == 0 && timeout.tv_nsec == 0))
			break;

		get_monotonic_time(&currtime);
		timespecsub(&endtime, &currtime, &timeout);
		if (timeout.tv_sec < 0)
			timeout.tv_sec = timeout.tv_nsec = 0;
	}
	if (ct->running > timeout_secs || sync) {
		struct io_event event;

		LOG(3, "abort check on timeout");

		io_cancel(ct->aio_grp->ioctx, &ct->req->io, &event);
		rc = PATH_DOWN;
	} else {
		LOG(4, "async io pending");
		rc = PATH_PENDING;
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
