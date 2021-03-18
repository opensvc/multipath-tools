/*
 * Copyright (c) 2020 Martin Wilck, SUSE
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <cmocka.h>
#include <libudev.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include "util.h"
#include "debug.h"

#include "globals.c"

static bool sys_dev_block_exists(void)
{
	DIR *dir;
	bool rc = false;

	dir = opendir("/sys/dev/block");
	if (dir != NULL) {
		struct dirent *de;

		while((de = readdir(dir)) != NULL) {
			if (strcmp(de->d_name, ".") &&
			    strcmp(de->d_name, "..")) {
				rc = true;
				break;
			}
		}
	}
	closedir(dir);
	return rc;
}

static int get_one_devt(char *devt, size_t len)
{
	struct udev_enumerate *enm;
	int r, ret = -1;
	struct udev_list_entry *first;
	struct udev_device *u_dev;
	const char *path;
	dev_t devnum;

	enm = udev_enumerate_new(udev);
	if (!enm)
		return -1;
	r = udev_enumerate_add_match_subsystem(enm, "block");
	r = udev_enumerate_scan_devices(enm);
	if (r < 0)
		goto out;
	first = udev_enumerate_get_list_entry(enm);
	if (!first)
		goto out;
	path = udev_list_entry_get_name(first);
	u_dev = udev_device_new_from_syspath(udev, path);
	if (!u_dev)
		goto out;
	devnum = udev_device_get_devnum(u_dev);
	snprintf(devt, len, "%d:%d",
		 major(devnum), minor(devnum));
	udev_device_unref(u_dev);
	condlog(3, "found block device: %s", devt);
	ret = 0;
out:
	udev_enumerate_unref(enm);
	return ret;
}

int setup(void **state)
{
	static char dev_t[BLK_DEV_SIZE];

	udev = udev_new();
	if (udev == NULL)
		return -1;
	*state = dev_t;
	return get_one_devt(dev_t, sizeof(dev_t));
}

int teardown(void **state)
{
	udev_unref(udev);
	return 0;
}

static void test_devt2devname_devt_good(void **state)
{
	char dummy[BLK_DEV_SIZE];

	if (!sys_dev_block_exists())
		skip();
	assert_int_equal(devt2devname(dummy, sizeof(dummy), *state), 0);
}

static void test_devt2devname_devname_null(void **state)
{
	assert_int_equal(devt2devname(NULL, 0, ""), 1);
}

/* buffer length 0 */
static void test_devt2devname_length_0(void **state)
{
	char dummy[] = "";

	assert_int_equal(devt2devname(dummy, 0, ""), 1);
}

/* buffer too small */
static void test_devt2devname_length_1(void **state)
{
	char dummy[] = "";

	assert_int_equal(devt2devname(dummy, sizeof(dummy), *state), 1);
}

static void test_devt2devname_devt_null(void **state)
{
	char dummy[32];

	assert_int_equal(devt2devname(dummy, sizeof(dummy), NULL), 1);
}

static void test_devt2devname_devt_empty(void **state)
{
	char dummy[32];

	assert_int_equal(devt2devname(dummy, sizeof(dummy), ""), 1);
}

static void test_devt2devname_devt_invalid_1(void **state)
{
	char dummy[32];

	assert_int_equal(devt2devname(dummy, sizeof(dummy), "foo"), 1);
}

static void test_devt2devname_devt_invalid_2(void **state)
{
	char dummy[32];

	assert_int_equal(devt2devname(dummy, sizeof(dummy), "1234"), 1);
}

static void test_devt2devname_devt_invalid_3(void **state)
{
	char dummy[32];

	assert_int_equal(devt2devname(dummy, sizeof(dummy), "0:0"), 1);
}

static void test_devt2devname_real(void **state)
{
	struct udev_enumerate *enm;
	int r;
	struct udev_list_entry *first, *item;
	unsigned int i = 0;

	if (!sys_dev_block_exists())
		skip();
	enm = udev_enumerate_new(udev);
	assert_non_null(enm);
	r = udev_enumerate_add_match_subsystem(enm, "block");
	assert_in_range(r, 0, INT_MAX);
	r = udev_enumerate_scan_devices(enm);
	first = udev_enumerate_get_list_entry(enm);
	udev_list_entry_foreach(item, first) {
		const char *path = udev_list_entry_get_name(item);
		struct udev_device *u_dev;
		dev_t devnum;
		char devt[BLK_DEV_SIZE];
		char devname[FILE_NAME_SIZE];

		u_dev = udev_device_new_from_syspath(udev, path);
		assert_non_null(u_dev);
		devnum = udev_device_get_devnum(u_dev);
		snprintf(devt, sizeof(devt), "%d:%d",
			 major(devnum), minor(devnum));
		r = devt2devname(devname, sizeof(devname), devt);
		assert_int_equal(r, 0);
		assert_string_equal(devname, udev_device_get_sysname(u_dev));
		i++;
		udev_device_unref(u_dev);
	}
	udev_enumerate_unref(enm);
	condlog(2, "devt2devname test passed for %u block devices", i);
}

static int devt2devname_tests(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_devt2devname_devt_good),
		cmocka_unit_test(test_devt2devname_devname_null),
		cmocka_unit_test(test_devt2devname_length_0),
		cmocka_unit_test(test_devt2devname_length_1),
		cmocka_unit_test(test_devt2devname_devt_null),
		cmocka_unit_test(test_devt2devname_devt_empty),
		cmocka_unit_test(test_devt2devname_devt_invalid_1),
		cmocka_unit_test(test_devt2devname_devt_invalid_2),
		cmocka_unit_test(test_devt2devname_devt_invalid_3),
		cmocka_unit_test(test_devt2devname_real),
	};

	return cmocka_run_group_tests(tests, setup, teardown);
}

int main(void)
{
	int ret = 0;

	init_test_verbosity(-1);
	ret += devt2devname_tests();
	return ret;
}
