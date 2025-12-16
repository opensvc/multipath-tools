/*
 * Copyright (c) 2021 SUSE LLC
 * SPDX-License-Identifier: GPL-2.0-only
 */

#define _GNU_SOURCE
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include "cmocka-compat.h"
#include "mt-udev-wrap.h"
#include <fcntl.h>
#include <errno.h>
#include "debug.h"
#include "globals.c"
#include "test-log.h"
#include "sysfs.h"
#include "util.h"
#include "wrap64.h"

#define TEST_FD 123

const char *__wrap_udev_device_get_syspath(struct udev_device *ud)
{
	const char *val = mock_ptr_type(const char *);

	return val;
}

int WRAP_OPEN(const char *pathname, int flags)
{
	int ret;

	check_expected_ptr(pathname);
	check_expected_int(flags);
	ret = mock_type(int);
	return ret;
}

ssize_t __wrap_read(int fd, void *buf, size_t count)
{
	ssize_t ret;
	const char *val;

	check_expected_int(fd);
	check_expected_uint(count);
	ret = mock_type(int);
	val = mock_ptr_type(const char *);
	if (ret >= (ssize_t)count)
		ret = count;
	if (ret >= 0 && val) {
		fprintf(stderr, "%s: '%s' -> %zd\n", __func__, val, ret);
		memcpy(buf, val, ret);
	}
	return ret;
}

ssize_t __wrap_write(int fd, void *buf, size_t count)
{
	ssize_t ret;

	check_expected_int(fd);
	check_expected_uint(count);
	ret = mock_type(int);
	if (ret >= (ssize_t)count)
		ret = count;
	return ret;
}

int __real_close(int fd);
int __wrap_close(int fd) {
	if (fd != TEST_FD)
		return __real_close(fd);
	return mock_type(int);
}

static int setup(void **state)
{
	udev = udev_new();
	return 0;
}

static int teardown(void **state)
{
	udev_unref(udev);
	return 0;
}

static void expect_sagv_invalid(void)
{
	expect_condlog(1, "sysfs_attr_get_value__: invalid parameters");
}

static void test_sagv_invalid(void **state)
{
	expect_sagv_invalid();
	assert_int_equal(sysfs_attr_get_value(NULL, NULL, NULL, 0), -EINVAL);
	expect_sagv_invalid();
	assert_int_equal(sysfs_bin_attr_get_value(NULL, NULL, NULL, 0), -EINVAL);

	expect_sagv_invalid();
	assert_int_equal(sysfs_attr_get_value(NULL, (void *)state, (void *)state, 1),
			 -EINVAL);
	expect_sagv_invalid();
	assert_int_equal(sysfs_bin_attr_get_value(NULL, (void *)state, (void *)state, 1),
			 -EINVAL);

	expect_sagv_invalid();
	assert_int_equal(sysfs_attr_get_value((void *)state, NULL, (void *)state, 1),
			 -EINVAL);
	expect_sagv_invalid();
	assert_int_equal(sysfs_bin_attr_get_value((void *)state, NULL, (void *)state, 1),
			 -EINVAL);

	expect_sagv_invalid();
	assert_int_equal(sysfs_attr_get_value((void *)state, (void *)state, NULL, 1),
			 -EINVAL);
	expect_sagv_invalid();
	assert_int_equal(sysfs_bin_attr_get_value((void *)state, (void *)state, NULL, 1),
			 -EINVAL);

	expect_sagv_invalid();
	assert_int_equal(sysfs_attr_get_value((void *)state, (void *)state,
					      (void *)state, 0), -EINVAL);
	expect_sagv_invalid();
	assert_int_equal(sysfs_bin_attr_get_value((void *)state, (void *)state,
						  (void *)state, 0), -EINVAL);
}

static void test_sagv_bad_udev(void **state)
{
	will_return(__wrap_udev_device_get_syspath, NULL);
	expect_condlog(3, "sysfs_attr_get_value__: invalid udevice");
	assert_int_equal(sysfs_attr_get_value((void *)state, (void *)state,
					      (void *)state, 1), -EINVAL);

	will_return(__wrap_udev_device_get_syspath, NULL);
	expect_condlog(3, "sysfs_attr_get_value__: invalid udevice");
	assert_int_equal(sysfs_bin_attr_get_value((void *)state, (void *)state,
						  (void *)state, 1), -EINVAL);
}

static void test_sagv_bad_snprintf(void **state)
{
	char longstr[PATH_MAX + 1];
	char buf[1];

	memset(longstr, 'a', sizeof(longstr) - 1);
	longstr[sizeof(longstr) - 1] = '\0';

	will_return(__wrap_udev_device_get_syspath, "/foo");
	expect_condlog(3, "sysfs_attr_get_value__: devpath overflow");
	assert_int_equal(sysfs_attr_get_value((void *)state, longstr,
					      buf, sizeof(buf)), -EOVERFLOW);
	will_return(__wrap_udev_device_get_syspath, "/foo");
	expect_condlog(3, "sysfs_attr_get_value__: devpath overflow");
	assert_int_equal(sysfs_bin_attr_get_value((void *)state, longstr,
						  (unsigned char *)buf, sizeof(buf)),
			 -EOVERFLOW);
}

static void test_sagv_open_fail(void **state)
{
	char buf[1];

	will_return(__wrap_udev_device_get_syspath, "/foo");
	expect_condlog(4, "open '/foo/bar'");
	expect_string(WRAP_OPEN, pathname, "/foo/bar");
	expect_int_value(WRAP_OPEN, flags, O_RDONLY);
	errno = ENOENT;
	wrap_will_return(WRAP_OPEN, -1);
	expect_condlog(3, "sysfs_attr_get_value__: attribute '/foo/bar' cannot be opened");
	assert_int_equal(sysfs_attr_get_value((void *)state, "bar",
					      buf, sizeof(buf)), -ENOENT);
}

static void test_sagv_read_fail(void **state)
{
	char buf[1];

	will_return(__wrap_udev_device_get_syspath, "/foo");
	expect_condlog(4, "open '/foo/bar'");
	expect_string(WRAP_OPEN, pathname, "/foo/bar");
	expect_int_value(WRAP_OPEN, flags, O_RDONLY);
	wrap_will_return(WRAP_OPEN, TEST_FD);
	expect_int_value(__wrap_read, fd, TEST_FD);
	expect_uint_value(__wrap_read, count, sizeof(buf));
	errno = EISDIR;
	will_return(__wrap_read, -1);
	will_return(__wrap_read, NULL);
	expect_condlog(3, "sysfs_attr_get_value__: read from /foo/bar failed:");
	will_return(__wrap_close, 0);
	assert_int_equal(sysfs_attr_get_value((void *)state, "bar",
					      buf, sizeof(buf)), -EISDIR);

	will_return(__wrap_udev_device_get_syspath, "/foo");
	expect_condlog(4, "open '/foo/baz'");
	expect_string(WRAP_OPEN, pathname, "/foo/baz");
	expect_int_value(WRAP_OPEN, flags, O_RDONLY);
	wrap_will_return(WRAP_OPEN, TEST_FD);
	expect_int_value(__wrap_read, fd, TEST_FD);
	expect_uint_value(__wrap_read, count, sizeof(buf));
	errno = EPERM;
	will_return(__wrap_read, -1);
	will_return(__wrap_read, NULL);
	expect_condlog(3, "sysfs_attr_get_value__: read from /foo/baz failed:");
	will_return(__wrap_close, 0);
	assert_int_equal(sysfs_bin_attr_get_value((void *)state, "baz",
						  (unsigned char *)buf, sizeof(buf)),
			 -EPERM);

}

static void _test_sagv_read(void **state, unsigned int bufsz)
{
	char buf[16];
	char input[] = "01234567";
	unsigned int n, trunc;

	assert_uint_in_range(bufsz, 1, sizeof(buf));
	memset(buf, '.', sizeof(buf));
	will_return(__wrap_udev_device_get_syspath, "/foo");
	expect_condlog(4, "open '/foo/bar'");
	expect_string(WRAP_OPEN, pathname, "/foo/bar");
	expect_int_value(WRAP_OPEN, flags, O_RDONLY);
	wrap_will_return(WRAP_OPEN, TEST_FD);
	expect_int_value(__wrap_read, fd, TEST_FD);
	expect_uint_value(__wrap_read, count, bufsz);
	will_return(__wrap_read, sizeof(input) - 1);
	will_return(__wrap_read, input);

	/* If the buffer is too small, input will be truncated by a 0 byte */
	if (bufsz <= sizeof(input) - 1) {
		n = bufsz;
		trunc = 1;
		expect_condlog(3, "sysfs_attr_get_value__: overflow reading from /foo/bar");
	} else {
		n = sizeof(input) - 1;
		trunc = 0;
	}
	will_return(__wrap_close, 0);
	assert_int_equal(sysfs_attr_get_value((void *)state, "bar",
					      buf, bufsz), n);
	assert_memory_equal(buf, input, n - trunc);
	assert_int_equal(buf[n - trunc], '\0');

	/* Binary input is not truncated */
	memset(buf, '.', sizeof(buf));
	will_return(__wrap_udev_device_get_syspath, "/foo");
	expect_condlog(4, "open '/foo/baz'");
	expect_string(WRAP_OPEN, pathname, "/foo/baz");
	expect_int_value(WRAP_OPEN, flags, O_RDONLY);
	wrap_will_return(WRAP_OPEN, TEST_FD);
	expect_int_value(__wrap_read, fd, TEST_FD);
	expect_uint_value(__wrap_read, count, bufsz);
	will_return(__wrap_read, sizeof(input) - 1);
	will_return(__wrap_read, input);
	will_return(__wrap_close, 0);
	n = bufsz < sizeof(input) - 1 ? bufsz : sizeof(input) - 1;
	assert_int_equal(sysfs_bin_attr_get_value((void *)state, "baz",
						  (unsigned char *)buf,
						  bufsz),
			 n);
	assert_memory_equal(buf, input, n);
}

static void test_sagv_read_overflow_8(void **state)
{
	_test_sagv_read(state, 8);
}

static void test_sagv_read_overflow_4(void **state)
{
	_test_sagv_read(state, 4);
}

static void test_sagv_read_overflow_1(void **state)
{
	_test_sagv_read(state, 1);
}

static void test_sagv_read_good_9(void **state)
{
	_test_sagv_read(state, 9);
}

static void test_sagv_read_good_15(void **state)
{
	_test_sagv_read(state, 15);
}

static void _test_sagv_read_zeroes(void **state, unsigned int bufsz)
{
	char buf[16];
	char input[] = { '\0','\0','\0','\0','\0','\0','\0','\0' };
	unsigned int n;

	assert_uint_in_range(bufsz, 1, sizeof(buf));
	memset(buf, '.', sizeof(buf));
	will_return(__wrap_udev_device_get_syspath, "/foo");
	expect_condlog(4, "open '/foo/bar'");
	expect_string(WRAP_OPEN, pathname, "/foo/bar");
	expect_int_value(WRAP_OPEN, flags, O_RDONLY);
	wrap_will_return(WRAP_OPEN, TEST_FD);
	expect_int_value(__wrap_read, fd, TEST_FD);
	expect_uint_value(__wrap_read, count, bufsz);
	will_return(__wrap_read, sizeof(input) - 1);
	will_return(__wrap_read, input);

	if (bufsz <= sizeof(input) - 1) {
		n = bufsz;
		expect_condlog(3, "sysfs_attr_get_value__: overflow reading from /foo/bar");
	} else
		n = 0;

	will_return(__wrap_close, 0);
	assert_int_equal(sysfs_attr_get_value((void *)state, "bar",
					      buf, bufsz), n);

	/*
	 * The return value of sysfs_attr_get_value ignores zero bytes,
	 * but the read data should have been copied to the buffer
	 */
	assert_memory_equal(buf, input, n == 0 ? bufsz : n);
}

static void test_sagv_read_zeroes_4(void **state)
{
	_test_sagv_read_zeroes(state, 4);
}

static void expect_sasv_invalid(void)
{
	expect_condlog(1, "sysfs_attr_set_value: invalid parameters");
}

static void test_sasv_invalid(void **state)
{
	expect_sasv_invalid();
	assert_int_equal(sysfs_attr_set_value(NULL, NULL, NULL, 0), -EINVAL);

	expect_sasv_invalid();
	assert_int_equal(sysfs_attr_set_value(NULL, (void *)state, (void *)state, 1),
			 -EINVAL);

	expect_sasv_invalid();
	assert_int_equal(sysfs_attr_set_value((void *)state, NULL, (void *)state, 1),
			 -EINVAL);

	expect_sasv_invalid();
	assert_int_equal(sysfs_attr_set_value((void *)state, (void *)state, NULL, 1),
			 -EINVAL);

	expect_sasv_invalid();
	assert_int_equal(sysfs_attr_set_value((void *)state, (void *)state,
					      (void *)state, 0), -EINVAL);
}

static void test_sasv_bad_udev(void **state)
{
	will_return(__wrap_udev_device_get_syspath, NULL);
	expect_condlog(3, "sysfs_attr_set_value: invalid udevice");
	assert_int_equal(sysfs_attr_set_value((void *)state, (void *)state,
					      (void *)state, 1), -EINVAL);
}

static void test_sasv_bad_snprintf(void **state)
{
	char longstr[PATH_MAX + 1];
	char buf[1];

	memset(longstr, 'a', sizeof(longstr) - 1);
	longstr[sizeof(longstr) - 1] = '\0';

	will_return(__wrap_udev_device_get_syspath, "/foo");
	expect_condlog(3, "sysfs_attr_set_value: devpath overflow");
	assert_int_equal(sysfs_attr_set_value((void *)state, longstr,
					      buf, sizeof(buf)), -EOVERFLOW);
}

static void test_sasv_open_fail(void **state)
{
	char buf[1];

	will_return(__wrap_udev_device_get_syspath, "/foo");
	expect_condlog(4, "open '/foo/bar'");
	expect_string(WRAP_OPEN, pathname, "/foo/bar");
	expect_int_value(WRAP_OPEN, flags, O_WRONLY);
	errno = EPERM;
	wrap_will_return(WRAP_OPEN, -1);
	expect_condlog(3, "sysfs_attr_set_value: attribute '/foo/bar' cannot be opened");
	assert_int_equal(sysfs_attr_set_value((void *)state, "bar",
					      buf, sizeof(buf)), -EPERM);
}

static void test_sasv_write_fail(void **state)
{
	char buf[1];

	will_return(__wrap_udev_device_get_syspath, "/foo");
	expect_condlog(4, "open '/foo/bar'");
	expect_string(WRAP_OPEN, pathname, "/foo/bar");
	expect_int_value(WRAP_OPEN, flags, O_WRONLY);
	wrap_will_return(WRAP_OPEN, TEST_FD);
	expect_int_value(__wrap_write, fd, TEST_FD);
	expect_uint_value(__wrap_write, count, sizeof(buf));
	errno = EISDIR;
	will_return(__wrap_write, -1);
	expect_condlog(3, "sysfs_attr_set_value: write to /foo/bar failed:");
	will_return(__wrap_close, 0);
	assert_int_equal(sysfs_attr_set_value((void *)state, "bar",
					      buf, sizeof(buf)), -EISDIR);

}

static void _test_sasv_write(void **state, unsigned int n_written)
{
	char buf[8];

	assert_uint_in_range(n_written, 0, sizeof(buf));
	will_return(__wrap_udev_device_get_syspath, "/foo");
	expect_condlog(4, "open '/foo/bar'");
	expect_string(WRAP_OPEN, pathname, "/foo/bar");
	expect_int_value(WRAP_OPEN, flags, O_WRONLY);
	wrap_will_return(WRAP_OPEN, TEST_FD);
	expect_int_value(__wrap_write, fd, TEST_FD);
	expect_uint_value(__wrap_write, count, sizeof(buf));
	will_return(__wrap_write, n_written);

	if (n_written < sizeof(buf))
		expect_condlog(3, "sysfs_attr_set_value: underflow writing");
	will_return(__wrap_close, 0);
	assert_int_equal(sysfs_attr_set_value((void *)state, "bar",
					      buf, sizeof(buf)),
			 n_written);
}

static void test_sasv_write_0(void **state)
{
	_test_sasv_write(state, 0);
}

static void test_sasv_write_4(void **state)
{
	_test_sasv_write(state, 4);
}

static void test_sasv_write_7(void **state)
{
	_test_sasv_write(state, 7);
}

static void test_sasv_write_8(void **state)
{
	_test_sasv_write(state, 8);
}

static int test_sysfs(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_sagv_invalid),
		cmocka_unit_test(test_sagv_bad_udev),
		cmocka_unit_test(test_sagv_bad_snprintf),
		cmocka_unit_test(test_sagv_open_fail),
		cmocka_unit_test(test_sagv_read_fail),
		cmocka_unit_test(test_sagv_read_overflow_1),
		cmocka_unit_test(test_sagv_read_overflow_4),
		cmocka_unit_test(test_sagv_read_overflow_8),
		cmocka_unit_test(test_sagv_read_good_9),
		cmocka_unit_test(test_sagv_read_good_15),
		cmocka_unit_test(test_sagv_read_zeroes_4),
		cmocka_unit_test(test_sasv_invalid),
		cmocka_unit_test(test_sasv_bad_udev),
		cmocka_unit_test(test_sasv_bad_snprintf),
		cmocka_unit_test(test_sasv_open_fail),
		cmocka_unit_test(test_sasv_write_fail),
		cmocka_unit_test(test_sasv_write_0),
		cmocka_unit_test(test_sasv_write_4),
		cmocka_unit_test(test_sasv_write_7),
		cmocka_unit_test(test_sasv_write_8),
	};

	return cmocka_run_group_tests(tests, setup, teardown);
}

int main(void)
{
	int ret = 0;

	init_test_verbosity(5);
	ret += test_sysfs();
	return ret;
}
