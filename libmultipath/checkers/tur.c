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
#include <errno.h>
#include <sys/time.h>
#include <pthread.h>

#include "checkers.h"

#include "../libmultipath/debug.h"
#include "../libmultipath/sg_include.h"
#include "../libmultipath/uevent.h"

#define TUR_CMD_LEN 6
#define HEAVY_CHECK_COUNT       10

#define MSG_TUR_UP	"tur checker reports path is up"
#define MSG_TUR_DOWN	"tur checker reports path is down"
#define MSG_TUR_GHOST	"tur checker reports path is in standby state"
#define MSG_TUR_RUNNING	"tur checker still running"
#define MSG_TUR_TIMEOUT	"tur checker timed out"
#define MSG_TUR_FAILED	"tur checker failed to initialize"

struct tur_checker_context {
	dev_t devt;
	int state;
	int running;
	int fd;
	unsigned int timeout;
	time_t time;
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t active;
	pthread_spinlock_t hldr_lock;
	int holders;
	char message[CHECKER_MSG_LEN];
};

#define TUR_DEVT(c) major((c)->devt), minor((c)->devt)

int libcheck_init (struct checker * c)
{
	struct tur_checker_context *ct;

	ct = malloc(sizeof(struct tur_checker_context));
	if (!ct)
		return 1;
	memset(ct, 0, sizeof(struct tur_checker_context));

	ct->state = PATH_UNCHECKED;
	ct->fd = -1;
	ct->holders = 1;
	pthread_cond_init(&ct->active, NULL);
	pthread_mutex_init(&ct->lock, NULL);
	pthread_spin_init(&ct->hldr_lock, PTHREAD_PROCESS_PRIVATE);
	c->context = ct;

	return 0;
}

void cleanup_context(struct tur_checker_context *ct)
{
	pthread_mutex_destroy(&ct->lock);
	pthread_cond_destroy(&ct->active);
	pthread_spin_destroy(&ct->hldr_lock);
	free(ct);
}

void libcheck_free (struct checker * c)
{
	if (c->context) {
		struct tur_checker_context *ct = c->context;
		int holders;
		pthread_t thread;

		pthread_spin_lock(&ct->hldr_lock);
		ct->holders--;
		holders = ct->holders;
		thread = ct->thread;
		pthread_spin_unlock(&ct->hldr_lock);
		if (holders)
			pthread_cancel(thread);
		else
			cleanup_context(ct);
		c->context = NULL;
	}
	return;
}

#define TUR_MSG(msg, fmt, args...) snprintf(msg, CHECKER_MSG_LEN, fmt, ##args);

int
tur_check(int fd, unsigned int timeout, char *msg)
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
		TUR_MSG(msg, MSG_TUR_DOWN);
		return PATH_DOWN;
	}
	if ((io_hdr.status & 0x7e) == 0x18) {
		/*
		 * SCSI-3 arrays might return
		 * reservation conflict on TUR
		 */
		TUR_MSG(msg, MSG_TUR_UP);
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
				TUR_MSG(msg, MSG_TUR_GHOST);
				return PATH_GHOST;
			}
		}
		TUR_MSG(msg, MSG_TUR_DOWN);
		return PATH_DOWN;
	}
	TUR_MSG(msg, MSG_TUR_UP);
	return PATH_UP;
}

#define tur_thread_cleanup_push(ct) pthread_cleanup_push(cleanup_func, ct)
#define tur_thread_cleanup_pop(ct) pthread_cleanup_pop(1)

void cleanup_func(void *data)
{
	int holders;
	struct tur_checker_context *ct = data;
	pthread_spin_lock(&ct->hldr_lock);
	ct->holders--;
	holders = ct->holders;
	ct->thread = 0;
	pthread_spin_unlock(&ct->hldr_lock);
	if (!holders)
		cleanup_context(ct);
}

void *tur_thread(void *ctx)
{
	struct tur_checker_context *ct = ctx;
	int state;

	condlog(3, "%d:%d: tur checker starting up", TUR_DEVT(ct));

	ct->message[0] = '\0';
	/* This thread can be canceled, so setup clean up */
	tur_thread_cleanup_push(ct)

	/* TUR checker start up */
	pthread_mutex_lock(&ct->lock);
	ct->state = PATH_PENDING;
	pthread_mutex_unlock(&ct->lock);

	state = tur_check(ct->fd, ct->timeout, ct->message);

	/* TUR checker done */
	pthread_mutex_lock(&ct->lock);
	ct->state = state;
	pthread_mutex_unlock(&ct->lock);
	pthread_cond_signal(&ct->active);

	condlog(3, "%d:%d: tur checker finished, state %s",
		TUR_DEVT(ct), checker_state_name(state));
	tur_thread_cleanup_pop(ct);
	return ((void *)0);
}


void tur_timeout(struct timespec *tsp)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	tsp->tv_sec = now.tv_sec;
	tsp->tv_nsec = now.tv_usec * 1000;
	tsp->tv_nsec += 1000000; /* 1 millisecond */
}

void tur_set_async_timeout(struct checker *c)
{
	struct tur_checker_context *ct = c->context;
	struct timeval now;

	gettimeofday(&now, NULL);
	ct->time = now.tv_sec + c->timeout;
}

int tur_check_async_timeout(struct checker *c)
{
	struct tur_checker_context *ct = c->context;
	struct timeval now;

	gettimeofday(&now, NULL);
	return (now.tv_sec > ct->time);
}

extern int
libcheck_check (struct checker * c)
{
	struct tur_checker_context *ct = c->context;
	struct timespec tsp;
	struct stat sb;
	pthread_attr_t attr;
	int tur_status, r;


	if (!ct)
		return PATH_UNCHECKED;

	if (fstat(c->fd, &sb) == 0)
		ct->devt = sb.st_rdev;

	if (c->sync)
		return tur_check(c->fd, c->timeout, c->message);

	/*
	 * Async mode
	 */
	r = pthread_mutex_lock(&ct->lock);
	if (r != 0) {
		condlog(2, "%d:%d: tur mutex lock failed with %d",
			TUR_DEVT(ct), r);
		MSG(c, MSG_TUR_FAILED);
		return PATH_WILD;
	}

	if (ct->running) {
		/* Check if TUR checker is still running */
		if (ct->thread) {
			if (tur_check_async_timeout(c)) {
				condlog(3, "%d:%d: tur checker timeout",
					TUR_DEVT(ct));
				pthread_cancel(ct->thread);
				ct->running = 0;
				MSG(c, MSG_TUR_TIMEOUT);
				tur_status = PATH_TIMEOUT;
			} else {
				condlog(3, "%d:%d: tur checker not finished",
					TUR_DEVT(ct));
				ct->running++;
				tur_status = PATH_PENDING;
			}
		} else {
			/* TUR checker done */
			ct->running = 0;
			tur_status = ct->state;
			strncpy(c->message, ct->message, CHECKER_MSG_LEN);
			c->message[CHECKER_MSG_LEN - 1] = '\0';
		}
		pthread_mutex_unlock(&ct->lock);
	} else {
		if (ct->thread) {
			/* pthread cancel failed. continue in sync mode */
			pthread_mutex_unlock(&ct->lock);
			condlog(3, "%d:%d: tur thread not responding",
				TUR_DEVT(ct));
			return PATH_TIMEOUT;
		}
		/* Start new TUR checker */
		ct->state = PATH_UNCHECKED;
		ct->fd = c->fd;
		ct->timeout = c->timeout;
		pthread_spin_lock(&ct->hldr_lock);
		ct->holders++;
		pthread_spin_unlock(&ct->hldr_lock);
		tur_set_async_timeout(c);
		setup_thread_attr(&attr, 32 * 1024, 1);
		r = pthread_create(&ct->thread, &attr, tur_thread, ct);
		if (r) {
			pthread_mutex_unlock(&ct->lock);
			ct->thread = 0;
			ct->holders--;
			condlog(3, "%d:%d: failed to start tur thread, using"
				" sync mode", TUR_DEVT(ct));
			return tur_check(c->fd, c->timeout, c->message);
		}
		pthread_attr_destroy(&attr);
		tur_timeout(&tsp);
		r = pthread_cond_timedwait(&ct->active, &ct->lock, &tsp);
		tur_status = ct->state;
		strncpy(c->message, ct->message,CHECKER_MSG_LEN);
		c->message[CHECKER_MSG_LEN -1] = '\0';
		pthread_mutex_unlock(&ct->lock);
		if (ct->thread &&
		    (tur_status == PATH_PENDING || tur_status == PATH_UNCHECKED)) {
			condlog(3, "%d:%d: tur checker still running",
				TUR_DEVT(ct));
			ct->running = 1;
			tur_status = PATH_PENDING;
		}
	}

	return tur_status;
}
