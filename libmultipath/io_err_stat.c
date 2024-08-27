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
#include <sys/mman.h>
#include <sys/select.h>

#include "vector.h"
#include "checkers.h"
#include "config.h"
#include "structs.h"
#include "structs_vec.h"
#include "devmapper.h"
#include "debug.h"
#include "lock.h"
#include "time-util.h"
#include "io_err_stat.h"
#include "util.h"

#define TIMEOUT_NO_IO_NSEC		10000000 /*10ms = 10000000ns*/
#define FLAKY_PATHFAIL_THRESHOLD	2
#define CONCUR_NR_EVENT			32
#define NR_IOSTAT_PATHS			32

#define PATH_IO_ERR_IN_CHECKING		-1
#define PATH_IO_ERR_WAITING_TO_CHECK	-2

#define io_err_stat_log(prio, fmt, args...) \
	condlog(prio, "io error statistic: " fmt, ##args)

struct dio_ctx {
	struct timespec	io_starttime;
	unsigned int	blksize;
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

static pthread_t	io_err_stat_thr;

static pthread_mutex_t io_err_thread_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t io_err_thread_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t io_err_pathvec_lock = PTHREAD_MUTEX_INITIALIZER;
static int io_err_thread_running = 0;

static vector io_err_pathvec;
struct vectors *vecs;
io_context_t	ioctx;

static void cancel_inflight_io(struct io_err_stat_path *pp);

static void rcu_unregister(__attribute__((unused)) void *param)
{
	rcu_unregister_thread();
}

static struct io_err_stat_path *find_err_path_by_dev(vector pathvec, char *dev)
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

static int deinit_each_dio_ctx(struct dio_ctx *ct)
{
	if (!ct->buf)
		return 0;
	if (ct->io_starttime.tv_sec != 0 || ct->io_starttime.tv_nsec != 0)
		return 1;
	free(ct->buf);
	return 0;
}

static int setup_directio_ctx(struct io_err_stat_path *p)
{
	unsigned long pgsize = getpagesize();
	char fpath[PATH_MAX];
	unsigned int blksize = 0;
	int i;

	if (snprintf(fpath, PATH_MAX, "/dev/%s", p->devname) >= PATH_MAX)
		return 1;
	if (p->fd < 0)
		p->fd = open(fpath, O_RDONLY | O_DIRECT);
	if (p->fd < 0)
		return 1;

	p->dio_ctx_array = calloc(1, sizeof(struct dio_ctx) * CONCUR_NR_EVENT);
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
	free(p->dio_ctx_array);
	p->dio_ctx_array = NULL;
fail_close:
	close(p->fd);

	return 1;
}

static void free_io_err_stat_path(struct io_err_stat_path *p)
{
	int i;
	int inflight = 0;

	if (!p)
		return;
	if (!p->dio_ctx_array)
		goto free_path;

	for (i = 0; i < CONCUR_NR_EVENT; i++)
		inflight += deinit_each_dio_ctx(p->dio_ctx_array + i);

	if (!inflight)
		free(p->dio_ctx_array);
	else
		io_err_stat_log(2, "%s: can't free aio space of %s, %d IOs in flight",
				__func__, p->devname, inflight);

	if (p->fd > 0)
		close(p->fd);
free_path:
	free(p);
}

static struct io_err_stat_path *alloc_io_err_stat_path(void)
{
	struct io_err_stat_path *p;

	p = (struct io_err_stat_path *)calloc(1, sizeof(*p));
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

static void free_io_err_pathvec(void)
{
	struct io_err_stat_path *path;
	int i;

	pthread_mutex_lock(&io_err_pathvec_lock);
	pthread_cleanup_push(cleanup_mutex, &io_err_pathvec_lock);
	if (!io_err_pathvec)
		goto out;

	/* io_cancel() is a noop, but maybe in the future it won't be */
	vector_foreach_slot(io_err_pathvec, path, i) {
		if (path && path->dio_ctx_array)
			cancel_inflight_io(path);
	}

	/* This blocks until all I/O is finished */
	io_destroy(ioctx);
	vector_foreach_slot(io_err_pathvec, path, i)
		free_io_err_stat_path(path);
	vector_free(io_err_pathvec);
	io_err_pathvec = NULL;
out:
	pthread_cleanup_pop(1);
}

/*
 * return value
 * 0: enqueue OK
 * 1: fails because of internal error
 */
static int enqueue_io_err_stat_by_path(struct path *path)
{
	struct io_err_stat_path *p;

	pthread_mutex_lock(&io_err_pathvec_lock);
	p = find_err_path_by_dev(io_err_pathvec, path->dev);
	if (p) {
		pthread_mutex_unlock(&io_err_pathvec_lock);
		return 0;
	}
	pthread_mutex_unlock(&io_err_pathvec_lock);

	p = alloc_io_err_stat_path();
	if (!p)
		return 1;

	memcpy(p->devname, path->dev, sizeof(p->devname));
	p->total_time = path->mpp->marginal_path_err_sample_time;
	p->err_rate_threshold = path->mpp->marginal_path_err_rate_threshold;

	if (setup_directio_ctx(p))
		goto free_ioerr_path;
	pthread_mutex_lock(&io_err_pathvec_lock);
	if (!vector_alloc_slot(io_err_pathvec))
		goto unlock_pathvec;
	vector_set_slot(io_err_pathvec, p);
	pthread_mutex_unlock(&io_err_pathvec_lock);

	io_err_stat_log(3, "%s: enqueue path %s to check",
			path->mpp->alias, path->dev);
	return 0;

unlock_pathvec:
	pthread_mutex_unlock(&io_err_pathvec_lock);
free_ioerr_path:
	free_io_err_stat_path(p);

	return 1;
}

int io_err_stat_handle_pathfail(struct path *path)
{
	struct timespec curr_time;

	if (uatomic_read(&io_err_thread_running) == 0)
		return 0;

	if (path->io_err_disable_reinstate) {
		io_err_stat_log(3, "%s: reinstate is already disabled",
				path->dev);
		return 0;
	}
	if (path->io_err_pathfail_cnt < 0)
		return 0;

	if (!path->mpp)
		return 0;

	if (!marginal_path_check_enabled(path->mpp))
		return 0;

	/*
	 * The test should only be started for paths that have failed
	 * repeatedly in a certain time frame, so that we have reason
	 * to assume they're flaky. Without bother the admin to configure
	 * the repeated count threshold and time frame, we assume a path
	 * which fails at least twice within 60 seconds is flaky.
	 */
	get_monotonic_time(&curr_time);
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
			unsigned int checkint;

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
	if (count_active_paths(pp->mpp) <= 0) {
		io_err_stat_log(2, "%s: no paths. recovering early", pp->dev);
		goto recover;
	}
	if (pp->io_err_pathfail_cnt != PATH_IO_ERR_WAITING_TO_CHECK)
		return 1;
	get_monotonic_time(&curr_time);
	if ((curr_time.tv_sec - pp->io_err_dis_reinstate_time) >
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
			io_err_stat_log(2, "%s: enqueue failed. recovering early", pp->dev);
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

static int io_err_stat_time_up(struct io_err_stat_path *pp)
{
	struct timespec currtime, difftime;

	get_monotonic_time(&currtime);
	timespecsub(&currtime, &pp->start_time, &difftime);
	if (difftime.tv_sec < pp->total_time)
		return 0;
	return 1;
}

static void end_io_err_stat(struct io_err_stat_path *pp)
{
	struct timespec currtime;
	struct path *path;
	double err_rate;

	get_monotonic_time(&currtime);

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

	} else if (path->mpp && count_active_paths(path->mpp) > 0) {
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
}

static int send_each_async_io(struct dio_ctx *ct, int fd, char *dev)
{
	int rc;

	if (ct->io_starttime.tv_nsec == 0 &&
			ct->io_starttime.tv_sec == 0) {
		struct iocb *ios[1] = { &ct->io };

		get_monotonic_time(&ct->io_starttime);
		io_prep_pread(&ct->io, fd, ct->buf, ct->blksize, 0);
		if ((rc = io_submit(ioctx, 1, ios)) != 1) {
			io_err_stat_log(2, "%s: io_submit error %s",
					dev, strerror(-rc));
			return -1;
		}
		return 0;
	}

	return -1;
}

static void send_batch_async_ios(struct io_err_stat_path *pp)
{
	int i;
	struct dio_ctx *ct;
	struct timespec currtime, difftime;

	get_monotonic_time(&currtime);
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
	if (pp->start_time.tv_sec == 0 && pp->start_time.tv_nsec == 0)
		get_monotonic_time(&pp->start_time);
}

static int try_to_cancel_timeout_io(struct dio_ctx *ct, struct timespec *t,
		char *dev)
{
	struct timespec	difftime;
	struct io_event	event;
	int		rc = PATH_UNCHECKED;
	int		r;

	if (ct->io_starttime.tv_sec == 0 && ct->io_starttime.tv_nsec == 0)
		return rc;
	timespecsub(t, &ct->io_starttime, &difftime);
	if (difftime.tv_sec > IOTIMEOUT_SEC) {
		struct iocb *ios[1] = { &ct->io };

		io_err_stat_log(5, "%s: abort check on timeout", dev);
		r = io_cancel(ioctx, ios[0], &event);
		if (r)
			io_err_stat_log(5, "%s: io_cancel error %s",
					dev, strerror(-r));
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

	get_monotonic_time(&curr_time);
	vector_foreach_slot(io_err_pathvec, pp, i) {
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
	int i;

	for (i = 0; i < CONCUR_NR_EVENT; i++) {
		struct dio_ctx *ct = pp->dio_ctx_array + i;
		struct iocb *ios[1] = { &ct->io };

		if (ct->io_starttime.tv_sec == 0
				&& ct->io_starttime.tv_nsec == 0)
			continue;
		io_err_stat_log(5, "%s: abort infligh io",
				pp->devname);
		io_cancel(ioctx, ios[0], &event);
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

	vector_foreach_slot(io_err_pathvec, pp, i) {
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

	pthread_testcancel();
	n = io_getevents(ioctx, 1L, CONCUR_NR_EVENT, events, &timeout);
	if (n < 0) {
		io_err_stat_log(3, "%s: io_getevents returned %s",
				dev, strerror(-n));
	} else {
		for (i = 0; i < n; i++)
			handle_async_io_done_event(&events[i]);
	}
}

static void service_paths(void)
{
	struct vector_s _pathvec = { .allocated = 0 };
	/* avoid gcc warnings that &_pathvec will never be NULL in vector ops */
	struct vector_s * const tmp_pathvec = &_pathvec;
	struct io_err_stat_path *pp;
	int i;

	pthread_mutex_lock(&io_err_pathvec_lock);
	pthread_cleanup_push(cleanup_mutex, &io_err_pathvec_lock);
	vector_foreach_slot(io_err_pathvec, pp, i) {
		send_batch_async_ios(pp);
		process_async_ios_event(TIMEOUT_NO_IO_NSEC, pp->devname);
		poll_async_io_timeout();
		if (io_err_stat_time_up(pp)) {
			if (!vector_alloc_slot(tmp_pathvec))
				continue;
			vector_del_slot(io_err_pathvec, i--);
			vector_set_slot(tmp_pathvec, pp);
		}
	}
	pthread_cleanup_pop(1);
	vector_foreach_slot_backwards(tmp_pathvec, pp, i) {
		end_io_err_stat(pp);
		vector_del_slot(tmp_pathvec, i);
		free_io_err_stat_path(pp);
	}
	vector_reset(tmp_pathvec);
}

static void cleanup_exited(__attribute__((unused)) void *arg)
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
	pthread_attr_t io_err_stat_attr;

	if (uatomic_read(&io_err_thread_running) == 1)
		return 0;

	if ((ret = io_setup(NR_IOSTAT_PATHS * CONCUR_NR_EVENT, &ioctx)) != 0) {
		io_err_stat_log(1, "io_setup failed: %s, increase /proc/sys/fs/aio-nr ?",
				strerror(-ret));
		return 1;
	}

	pthread_mutex_lock(&io_err_pathvec_lock);
	io_err_pathvec = vector_alloc();
	if (!io_err_pathvec) {
		pthread_mutex_unlock(&io_err_pathvec_lock);
		goto destroy_ctx;
	}
	pthread_mutex_unlock(&io_err_pathvec_lock);

	setup_thread_attr(&io_err_stat_attr, 32 * 1024, 0);
	pthread_mutex_lock(&io_err_thread_lock);
	pthread_cleanup_push(cleanup_mutex, &io_err_thread_lock);

	ret = pthread_create(&io_err_stat_thr, &io_err_stat_attr,
			     io_err_stat_loop, data);

	while (!ret && !uatomic_read(&io_err_thread_running) &&
	       pthread_cond_wait(&io_err_thread_cond,
				 &io_err_thread_lock) == 0);

	pthread_cleanup_pop(1);
	pthread_attr_destroy(&io_err_stat_attr);

	if (ret) {
		io_err_stat_log(0, "cannot create io_error statistic thread");
		goto out_free;
	}

	io_err_stat_log(2, "io_error statistic thread started");
	return 0;

out_free:
	pthread_mutex_lock(&io_err_pathvec_lock);
	vector_free(io_err_pathvec);
	io_err_pathvec = NULL;
	pthread_mutex_unlock(&io_err_pathvec_lock);
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
	free_io_err_pathvec();
}
