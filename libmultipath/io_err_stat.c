/*
 * (C) Copyright HUAWEI Technology Corp. 2017, All Rights Reserved.
 *
 * io_err_stat.c
 * version 1.0
 *
 * IO error stream statistic process for path failure event from kernel
 *
 * Author(s): Guan Junxiong 2017 <guanjunxiong@huawei.com>
 *
 * This file is released under the GPL version 2, or any later version.
 */

#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <libaio.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/select.h>

#include "vector.h"
#include "memory.h"
#include "checkers.h"
#include "config.h"
#include "structs.h"
#include "structs_vec.h"
#include "devmapper.h"
#include "debug.h"
#include "lock.h"
#include "time-util.h"
#include "io_err_stat.h"

#define IOTIMEOUT_SEC			60
#define TIMEOUT_NO_IO_NSEC		10000000 /*10ms = 10000000ns*/
#define FLAKY_PATHFAIL_THRESHOLD	2
#define CONCUR_NR_EVENT			32

#define PATH_IO_ERR_IN_CHECKING		-1
#define PATH_IO_ERR_WAITING_TO_CHECK	-2

#define io_err_stat_log(prio, fmt, args...) \
	condlog(prio, "io error statistic: " fmt, ##args)


struct io_err_stat_pathvec {
	pthread_mutex_t mutex;
	vector		pathvec;
};

struct dio_ctx {
	struct timespec	io_starttime;
	int		blksize;
	void		*buf;
	struct iocb	io;
};

struct io_err_stat_path {
	char		devname[FILE_NAME_SIZE];
	int		fd;
	struct dio_ctx	*dio_ctx_array;
	int		io_err_nr;
	int		io_nr;
	struct timespec	start_time;

	int		total_time;
	int		err_rate_threshold;
};

pthread_t		io_err_stat_thr;
pthread_attr_t		io_err_stat_attr;

static pthread_mutex_t io_err_thread_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t io_err_thread_cond = PTHREAD_COND_INITIALIZER;
static int io_err_thread_running = 0;

static struct io_err_stat_pathvec *paths;
struct vectors *vecs;
io_context_t	ioctx;

static void cancel_inflight_io(struct io_err_stat_path *pp);

static void rcu_unregister(void *param)
{
	rcu_unregister_thread();
}

struct io_err_stat_path *find_err_path_by_dev(vector pathvec, char *dev)
{
	int i;
	struct io_err_stat_path *pp;

	if (!pathvec)
		return NULL;
	vector_foreach_slot(pathvec, pp, i)
		if (!strcmp(pp->devname, dev))
			return pp;

	io_err_stat_log(4, "%s: not found in check queue", dev);

	return NULL;
}

static int init_each_dio_ctx(struct dio_ctx *ct, int blksize,
		unsigned long pgsize)
{
	ct->blksize = blksize;
	if (posix_memalign(&ct->buf, pgsize, blksize))
		return 1;
	memset(ct->buf, 0, blksize);
	ct->io_starttime.tv_sec = 0;
	ct->io_starttime.tv_nsec = 0;

	return 0;
}

static void deinit_each_dio_ctx(struct dio_ctx *ct)
{
	if (ct->buf)
		free(ct->buf);
}

static int setup_directio_ctx(struct io_err_stat_path *p)
{
	unsigned long pgsize = getpagesize();
	char fpath[PATH_MAX];
	int blksize = 0;
	int i;

	if (snprintf(fpath, PATH_MAX, "/dev/%s", p->devname) >= PATH_MAX)
		return 1;
	if (p->fd < 0)
		p->fd = open(fpath, O_RDONLY | O_DIRECT);
	if (p->fd < 0)
		return 1;

	p->dio_ctx_array = MALLOC(sizeof(struct dio_ctx) * CONCUR_NR_EVENT);
	if (!p->dio_ctx_array)
		goto fail_close;

	if (ioctl(p->fd, BLKBSZGET, &blksize) < 0) {
		io_err_stat_log(4, "%s:cannot get blocksize, set default 512",
				p->devname);
		blksize = 512;
	}
	if (!blksize)
		goto free_pdctx;

	for (i = 0; i < CONCUR_NR_EVENT; i++) {
		if (init_each_dio_ctx(p->dio_ctx_array + i, blksize, pgsize))
			goto deinit;
	}
	return 0;

deinit:
	for (i = 0; i < CONCUR_NR_EVENT; i++)
		deinit_each_dio_ctx(p->dio_ctx_array + i);
free_pdctx:
	FREE(p->dio_ctx_array);
fail_close:
	close(p->fd);

	return 1;
}

static void destroy_directio_ctx(struct io_err_stat_path *p)
{
	int i;

	if (!p || !p->dio_ctx_array)
		return;
	cancel_inflight_io(p);

	for (i = 0; i < CONCUR_NR_EVENT; i++)
		deinit_each_dio_ctx(p->dio_ctx_array + i);
	FREE(p->dio_ctx_array);

	if (p->fd > 0)
		close(p->fd);
}

static struct io_err_stat_path *alloc_io_err_stat_path(void)
{
	struct io_err_stat_path *p;

	p = (struct io_err_stat_path *)MALLOC(sizeof(*p));
	if (!p)
		return NULL;

	memset(p->devname, 0, sizeof(p->devname));
	p->io_err_nr = 0;
	p->io_nr = 0;
	p->total_time = 0;
	p->start_time.tv_sec = 0;
	p->start_time.tv_nsec = 0;
	p->err_rate_threshold = 0;
	p->fd = -1;

	return p;
}

static void free_io_err_stat_path(struct io_err_stat_path *p)
{
	FREE(p);
}

static struct io_err_stat_pathvec *alloc_pathvec(void)
{
	struct io_err_stat_pathvec *p;
	int r;

	p = (struct io_err_stat_pathvec *)MALLOC(sizeof(*p));
	if (!p)
		return NULL;
	p->pathvec = vector_alloc();
	if (!p->pathvec)
		goto out_free_struct_pathvec;
	r = pthread_mutex_init(&p->mutex, NULL);
	if (r)
		goto out_free_member_pathvec;

	return p;

out_free_member_pathvec:
	vector_free(p->pathvec);
out_free_struct_pathvec:
	FREE(p);
	return NULL;
}

static void free_io_err_pathvec(struct io_err_stat_pathvec *p)
{
	struct io_err_stat_path *path;
	int i;

	if (!p)
		return;
	pthread_mutex_destroy(&p->mutex);
	if (!p->pathvec) {
		vector_foreach_slot(p->pathvec, path, i) {
			destroy_directio_ctx(path);
			free_io_err_stat_path(path);
		}
		vector_free(p->pathvec);
	}
	FREE(p);
}

/*
 * return value
 * 0: enqueue OK
 * 1: fails because of internal error
 */
static int enqueue_io_err_stat_by_path(struct path *path)
{
	struct io_err_stat_path *p;

	pthread_mutex_lock(&paths->mutex);
	p = find_err_path_by_dev(paths->pathvec, path->dev);
	if (p) {
		pthread_mutex_unlock(&paths->mutex);
		return 0;
	}
	pthread_mutex_unlock(&paths->mutex);

	p = alloc_io_err_stat_path();
	if (!p)
		return 1;

	memcpy(p->devname, path->dev, sizeof(p->devname));
	p->total_time = path->mpp->marginal_path_err_sample_time;
	p->err_rate_threshold = path->mpp->marginal_path_err_rate_threshold;

	if (setup_directio_ctx(p))
		goto free_ioerr_path;
	pthread_mutex_lock(&paths->mutex);
	if (!vector_alloc_slot(paths->pathvec))
		goto unlock_destroy;
	vector_set_slot(paths->pathvec, p);
	pthread_mutex_unlock(&paths->mutex);

	io_err_stat_log(2, "%s: enqueue path %s to check",
			path->mpp->alias, path->dev);
	return 0;

unlock_destroy:
	pthread_mutex_unlock(&paths->mutex);
	destroy_directio_ctx(p);
free_ioerr_path:
	free_io_err_stat_path(p);

	return 1;
}

int io_err_stat_handle_pathfail(struct path *path)
{
	struct timespec curr_time;

	if (uatomic_read(&io_err_thread_running) == 0)
		return 1;

	if (path->io_err_disable_reinstate) {
		io_err_stat_log(3, "%s: reinstate is already disabled",
				path->dev);
		return 1;
	}
	if (path->io_err_pathfail_cnt < 0)
		return 1;

	if (!path->mpp)
		return 1;
	if (path->mpp->marginal_path_double_failed_time <= 0 ||
		path->mpp->marginal_path_err_sample_time <= 0 ||
		path->mpp->marginal_path_err_recheck_gap_time <= 0 ||
		path->mpp->marginal_path_err_rate_threshold < 0) {
		io_err_stat_log(4, "%s: parameter not set", path->mpp->alias);
		return 1;
	}
	if (path->mpp->marginal_path_err_sample_time < (2 * IOTIMEOUT_SEC)) {
		io_err_stat_log(2, "%s: marginal_path_err_sample_time should not less than %d",
				path->mpp->alias, 2 * IOTIMEOUT_SEC);
		return 1;
	}
	/*
	 * The test should only be started for paths that have failed
	 * repeatedly in a certain time frame, so that we have reason
	 * to assume they're flaky. Without bother the admin to configure
	 * the repeated count threshold and time frame, we assume a path
	 * which fails at least twice within 60 seconds is flaky.
	 */
	if (clock_gettime(CLOCK_MONOTONIC, &curr_time) != 0)
		return 1;
	if (path->io_err_pathfail_cnt == 0) {
		path->io_err_pathfail_cnt++;
		path->io_err_pathfail_starttime = curr_time.tv_sec;
		io_err_stat_log(5, "%s: start path flakiness pre-checking",
				path->dev);
		return 0;
	}
	if ((curr_time.tv_sec - path->io_err_pathfail_starttime) >
			path->mpp->marginal_path_double_failed_time) {
		path->io_err_pathfail_cnt = 0;
		path->io_err_pathfail_starttime = curr_time.tv_sec;
		io_err_stat_log(5, "%s: restart path flakiness pre-checking",
				path->dev);
	}
	path->io_err_pathfail_cnt++;
	if (path->io_err_pathfail_cnt >= FLAKY_PATHFAIL_THRESHOLD) {
		path->io_err_disable_reinstate = 1;
		path->io_err_pathfail_cnt = PATH_IO_ERR_WAITING_TO_CHECK;
		/* enqueue path as soon as it comes up */
		path->io_err_dis_reinstate_time = 0;
		if (path->state != PATH_DOWN) {
			struct config *conf;
			int oldstate = path->state;
			int checkint;

			conf = get_multipath_config();
			checkint = conf->checkint;
			put_multipath_config(conf);
			io_err_stat_log(2, "%s: mark as failed", path->dev);
			path->mpp->stat_path_failures++;
			path->state = PATH_DOWN;
			path->dmstate = PSTATE_FAILED;
			if (oldstate == PATH_UP || oldstate == PATH_GHOST)
				update_queue_mode_del_path(path->mpp);
			if (path->tick > checkint)
				path->tick = checkint;
		}
	}

	return 0;
}

int need_io_err_check(struct path *pp)
{
	struct timespec curr_time;
	int r;

	if (uatomic_read(&io_err_thread_running) == 0)
		return 0;
	if (pp->mpp->nr_active <= 0) {
		io_err_stat_log(2, "%s: recover path early", pp->dev);
		goto recover;
	}
	if (pp->io_err_pathfail_cnt != PATH_IO_ERR_WAITING_TO_CHECK)
		return 1;
	if (clock_gettime(CLOCK_MONOTONIC, &curr_time) != 0 ||
	    (curr_time.tv_sec - pp->io_err_dis_reinstate_time) >
			pp->mpp->marginal_path_err_recheck_gap_time) {
		io_err_stat_log(4, "%s: reschedule checking after %d seconds",
				pp->dev,
				pp->mpp->marginal_path_err_recheck_gap_time);
		r = enqueue_io_err_stat_by_path(pp);
		/*
		 * Enqueue fails because of internal error.
		 * In this case , we recover this path
		 * Or else,  return 1 to set path state to PATH_SHAKY
		 */
		if (r == 1) {
			io_err_stat_log(3, "%s: enqueue fails, to recover",
					pp->dev);
			goto recover;
		} else
			pp->io_err_pathfail_cnt = PATH_IO_ERR_IN_CHECKING;
	}

	return 1;

recover:
	pp->io_err_pathfail_cnt = 0;
	pp->io_err_disable_reinstate = 0;
	return 0;
}

static int delete_io_err_stat_by_addr(struct io_err_stat_path *p)
{
	int i;

	i = find_slot(paths->pathvec, p);
	if (i != -1)
		vector_del_slot(paths->pathvec, i);

	destroy_directio_ctx(p);
	free_io_err_stat_path(p);

	return 0;
}

static void account_async_io_state(struct io_err_stat_path *pp, int rc)
{
	switch (rc) {
	case PATH_DOWN:
	case PATH_TIMEOUT:
		pp->io_err_nr++;
		break;
	case PATH_UNCHECKED:
	case PATH_UP:
	case PATH_PENDING:
		break;
	default:
		break;
	}
}

static int poll_io_err_stat(struct vectors *vecs, struct io_err_stat_path *pp)
{
	struct timespec currtime, difftime;
	struct path *path;
	double err_rate;

	if (clock_gettime(CLOCK_MONOTONIC, &currtime) != 0)
		return 1;
	timespecsub(&currtime, &pp->start_time, &difftime);
	if (difftime.tv_sec < pp->total_time)
		return 0;

	io_err_stat_log(4, "%s: check end", pp->devname);

	err_rate = pp->io_nr == 0 ? 0 : (pp->io_err_nr * 1000.0f) / pp->io_nr;
	io_err_stat_log(3, "%s: IO error rate (%.1f/1000)",
			pp->devname, err_rate);
	pthread_cleanup_push(cleanup_lock, &vecs->lock);
	lock(&vecs->lock);
	pthread_testcancel();
	path = find_path_by_dev(vecs->pathvec, pp->devname);
	if (!path) {
		io_err_stat_log(4, "path %s not found'", pp->devname);
	} else if (err_rate <= pp->err_rate_threshold) {
		path->io_err_pathfail_cnt = 0;
		path->io_err_disable_reinstate = 0;
		io_err_stat_log(3, "%s: (%d/%d) good to enable reinstating",
				pp->devname, pp->io_err_nr, pp->io_nr);
		/*
		 * schedule path check as soon as possible to
		 * update path state. Do NOT reinstate dm path here
		 */
		path->tick = 1;

	} else if (path->mpp && path->mpp->nr_active > 0) {
		io_err_stat_log(3, "%s: keep failing the dm path %s",
				path->mpp->alias, path->dev);
		path->io_err_pathfail_cnt = PATH_IO_ERR_WAITING_TO_CHECK;
		path->io_err_disable_reinstate = 1;
		path->io_err_dis_reinstate_time = currtime.tv_sec;
		io_err_stat_log(3, "%s: disable reinstating of %s",
				path->mpp->alias, path->dev);
	} else {
		path->io_err_pathfail_cnt = 0;
		path->io_err_disable_reinstate = 0;
		io_err_stat_log(3, "%s: there is orphan path, enable reinstating",
				pp->devname);
	}
	lock_cleanup_pop(vecs->lock);

	delete_io_err_stat_by_addr(pp);

	return 0;
}

static int send_each_async_io(struct dio_ctx *ct, int fd, char *dev)
{
	int rc = -1;

	if (ct->io_starttime.tv_nsec == 0 &&
			ct->io_starttime.tv_sec == 0) {
		struct iocb *ios[1] = { &ct->io };

		if (clock_gettime(CLOCK_MONOTONIC, &ct->io_starttime) != 0) {
			ct->io_starttime.tv_sec = 0;
			ct->io_starttime.tv_nsec = 0;
			return rc;
		}
		io_prep_pread(&ct->io, fd, ct->buf, ct->blksize, 0);
		if (io_submit(ioctx, 1, ios) != 1) {
			io_err_stat_log(5, "%s: io_submit error %i",
					dev, errno);
			return rc;
		}
		rc = 0;
	}

	return rc;
}

static void send_batch_async_ios(struct io_err_stat_path *pp)
{
	int i;
	struct dio_ctx *ct;
	struct timespec currtime, difftime;

	if (clock_gettime(CLOCK_MONOTONIC, &currtime) != 0)
		return;
	/*
	 * Give a free time for all IO to complete or timeout
	 */
	if (pp->start_time.tv_sec != 0) {
		timespecsub(&currtime, &pp->start_time, &difftime);
		if (difftime.tv_sec + IOTIMEOUT_SEC >= pp->total_time)
			return;
	}

	for (i = 0; i < CONCUR_NR_EVENT; i++) {
		ct = pp->dio_ctx_array + i;
		if (!send_each_async_io(ct, pp->fd, pp->devname))
			pp->io_nr++;
	}
	if (pp->start_time.tv_sec == 0 && pp->start_time.tv_nsec == 0 &&
		clock_gettime(CLOCK_MONOTONIC, &pp->start_time)) {
		pp->start_time.tv_sec = 0;
		pp->start_time.tv_nsec = 0;
	}
}

static int try_to_cancel_timeout_io(struct dio_ctx *ct, struct timespec *t,
		char *dev)
{
	struct timespec	difftime;
	struct io_event	event;
	int		rc = PATH_UNCHECKED;
	int		r;

	if (ct->io_starttime.tv_sec == 0)
		return rc;
	timespecsub(t, &ct->io_starttime, &difftime);
	if (difftime.tv_sec > IOTIMEOUT_SEC) {
		struct iocb *ios[1] = { &ct->io };

		io_err_stat_log(5, "%s: abort check on timeout", dev);
		r = io_cancel(ioctx, ios[0], &event);
		if (r)
			io_err_stat_log(5, "%s: io_cancel error %i",
					dev, errno);
		ct->io_starttime.tv_sec = 0;
		ct->io_starttime.tv_nsec = 0;
		rc = PATH_TIMEOUT;
	} else {
		rc = PATH_PENDING;
	}

	return rc;
}

static void poll_async_io_timeout(void)
{
	struct io_err_stat_path *pp;
	struct timespec curr_time;
	int		rc = PATH_UNCHECKED;
	int		i, j;

	if (clock_gettime(CLOCK_MONOTONIC, &curr_time) != 0)
		return;
	vector_foreach_slot(paths->pathvec, pp, i) {
		for (j = 0; j < CONCUR_NR_EVENT; j++) {
			rc = try_to_cancel_timeout_io(pp->dio_ctx_array + j,
					&curr_time, pp->devname);
			account_async_io_state(pp, rc);
		}
	}
}

static void cancel_inflight_io(struct io_err_stat_path *pp)
{
	struct io_event event;
	int i, r;

	for (i = 0; i < CONCUR_NR_EVENT; i++) {
		struct dio_ctx *ct = pp->dio_ctx_array + i;
		struct iocb *ios[1] = { &ct->io };

		if (ct->io_starttime.tv_sec == 0
				&& ct->io_starttime.tv_nsec == 0)
			continue;
		io_err_stat_log(5, "%s: abort infligh io",
				pp->devname);
		r = io_cancel(ioctx, ios[0], &event);
		if (r)
			io_err_stat_log(5, "%s: io_cancel error %d, %i",
					pp->devname, r, errno);
		ct->io_starttime.tv_sec = 0;
		ct->io_starttime.tv_nsec = 0;
	}
}

static inline int handle_done_dio_ctx(struct dio_ctx *ct, struct io_event *ev)
{
	ct->io_starttime.tv_sec = 0;
	ct->io_starttime.tv_nsec = 0;
	return (ev->res == ct->blksize) ? PATH_UP : PATH_DOWN;
}

static void handle_async_io_done_event(struct io_event *io_evt)
{
	struct io_err_stat_path *pp;
	struct dio_ctx *ct;
	int rc = PATH_UNCHECKED;
	int i, j;

	vector_foreach_slot(paths->pathvec, pp, i) {
		for (j = 0; j < CONCUR_NR_EVENT; j++) {
			ct = pp->dio_ctx_array + j;
			if (&ct->io == io_evt->obj) {
				rc = handle_done_dio_ctx(ct, io_evt);
				account_async_io_state(pp, rc);
				return;
			}
		}
	}
}

static void process_async_ios_event(int timeout_nsecs, char *dev)
{
	struct io_event events[CONCUR_NR_EVENT];
	int		i, n;
	struct timespec	timeout = { .tv_nsec = timeout_nsecs };

	errno = 0;
	n = io_getevents(ioctx, 1L, CONCUR_NR_EVENT, events, &timeout);
	if (n < 0) {
		io_err_stat_log(3, "%s: async io events returned %d (errno=%s)",
				dev, n, strerror(errno));
	} else {
		for (i = 0; i < n; i++)
			handle_async_io_done_event(&events[i]);
	}
}

static void service_paths(void)
{
	struct io_err_stat_path *pp;
	int i;

	pthread_mutex_lock(&paths->mutex);
	vector_foreach_slot(paths->pathvec, pp, i) {
		send_batch_async_ios(pp);
		process_async_ios_event(TIMEOUT_NO_IO_NSEC, pp->devname);
		poll_async_io_timeout();
		poll_io_err_stat(vecs, pp);
	}
	pthread_mutex_unlock(&paths->mutex);
}

static void cleanup_unlock(void *arg)
{
	pthread_mutex_unlock((pthread_mutex_t*) arg);
}

static void cleanup_exited(void *arg)
{
	uatomic_set(&io_err_thread_running, 0);
}

static void *io_err_stat_loop(void *data)
{
	sigset_t set;

	vecs = (struct vectors *)data;
	pthread_cleanup_push(rcu_unregister, NULL);
	rcu_register_thread();

	pthread_cleanup_push(cleanup_exited, NULL);

	sigfillset(&set);
	sigdelset(&set, SIGUSR2);

	mlockall(MCL_CURRENT | MCL_FUTURE);

	pthread_mutex_lock(&io_err_thread_lock);
	uatomic_set(&io_err_thread_running, 1);
	pthread_cond_broadcast(&io_err_thread_cond);
	pthread_mutex_unlock(&io_err_thread_lock);

	while (1) {
		struct timespec ts;

		service_paths();

		ts.tv_sec = 0;
		ts.tv_nsec = 100 * 1000 * 1000;
		/*
		 * pselect() with no fds, a timeout, and a sigmask:
		 * sleep for 100ms and react on SIGUSR2.
		 */
		pselect(1, NULL, NULL, NULL, &ts, &set);
	}

	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
	return NULL;
}

int start_io_err_stat_thread(void *data)
{
	int ret;

	if (uatomic_read(&io_err_thread_running) == 1)
		return 0;

	if (io_setup(CONCUR_NR_EVENT, &ioctx) != 0) {
		io_err_stat_log(4, "io_setup failed");
		return 1;
	}
	paths = alloc_pathvec();
	if (!paths)
		goto destroy_ctx;

	pthread_mutex_lock(&io_err_thread_lock);
	pthread_cleanup_push(cleanup_unlock, &io_err_thread_lock);

	ret = pthread_create(&io_err_stat_thr, &io_err_stat_attr,
			     io_err_stat_loop, data);

	while (!ret && !uatomic_read(&io_err_thread_running) &&
	       pthread_cond_wait(&io_err_thread_cond,
				 &io_err_thread_lock) == 0);

	pthread_cleanup_pop(1);

	if (ret) {
		io_err_stat_log(0, "cannot create io_error statistic thread");
		goto out_free;
	}

	io_err_stat_log(2, "io_error statistic thread started");
	return 0;

out_free:
	free_io_err_pathvec(paths);
destroy_ctx:
	io_destroy(ioctx);
	io_err_stat_log(0, "failed to start io_error statistic thread");
	return 1;
}

void stop_io_err_stat_thread(void)
{
	if (io_err_stat_thr == (pthread_t)0)
		return;

	if (uatomic_read(&io_err_thread_running) == 1)
		pthread_cancel(io_err_stat_thr);

	pthread_join(io_err_stat_thr, NULL);
	free_io_err_pathvec(paths);
	io_destroy(ioctx);
}
