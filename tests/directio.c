/*
 * Copyright (c) 2018 Benjamin Marzinski, Redhat
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <cmocka.h>
#include "wrap64.h"
#include "globals.c"
#include "../libmultipath/checkers/directio.c"

int test_fd = 111;
int ioctx_count = 0;
struct io_event mock_events[AIO_GROUP_SIZE]; /* same as the checker max */
int ev_off = 0;
struct timespec zero_timeout = { .tv_sec = 0 };
struct timespec full_timeout = { .tv_sec = -1 };

#ifdef __GLIBC__
#define ioctl_request_t unsigned long
#else
#define ioctl_request_t int
#endif

int REAL_IOCTL(int fd, ioctl_request_t request, void *argp);

int WRAP_IOCTL(int fd, ioctl_request_t request, void *argp)
{
#ifdef DIO_TEST_DEV
	mock_type(int);
	return REAL_IOCTL(fd, request, argp);
#else
	int *blocksize = (int *)argp;

	assert_int_equal(fd, test_fd);
	/*
	 * On MUSL libc, the "request" arg is an int (glibc: unsigned long).
	 * cmocka casts the args of assert_int_equal() to "unsigned long".
	 * BLKSZGET = 80081270 is sign-extended to ffffffff80081270
	 * when cast from int to unsigned long on s390x.
	 * BLKSZGET must be cast to "int" and back to "unsigned long",
	 * otherwise the assertion below will fail.
	 */
	assert_int_equal(request, (ioctl_request_t)BLKBSZGET);
	assert_non_null(blocksize);
	*blocksize = mock_type(int);
	return 0;
#endif
}

int REAL_FCNTL (int fd, int cmd, long arg);

int WRAP_FCNTL (int fd, int cmd, long arg)
{
#ifdef DIO_TEST_DEV
	return REAL_FCNTL(fd, cmd, arg);
#else
	assert_int_equal(fd, test_fd);
	assert_int_equal(cmd, F_GETFL);
	return O_DIRECT;
#endif
}

int __real___fxstat(int ver, int fd, struct stat *statbuf);

int __wrap___fxstat(int ver, int fd, struct stat *statbuf)
{
#ifdef DIO_TEST_DEV
	return __real___fxstat(ver, fd, statbuf);
#else
	assert_int_equal(fd, test_fd);
	assert_non_null(statbuf);
	memset(statbuf, 0, sizeof(struct stat));
	return 0;
#endif
}

int __real_io_setup(int maxevents, io_context_t *ctxp);

int __wrap_io_setup(int maxevents, io_context_t *ctxp)
{
	ioctx_count++;
#ifdef DIO_TEST_DEV
	int ret = mock_type(int);
	assert_int_equal(ret, __real_io_setup(maxevents, ctxp));
	return ret;
#else
	return mock_type(int);
#endif
}

int __real_io_destroy(io_context_t ctx);

int __wrap_io_destroy(io_context_t ctx)
{
	ioctx_count--;
#ifdef DIO_TEST_DEV
	int ret = mock_type(int);
	assert_int_equal(ret, __real_io_destroy(ctx));
	return ret;
#else
	return mock_type(int);
#endif
}

int __real_io_submit(io_context_t ctx, long nr, struct iocb *ios[]);

int __wrap_io_submit(io_context_t ctx, long nr, struct iocb *ios[])
{
#ifdef DIO_TEST_DEV
	struct timespec dev_delay = { .tv_nsec = 100000 };
	int ret = mock_type(int);
	assert_int_equal(ret, __real_io_submit(ctx, nr, ios));
	nanosleep(&dev_delay, NULL);
	return ret;
#else
	return mock_type(int);
#endif
}

int __real_io_cancel(io_context_t ctx, struct iocb *iocb, struct io_event *evt);

int __wrap_io_cancel(io_context_t ctx, struct iocb *iocb, struct io_event *evt)
{
#ifdef DIO_TEST_DEV
	return __real_io_cancel(ctx, iocb, evt);
#else
	return 0;
#endif
}

int REAL_IO_GETEVENTS(io_context_t ctx, long min_nr, long nr,
			struct io_event *events, struct timespec *timeout);

int WRAP_IO_GETEVENTS(io_context_t ctx, long min_nr, long nr,
			struct io_event *events, struct timespec *timeout)
{
	int nr_evs;
#ifndef DIO_TEST_DEV
	struct timespec *sleep_tmo;
	int i;
	struct io_event *evs;
#endif

	assert_non_null(timeout);
	nr_evs = mock_type(int);
	assert_true(nr_evs <= nr);
	if (!nr_evs)
		return 0;
#ifdef DIO_TEST_DEV
	mock_ptr_type(struct timespec *);
	mock_ptr_type(struct io_event *);
	assert_int_equal(nr_evs, REAL_IO_GETEVENTS(ctx, min_nr, nr_evs,
						   events, timeout));
#else
	sleep_tmo = mock_ptr_type(struct timespec *);
	if (sleep_tmo) {
		if (sleep_tmo->tv_sec < 0)
			nanosleep(timeout, NULL);
		else
			nanosleep(sleep_tmo, NULL);
	}
	if (nr_evs < 0) {
		errno = -nr_evs;
		return -1;
	}
	evs = mock_ptr_type(struct io_event *);
	for (i = 0; i < nr_evs; i++)
		events[i] = evs[i];
#endif
	ev_off -= nr_evs;
	return nr_evs;
}

static void return_io_getevents_none(void)
{
	wrap_will_return(WRAP_IO_GETEVENTS, 0);
}

static void return_io_getevents_nr(struct timespec *ts, int nr,
				   struct async_req **reqs, int *res)
{
	int i, off = 0;

	for(i = 0; i < nr; i++) {
		mock_events[i + ev_off].obj = &reqs[i]->io;
		if (res[i] == 0)
			mock_events[i + ev_off].res = reqs[i]->blksize;
	}
	while (nr > 0) {
		wrap_will_return(WRAP_IO_GETEVENTS, (nr > 128)? 128 : nr);
		wrap_will_return(WRAP_IO_GETEVENTS, ts);
		wrap_will_return(WRAP_IO_GETEVENTS, &mock_events[off + ev_off]);
		ts = NULL;
		off += 128;
		nr -= 128;
	}
	if (nr == 0)
		wrap_will_return(WRAP_IO_GETEVENTS, 0);
	ev_off += i;
}

void do_check_state(struct checker *c, int sync, int timeout, int chk_state)
{
	struct directio_context * ct = (struct directio_context *)c->context;

	if (!ct->running)
		will_return(__wrap_io_submit, 1);
	assert_int_equal(check_state(test_fd, ct, sync, timeout), chk_state);
	assert_int_equal(ev_off, 0);
	memset(mock_events, 0, sizeof(mock_events));
}

void do_libcheck_reset(int nr_aio_grps)
{
	int count = 0;
	struct aio_group *aio_grp;

	list_for_each_entry(aio_grp, &aio_grp_list, node)
		count++;
	assert_int_equal(count, nr_aio_grps);
	for (count = 0; count < nr_aio_grps; count++)
		will_return(__wrap_io_destroy, 0);
	libcheck_reset();
	assert_true(list_empty(&aio_grp_list));
	assert_int_equal(ioctx_count, 0);
}

static void do_libcheck_init(struct checker *c, int blocksize,
			     struct async_req **req)
{
	struct directio_context * ct;

	c->fd = test_fd;
	wrap_will_return(WRAP_IOCTL, blocksize);
	assert_int_equal(libcheck_init(c), 0);
	ct = (struct directio_context *)c->context;
	assert_non_null(ct);
	assert_non_null(ct->aio_grp);
	assert_non_null(ct->req);
	if (req)
		*req = ct->req;
#ifndef DIO_TEST_DEV
	/* don't check fake blocksize on real devices */
	assert_int_equal(ct->req->blksize, blocksize);
#endif
}

static int is_checker_running(struct checker *c)
{
	struct directio_context * ct = (struct directio_context *)c->context;
	return ct->running;
}

static struct aio_group *get_aio_grp(struct checker *c)
{
	struct directio_context * ct = (struct directio_context *)c->context;

	assert_non_null(ct);
	return ct->aio_grp;
}

static void check_aio_grp(struct aio_group *aio_grp, int holders,
			  int orphans)
{
	int count = 0;
	struct list_head *item;

	list_for_each(item, &aio_grp->orphans)
		count++;
	assert_int_equal(holders, aio_grp->holders);
	assert_int_equal(orphans, count);
}

/* simple resetting test */
static void test_reset(void **state)
{
	assert_true(list_empty(&aio_grp_list));
	do_libcheck_reset(0);
}

/* tests initializing, then resetting, and then initializing again */
static void test_init_reset_init(void **state)
{
	struct checker c = {.cls = NULL};
	struct aio_group *aio_grp, *tmp_grp;

	assert_true(list_empty(&aio_grp_list));
	will_return(__wrap_io_setup, 0);
	do_libcheck_init(&c, 4096, NULL);
	aio_grp = get_aio_grp(&c);
	check_aio_grp(aio_grp, 1, 0);
	list_for_each_entry(tmp_grp, &aio_grp_list, node)
		assert_ptr_equal(aio_grp, tmp_grp);
	libcheck_free(&c);
	check_aio_grp(aio_grp, 0, 0);
	do_libcheck_reset(1);
	will_return(__wrap_io_setup, 0);
	do_libcheck_init(&c, 4096, NULL);
	aio_grp = get_aio_grp(&c);
	check_aio_grp(aio_grp, 1, 0);
	list_for_each_entry(tmp_grp, &aio_grp_list, node)
		assert_ptr_equal(aio_grp, tmp_grp);
	libcheck_free(&c);
	check_aio_grp(aio_grp, 0, 0);
	do_libcheck_reset(1);
}

/* test initializing and then freeing 4096 checkers */
static void test_init_free(void **state)
{
	int i, count = 0;
	struct checker c[4096] = {{.cls = NULL}};
	struct aio_group *aio_grp = NULL;

	assert_true(list_empty(&aio_grp_list));
	will_return(__wrap_io_setup, 0);
	will_return(__wrap_io_setup, 0);
	will_return(__wrap_io_setup, 0);
	will_return(__wrap_io_setup, 0);
	for (i = 0; i < 4096; i++) {
		struct directio_context * ct;

		if (i % 3 == 0)
			do_libcheck_init(&c[i], 512, NULL);
		else if (i % 3 == 1)
			do_libcheck_init(&c[i], 1024, NULL);
		else
			do_libcheck_init(&c[i], 4096, NULL);
		ct = (struct directio_context *)c[i].context;
		assert_non_null(ct->aio_grp);
		if ((i & 1023) == 0)
			aio_grp = ct->aio_grp;
		else {
			assert_ptr_equal(ct->aio_grp, aio_grp);
			assert_int_equal(aio_grp->holders, (i & 1023) + 1);
		}
	}
	count = 0;
	list_for_each_entry(aio_grp, &aio_grp_list, node)
		count++;
	assert_int_equal(count, 4);
	for (i = 0; i < 4096; i++) {
		struct directio_context * ct = (struct directio_context *)c[i].context;

		aio_grp = ct->aio_grp;
		libcheck_free(&c[i]);
		assert_int_equal(aio_grp->holders, 1023 - (i & 1023));
	}
	list_for_each_entry(aio_grp, &aio_grp_list, node)
		assert_int_equal(aio_grp->holders, 0);
	do_libcheck_reset(4);
}

/* check mixed initializing and freeing 4096 checkers */
static void test_multi_init_free(void **state)
{
	int i, count;
	struct checker c[4096] = {{.cls = NULL}};
	struct aio_group *aio_grp;

	assert_true(list_empty(&aio_grp_list));
	will_return(__wrap_io_setup, 0);
	will_return(__wrap_io_setup, 0);
	will_return(__wrap_io_setup, 0);
	will_return(__wrap_io_setup, 0);
	for (count = 0, i = 0; i < 4096; count++) {
		/* usually init, but occasionally free checkers */
		if (count == 0 || (count % 5 != 0 && count % 7 != 0)) {
			do_libcheck_init(&c[i], 4096, NULL);
			i++;
		} else {
			i--;
			libcheck_free(&c[i]);
		}
	}
	count = 0;
	list_for_each_entry(aio_grp, &aio_grp_list, node) {
		assert_int_equal(aio_grp->holders, 1024);
		count++;
	}
	assert_int_equal(count, 4);
	for (count = 0, i = 4096; i > 0; count++) {
		/* usually free, but occasionally init checkers */
		if (count == 0 || (count % 5 != 0 && count % 7 != 0)) {
			i--;
			libcheck_free(&c[i]);
		} else {
			do_libcheck_init(&c[i], 4096, NULL);
			i++;
		}
	}
	do_libcheck_reset(4);
}

/* simple single checker sync test */
static void test_check_state_simple(void **state)
{
	struct checker c = {.cls = NULL};
	struct async_req *req;
	int res = 0;

	assert_true(list_empty(&aio_grp_list));
	will_return(__wrap_io_setup, 0);
	do_libcheck_init(&c, 4096, &req);
	return_io_getevents_nr(NULL, 1, &req, &res);
	do_check_state(&c, 1, 30, PATH_UP);
	libcheck_free(&c);
	do_libcheck_reset(1);
}

/* test sync timeout */
static void test_check_state_timeout(void **state)
{
	struct checker c = {.cls = NULL};
	struct aio_group *aio_grp;

	assert_true(list_empty(&aio_grp_list));
	will_return(__wrap_io_setup, 0);
	do_libcheck_init(&c, 4096, NULL);
	aio_grp = get_aio_grp(&c);
	return_io_getevents_none();
	do_check_state(&c, 1, 30, PATH_DOWN);
	check_aio_grp(aio_grp, 1, 0);
	libcheck_free(&c);
	do_libcheck_reset(1);
}

/* test async timeout */
static void test_check_state_async_timeout(void **state)
{
	struct checker c = {.cls = NULL};
	struct aio_group *aio_grp;

	assert_true(list_empty(&aio_grp_list));
	will_return(__wrap_io_setup, 0);
	do_libcheck_init(&c, 4096, NULL);
	aio_grp = get_aio_grp(&c);
	return_io_getevents_none();
	do_check_state(&c, 0, 3, PATH_PENDING);
	return_io_getevents_none();
	do_check_state(&c, 0, 3, PATH_PENDING);
	return_io_getevents_none();
	do_check_state(&c, 0, 3, PATH_PENDING);
	return_io_getevents_none();
	do_check_state(&c, 0, 3, PATH_DOWN);
	check_aio_grp(aio_grp, 1, 0);
	libcheck_free(&c);
	do_libcheck_reset(1);
}

/* test freeing checkers with outstanding requests */
static void test_free_with_pending(void **state)
{
        struct checker c[2] = {{.cls = NULL}};
        struct aio_group *aio_grp;
	struct async_req *req;
	int res = 0;

	assert_true(list_empty(&aio_grp_list));
	will_return(__wrap_io_setup, 0);
        do_libcheck_init(&c[0], 4096, &req);
	do_libcheck_init(&c[1], 4096, NULL);
        aio_grp = get_aio_grp(c);
        return_io_getevents_none();
        do_check_state(&c[0], 0, 30, PATH_PENDING);
	return_io_getevents_nr(NULL, 1, &req, &res);
	return_io_getevents_none();
	do_check_state(&c[1], 0, 30, PATH_PENDING);
	assert_true(is_checker_running(&c[0]));
	assert_true(is_checker_running(&c[1]));
	check_aio_grp(aio_grp, 2, 0);
        libcheck_free(&c[0]);
	check_aio_grp(aio_grp, 1, 0);
        libcheck_free(&c[1]);
	check_aio_grp(aio_grp, 1, 1); /* cancel doesn't remove request */
        do_libcheck_reset(1);
}

/* test removing orphaned aio_group on free */
static void test_orphaned_aio_group(void **state)
{
	struct checker c[AIO_GROUP_SIZE] = {{.cls = NULL}};
	struct aio_group *aio_grp, *tmp_grp;
	int i;

	assert_true(list_empty(&aio_grp_list));
	will_return(__wrap_io_setup, 0);
	for (i = 0; i < AIO_GROUP_SIZE; i++) {
		do_libcheck_init(&c[i], 4096, NULL);
		return_io_getevents_none();
		do_check_state(&c[i], 0, 30, PATH_PENDING);
	}
	aio_grp = get_aio_grp(c);
	check_aio_grp(aio_grp, AIO_GROUP_SIZE, 0);
	i = 0;
	list_for_each_entry(tmp_grp, &aio_grp_list, node)
		i++;
	assert_int_equal(i, 1);
	for (i = 0; i < AIO_GROUP_SIZE; i++) {
		assert_true(is_checker_running(&c[i]));
		if (i == AIO_GROUP_SIZE - 1) {
			/* remove the orphaned group and create a new one */
			will_return(__wrap_io_destroy, 0);
		}
		libcheck_free(&c[i]);
	}
        do_libcheck_reset(0);
}

/* test sync timeout with failed cancel and cleanup by another
 * checker */
static void test_timeout_cancel_failed(void **state)
{
	struct checker c[2] = {{.cls = NULL}};
	struct aio_group *aio_grp;
	struct async_req *reqs[2];
	int res[] = {0,0};
	int i;

	assert_true(list_empty(&aio_grp_list));
	will_return(__wrap_io_setup, 0);
	for (i = 0; i < 2; i++)
		do_libcheck_init(&c[i], 4096, &reqs[i]);
	aio_grp = get_aio_grp(c);
	return_io_getevents_none();
	do_check_state(&c[0], 1, 30, PATH_DOWN);
	assert_true(is_checker_running(&c[0]));
	check_aio_grp(aio_grp, 2, 0);
	return_io_getevents_none();
	do_check_state(&c[0], 1, 30, PATH_DOWN);
	assert_true(is_checker_running(&c[0]));
	return_io_getevents_nr(NULL, 1, &reqs[0], &res[0]);
	return_io_getevents_nr(NULL, 1, &reqs[1], &res[1]);
	do_check_state(&c[1], 1, 30, PATH_UP);
	do_check_state(&c[0], 1, 30, PATH_UP);
	for (i = 0; i < 2; i++) {
		assert_false(is_checker_running(&c[i]));
		libcheck_free(&c[i]);
	}
	do_libcheck_reset(1);
}

/* test async timeout with failed cancel and cleanup by another
 * checker */
static void test_async_timeout_cancel_failed(void **state)
{
	struct checker c[2] = {{.cls = NULL}};
	struct async_req *reqs[2];
	int res[] = {0,0};
	int i;

	assert_true(list_empty(&aio_grp_list));
	will_return(__wrap_io_setup, 0);
	for (i = 0; i < 2; i++)
		do_libcheck_init(&c[i], 4096, &reqs[i]);
	return_io_getevents_none();
	do_check_state(&c[0], 0, 2, PATH_PENDING);
	return_io_getevents_none();
	do_check_state(&c[1], 0, 2, PATH_PENDING);
	return_io_getevents_none();
	do_check_state(&c[0], 0, 2, PATH_PENDING);
	return_io_getevents_none();
	do_check_state(&c[1], 0, 2, PATH_PENDING);
	return_io_getevents_none();
	do_check_state(&c[0], 0, 2, PATH_DOWN);
#ifndef DIO_TEST_DEV
	/* can't pick which even gets returned on real devices */
	return_io_getevents_nr(NULL, 1, &reqs[1], &res[1]);
	do_check_state(&c[1], 0, 2, PATH_UP);
#endif
	return_io_getevents_none();
	do_check_state(&c[0], 0, 2, PATH_DOWN);
	assert_true(is_checker_running(&c[0]));
	return_io_getevents_nr(NULL, 2, reqs, res);
	do_check_state(&c[1], 0, 2, PATH_UP);
	do_check_state(&c[0], 0, 2, PATH_UP);
	for (i = 0; i < 2; i++) {
		assert_false(is_checker_running(&c[i]));
		libcheck_free(&c[i]);
	}
	do_libcheck_reset(1);
}

/* test orphaning a request, and having another checker clean it up */
static void test_orphan_checker_cleanup(void **state)
{
	struct checker c[2] = {{.cls = NULL}};
	struct async_req *reqs[2];
	int res[] = {0,0};
	struct aio_group *aio_grp;
	int i;

	assert_true(list_empty(&aio_grp_list));
	will_return(__wrap_io_setup, 0);
	for (i = 0; i < 2; i++)
		do_libcheck_init(&c[i], 4096, &reqs[i]);
	aio_grp = get_aio_grp(c);
	return_io_getevents_none();
	do_check_state(&c[0], 0, 30, PATH_PENDING);
	check_aio_grp(aio_grp, 2, 0);
	libcheck_free(&c[0]);
	check_aio_grp(aio_grp, 2, 1);
	return_io_getevents_nr(NULL, 2, reqs, res);
	do_check_state(&c[1], 0, 2, PATH_UP);
	check_aio_grp(aio_grp, 1, 0);
	libcheck_free(&c[1]);
	check_aio_grp(aio_grp, 0, 0);
	do_libcheck_reset(1);
}

/* test orphaning a request, and having reset clean it up */
static void test_orphan_reset_cleanup(void **state)
{
	struct checker c;
	struct aio_group *orphan_aio_grp, *tmp_aio_grp;
	int found, count;

	assert_true(list_empty(&aio_grp_list));
	will_return(__wrap_io_setup, 0);
	do_libcheck_init(&c, 4096, NULL);
	orphan_aio_grp = get_aio_grp(&c);
	return_io_getevents_none();
	do_check_state(&c, 0, 30, PATH_PENDING);
	check_aio_grp(orphan_aio_grp, 1, 0);
	libcheck_free(&c);
	check_aio_grp(orphan_aio_grp, 1, 1);
	found = count = 0;
	list_for_each_entry(tmp_aio_grp, &aio_grp_list, node) {
		count++;
		if (tmp_aio_grp == orphan_aio_grp)
			found = 1;
	}
	assert_int_equal(count, 1);
	assert_int_equal(found, 1);
	do_libcheck_reset(1);
}

/* test checkers with different blocksizes */
static void test_check_state_blksize(void **state)
{
	int i;
	struct checker c[3] = {{.cls = NULL}};
	int blksize[] = {4096, 1024, 512};
	struct async_req *reqs[3];
	int res[] = {0,1,0};
#ifdef DIO_TEST_DEV
	/* can't pick event return state on real devices */
	int chk_state[] = {PATH_UP, PATH_UP, PATH_UP};
#else
	int chk_state[] = {PATH_UP, PATH_DOWN, PATH_UP};
#endif

	assert_true(list_empty(&aio_grp_list));
	will_return(__wrap_io_setup, 0);
	for (i = 0; i < 3; i++)
		do_libcheck_init(&c[i], blksize[i], &reqs[i]);
	for (i = 0; i < 3; i++) {
		return_io_getevents_nr(NULL, 1, &reqs[i], &res[i]);
		do_check_state(&c[i], 1, 30, chk_state[i]);
	}
	for (i = 0; i < 3; i++) {
		assert_false(is_checker_running(&c[i]));
		libcheck_free(&c[i]);
	}
	do_libcheck_reset(1);
}

/* test async checkers pending and getting resolved by another checker
 * as well as the loops for getting multiple events */
static void test_check_state_async(void **state)
{
	int i;
	struct checker c[257] = {{.cls = NULL}};
	struct async_req *reqs[257];
	int res[257] = {0};

	assert_true(list_empty(&aio_grp_list));
	will_return(__wrap_io_setup, 0);
	for (i = 0; i < 257; i++)
		do_libcheck_init(&c[i], 4096, &reqs[i]);
	for (i = 0; i < 256; i++) {
		return_io_getevents_none();
		do_check_state(&c[i], 0, 30, PATH_PENDING);
		assert_true(is_checker_running(&c[i]));
	}
	return_io_getevents_nr(&full_timeout, 256, reqs, res);
	return_io_getevents_nr(NULL, 1, &reqs[256], &res[256]);
	do_check_state(&c[256], 0, 30, PATH_UP);
	assert_false(is_checker_running(&c[256]));
	libcheck_free(&c[256]);
	for (i = 0; i < 256; i++) {
		do_check_state(&c[i], 0, 30, PATH_UP);
		assert_false(is_checker_running(&c[i]));
		libcheck_free(&c[i]);
	}
	do_libcheck_reset(1);
}

static int setup(void **state)
{
#ifdef DIO_TEST_DEV
	test_fd = open(DIO_TEST_DEV, O_RDONLY);
	if (test_fd < 0)
		fail_msg("cannot open %s: %m", DIO_TEST_DEV);
#endif
	return 0;
}

static int teardown(void **state)
{
#ifdef DIO_TEST_DEV
	assert_true(test_fd > 0);
	assert_int_equal(close(test_fd), 0);
#endif
	return 0;
}

int test_directio(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_reset),
		cmocka_unit_test(test_init_reset_init),
		cmocka_unit_test(test_init_free),
		cmocka_unit_test(test_multi_init_free),
		cmocka_unit_test(test_check_state_simple),
		cmocka_unit_test(test_check_state_timeout),
		cmocka_unit_test(test_check_state_async_timeout),
		cmocka_unit_test(test_free_with_pending),
		cmocka_unit_test(test_timeout_cancel_failed),
		cmocka_unit_test(test_async_timeout_cancel_failed),
		cmocka_unit_test(test_orphan_checker_cleanup),
		cmocka_unit_test(test_orphan_reset_cleanup),
		cmocka_unit_test(test_check_state_blksize),
		cmocka_unit_test(test_check_state_async),
		cmocka_unit_test(test_orphaned_aio_group),
	};

	return cmocka_run_group_tests(tests, setup, teardown);
}

int main(void)
{
	int ret = 0;

	init_test_verbosity(5);
	ret += test_directio();
	return ret;
}
