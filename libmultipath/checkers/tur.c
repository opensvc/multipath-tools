/*
 * Some code borrowed from sg-utils.
 *
 * Copyright (c) 2004 Christophe Varoqui
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <errno.h>
#include <sys/time.h>
#include <pthread.h>
#include <urcu.h>
#include <urcu/uatomic.h>

#include "checkers.h"

#include "../libmultipath/debug.h"
#include "../libmultipath/sg_include.h"
#include "../libmultipath/util.h"
#include "../libmultipath/time-util.h"
#include "../libmultipath/util.h"

#define TUR_CMD_LEN 6
#define HEAVY_CHECK_COUNT       10

enum {
	MSG_TUR_RUNNING = CHECKER_FIRST_MSGID,
	MSG_TUR_TIMEOUT,
	MSG_TUR_FAILED,
};

#define _IDX(x) (MSG_ ## x - CHECKER_FIRST_MSGID)
const char *libcheck_msgtable[] = {
	[_IDX(TUR_RUNNING)] = " still running",
	[_IDX(TUR_TIMEOUT)] = " timed out",
	[_IDX(TUR_FAILED)] = " failed to initialize",
	NULL,
};

struct tur_checker_context {
	dev_t devt;
	int state;
	int running; /* uatomic access only */
	int fd;
	unsigned int timeout;
	time_t time;
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t active;
	int holders; /* uatomic access only */
	int msgid;
};

int libcheck_init (struct checker * c)
{
	struct tur_checker_context *ct;
	struct stat sb;

	ct = malloc(sizeof(struct tur_checker_context));
	if (!ct)
		return 1;
	memset(ct, 0, sizeof(struct tur_checker_context));

	ct->state = PATH_UNCHECKED;
	ct->fd = -1;
	uatomic_set(&ct->holders, 1);
	pthread_cond_init_mono(&ct->active);
	pthread_mutex_init(&ct->lock, NULL);
	if (fstat(c->fd, &sb) == 0)
		ct->devt = sb.st_rdev;
	c->context = ct;

	return 0;
}

static void cleanup_context(struct tur_checker_context *ct)
{
	pthread_mutex_destroy(&ct->lock);
	pthread_cond_destroy(&ct->active);
	free(ct);
}

void libcheck_free (struct checker * c)
{
	if (c->context) {
		struct tur_checker_context *ct = c->context;
		int holders;
		int running;

		running = uatomic_xchg(&ct->running, 0);
		if (running)
			pthread_cancel(ct->thread);
		ct->thread = 0;
		holders = uatomic_sub_return(&ct->holders, 1);
		if (!holders)
			cleanup_context(ct);
		c->context = NULL;
	}
	return;
}

static int
tur_check(int fd, unsigned int timeout, short *msgid)
{
	struct sg_io_hdr io_hdr;
	unsigned char turCmdBlk[TUR_CMD_LEN] = { 0x00, 0, 0, 0, 0, 0 };
	unsigned char sense_buffer[32];
	int retry_tur = 5;

retry:
	memset(&io_hdr, 0, sizeof (struct sg_io_hdr));
	memset(&sense_buffer, 0, 32);
	io_hdr.interface_id = 'S';
	io_hdr.cmd_len = sizeof (turCmdBlk);
	io_hdr.mx_sb_len = sizeof (sense_buffer);
	io_hdr.dxfer_direction = SG_DXFER_NONE;
	io_hdr.cmdp = turCmdBlk;
	io_hdr.sbp = sense_buffer;
	io_hdr.timeout = timeout * 1000;
	io_hdr.pack_id = 0;
	if (ioctl(fd, SG_IO, &io_hdr) < 0) {
		if (errno == ENOTTY) {
			*msgid = CHECKER_MSGID_UNSUPPORTED;
			return PATH_WILD;
		}
		*msgid = CHECKER_MSGID_DOWN;
		return PATH_DOWN;
	}
	if ((io_hdr.status & 0x7e) == 0x18) {
		/*
		 * SCSI-3 arrays might return
		 * reservation conflict on TUR
		 */
		*msgid = CHECKER_MSGID_UP;
		return PATH_UP;
	}
	if (io_hdr.info & SG_INFO_OK_MASK) {
		int key = 0, asc, ascq;

		switch (io_hdr.host_status) {
		case DID_OK:
		case DID_NO_CONNECT:
		case DID_BAD_TARGET:
		case DID_ABORT:
		case DID_TRANSPORT_FAILFAST:
			break;
		default:
			/* Driver error, retry */
			if (--retry_tur)
				goto retry;
			break;
		}
		if (io_hdr.sb_len_wr > 3) {
			if (io_hdr.sbp[0] == 0x72 || io_hdr.sbp[0] == 0x73) {
				key = io_hdr.sbp[1] & 0x0f;
				asc = io_hdr.sbp[2];
				ascq = io_hdr.sbp[3];
			} else if (io_hdr.sb_len_wr > 13 &&
				   ((io_hdr.sbp[0] & 0x7f) == 0x70 ||
				    (io_hdr.sbp[0] & 0x7f) == 0x71)) {
				key = io_hdr.sbp[2] & 0x0f;
				asc = io_hdr.sbp[12];
				ascq = io_hdr.sbp[13];
			}
		}
		if (key == 0x6) {
			/* Unit Attention, retry */
			if (--retry_tur)
				goto retry;
		}
		else if (key == 0x2) {
			/* Not Ready */
			/* Note: Other ALUA states are either UP or DOWN */
			if( asc == 0x04 && ascq == 0x0b){
				/*
				 * LOGICAL UNIT NOT ACCESSIBLE,
				 * TARGET PORT IN STANDBY STATE
				 */
				*msgid = CHECKER_MSGID_GHOST;
				return PATH_GHOST;
			}
		}
		*msgid = CHECKER_MSGID_DOWN;
		return PATH_DOWN;
	}
	*msgid = CHECKER_MSGID_UP;
	return PATH_UP;
}

#define tur_thread_cleanup_push(ct) pthread_cleanup_push(cleanup_func, ct)
#define tur_thread_cleanup_pop(ct) pthread_cleanup_pop(1)

static void cleanup_func(void *data)
{
	int holders;
	struct tur_checker_context *ct = data;

	holders = uatomic_sub_return(&ct->holders, 1);
	if (!holders)
		cleanup_context(ct);
	rcu_unregister_thread();
}

/*
 * Test code for "zombie tur thread" handling.
 * Compile e.g. with CFLAGS=-DTUR_TEST_MAJOR=8
 * Additional parameters can be configure with the macros below.
 *
 * Everty nth started TUR thread will hang in non-cancellable state
 * for given number of seconds, for device given by major/minor.
 */
#ifdef TUR_TEST_MAJOR

#ifndef TUR_TEST_MINOR
#define TUR_TEST_MINOR 0
#endif
#ifndef TUR_SLEEP_INTERVAL
#define TUR_SLEEP_INTERVAL 3
#endif
#ifndef TUR_SLEEP_SECS
#define TUR_SLEEP_SECS 60
#endif

static void tur_deep_sleep(const struct tur_checker_context *ct)
{
	static int sleep_cnt;
	const struct timespec ts = { .tv_sec = TUR_SLEEP_SECS, .tv_nsec = 0 };
	int oldstate;

	if (ct->devt != makedev(TUR_TEST_MAJOR, TUR_TEST_MINOR) ||
	    ++sleep_cnt % TUR_SLEEP_INTERVAL != 0)
		return;

	condlog(1, "tur thread going to sleep for %ld seconds", ts.tv_sec);
	if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate) != 0)
		condlog(0, "pthread_setcancelstate: %m");
	if (nanosleep(&ts, NULL) != 0)
		condlog(0, "nanosleep: %m");
	condlog(1, "tur zombie thread woke up");
	if (pthread_setcancelstate(oldstate, NULL) != 0)
		condlog(0, "pthread_setcancelstate (2): %m");
	pthread_testcancel();
}
#else
#define tur_deep_sleep(x) do {} while (0)
#endif /* TUR_TEST_MAJOR */

static void *tur_thread(void *ctx)
{
	struct tur_checker_context *ct = ctx;
	int state, running;
	short msgid;

	/* This thread can be canceled, so setup clean up */
	tur_thread_cleanup_push(ct);
	rcu_register_thread();

	condlog(4, "%d:%d : tur checker starting up", major(ct->devt),
		minor(ct->devt));

	tur_deep_sleep(ct);
	state = tur_check(ct->fd, ct->timeout, &msgid);
	pthread_testcancel();

	/* TUR checker done */
	pthread_mutex_lock(&ct->lock);
	ct->state = state;
	ct->msgid = msgid;
	pthread_cond_signal(&ct->active);
	pthread_mutex_unlock(&ct->lock);

	condlog(4, "%d:%d : tur checker finished, state %s", major(ct->devt),
		minor(ct->devt), checker_state_name(state));

	running = uatomic_xchg(&ct->running, 0);
	if (!running)
		pause();

	tur_thread_cleanup_pop(ct);

	return ((void *)0);
}


static void tur_timeout(struct timespec *tsp)
{
	get_monotonic_time(tsp);
	tsp->tv_nsec += 1000 * 1000; /* 1 millisecond */
	normalize_timespec(tsp);
}

static void tur_set_async_timeout(struct checker *c)
{
	struct tur_checker_context *ct = c->context;
	struct timespec now;

	get_monotonic_time(&now);
	ct->time = now.tv_sec + c->timeout;
}

static int tur_check_async_timeout(struct checker *c)
{
	struct tur_checker_context *ct = c->context;
	struct timespec now;

	get_monotonic_time(&now);
	return (now.tv_sec > ct->time);
}

int libcheck_check(struct checker * c)
{
	struct tur_checker_context *ct = c->context;
	struct timespec tsp;
	pthread_attr_t attr;
	int tur_status, r;

	if (!ct)
		return PATH_UNCHECKED;

	if (checker_is_sync(c))
		return tur_check(c->fd, c->timeout, &c->msgid);

	/*
	 * Async mode
	 */
	if (ct->thread) {
		if (tur_check_async_timeout(c)) {
			int running = uatomic_xchg(&ct->running, 0);
			if (running) {
				pthread_cancel(ct->thread);
				condlog(3, "%d:%d : tur checker timeout",
					major(ct->devt), minor(ct->devt));
				c->msgid = MSG_TUR_TIMEOUT;
				tur_status = PATH_TIMEOUT;
			} else {
				pthread_mutex_lock(&ct->lock);
				tur_status = ct->state;
				c->msgid = ct->msgid;
				pthread_mutex_unlock(&ct->lock);
			}
			ct->thread = 0;
		} else if (uatomic_read(&ct->running) != 0) {
			condlog(3, "%d:%d : tur checker not finished",
				major(ct->devt), minor(ct->devt));
			tur_status = PATH_PENDING;
		} else {
			/* TUR checker done */
			ct->thread = 0;
			pthread_mutex_lock(&ct->lock);
			tur_status = ct->state;
			c->msgid = ct->msgid;
			pthread_mutex_unlock(&ct->lock);
		}
	} else {
		if (uatomic_read(&ct->holders) > 1) {
			/*
			 * The thread has been cancelled but hasn't quit.
			 * We have to prevent it from interfering with the new
			 * thread. We create a new context and leave the old
			 * one with the stale thread, hoping it will clean up
			 * eventually.
			 */
			condlog(3, "%d:%d : tur thread not responding",
				major(ct->devt), minor(ct->devt));

			/*
			 * libcheck_init will replace c->context.
			 * It fails only in OOM situations. In this case, return
			 * PATH_UNCHECKED to avoid prematurely failing the path.
			 */
			if (libcheck_init(c) != 0)
				return PATH_UNCHECKED;

			if (!uatomic_sub_return(&ct->holders, 1))
				/* It did terminate, eventually */
				cleanup_context(ct);

			ct = c->context;
		}
		/* Start new TUR checker */
		pthread_mutex_lock(&ct->lock);
		tur_status = ct->state = PATH_PENDING;
		ct->msgid = CHECKER_MSGID_NONE;
		pthread_mutex_unlock(&ct->lock);
		ct->fd = c->fd;
		ct->timeout = c->timeout;
		uatomic_add(&ct->holders, 1);
		uatomic_set(&ct->running, 1);
		tur_set_async_timeout(c);
		setup_thread_attr(&attr, 32 * 1024, 1);
		r = pthread_create(&ct->thread, &attr, tur_thread, ct);
		pthread_attr_destroy(&attr);
		if (r) {
			uatomic_sub(&ct->holders, 1);
			uatomic_set(&ct->running, 0);
			ct->thread = 0;
			condlog(3, "%d:%d : failed to start tur thread, using"
				" sync mode", major(ct->devt), minor(ct->devt));
			return tur_check(c->fd, c->timeout, &c->msgid);
		}
		tur_timeout(&tsp);
		pthread_mutex_lock(&ct->lock);
		if (ct->state == PATH_PENDING)
			r = pthread_cond_timedwait(&ct->active, &ct->lock,
						   &tsp);
		if (!r) {
			tur_status = ct->state;
			c->msgid = ct->msgid;
		}
		pthread_mutex_unlock(&ct->lock);
		if (tur_status == PATH_PENDING) {
			condlog(4, "%d:%d : tur checker still running",
				major(ct->devt), minor(ct->devt));
		} else {
			int running = uatomic_xchg(&ct->running, 0);
			if (running)
				pthread_cancel(ct->thread);
			ct->thread = 0;
		}
	}

	return tur_status;
}
