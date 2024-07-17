/*
 * Copyright (c) 2020 Benjamin Marzinski, Redhat
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
#include <errno.h>
#include <cmocka.h>
#include <sys/sysmacros.h>

#include "globals.c"
#include "util.h"
#include "discovery.h"
#include "wwids.h"
#include "blacklist.h"
#include "foreign.h"
#include "valid.h"

#define PATHINFO_REAL 9999

int test_fd;
struct udev_device {
	int unused;
} test_udev;

bool __wrap_sysfs_is_multipathed(struct path *pp, bool set_wwid)
{
	bool is_multipathed = mock_type(bool);
	assert_non_null(pp);
	assert_int_not_equal(strlen(pp->dev), 0);
	if (is_multipathed && set_wwid)
		strlcpy(pp->wwid, mock_ptr_type(char *), WWID_SIZE);
	return is_multipathed;
}

int __wrap___mpath_connect(int nonblocking)
{
	bool connected = mock_type(bool);
	assert_int_equal(nonblocking, 1);
	if (connected)
		return test_fd;
	errno = mock_type(int);
	return -1;
}

/* There's no point in checking the return value here */
int __wrap_mpath_disconnect(int fd)
{
	assert_int_equal(fd, test_fd);
	return 0;
}

struct udev_device *__wrap_udev_device_new_from_subsystem_sysname(struct udev *udev, const char *subsystem, const char *sysname)
{
	bool passed = mock_type(bool);
	assert_string_equal(sysname, mock_ptr_type(char *));
	if (passed)
		return &test_udev;
	return NULL;
}

/* For devtype check */
const char *__wrap_udev_device_get_property_value(struct udev_device *udev_device, const char *property)
{
	check_expected(property);
	return mock_ptr_type(char *);
}

/* For the "hidden" check in pathinfo() */
const char *__wrap_udev_device_get_sysattr_value(struct udev_device *udev_device,
					 const char *sysattr)
{
	check_expected(sysattr);
	return mock_ptr_type(char *);
}

/* For pathinfo() -> is_claimed_by_foreign() */
int __wrap_add_foreign(struct udev_device *udev_device)
{
	return mock_type(int);
}

/* For is_device_used() */
const char *__wrap_udev_device_get_sysname(struct udev_device *udev_device)
{
	return mock_ptr_type(char *);
}

/* called from pathinfo() */
int __wrap_filter_devnode(struct config *conf, const struct _vector *elist,
			  const char *vendor, const char * product, const char *dev)
{
	return mock_type(int);
}

/* called from pathinfo() */
int __wrap_filter_device(const struct _vector *blist, const struct _vector *elist,
	       const char *vendor, const char * product, const char *dev)
{
	return mock_type(int);
}

/* for common_sysfs_pathinfo() */
dev_t __wrap_udev_device_get_devnum(struct udev_device *ud)
{
	return  mock_type(dev_t);
}

/* for common_sysfs_pathinfo() */
int __wrap_sysfs_get_size(struct path *pp, unsigned long long * size)
{
	return mock_type(int);
}

/* called in pathinfo() before filter_property() */
int __wrap_select_getuid(struct config *conf, struct path *pp)
{
	pp->uid_attribute = mock_ptr_type(char *);
	return 0;
}

int __real_pathinfo(struct path *pp, struct config *conf, int mask);

int __wrap_pathinfo(struct path *pp, struct config *conf, int mask)
{
	int ret = mock_type(int);

	assert_string_equal(pp->dev, mock_ptr_type(char *));
	assert_int_equal(mask, DI_SYSFS | DI_WWID | DI_BLACKLIST);
	if (ret == PATHINFO_REAL) {
		/* for test_filter_property() */
		ret =  __real_pathinfo(pp, conf, mask);
		return ret;
	} else if (ret == PATHINFO_OK) {
		pp->uid_attribute = "ID_TEST";
		strlcpy(pp->wwid, mock_ptr_type(char *), WWID_SIZE);
	} else
		memset(pp->wwid, 0, WWID_SIZE);
	return ret;
}

int __wrap_filter_property(struct config *conf, struct udev_device *udev,
			   int lvl, const char *uid_attribute)
{
	int ret = mock_type(int);
	assert_string_equal(uid_attribute, "ID_TEST");
	return ret;
}

int __wrap_is_failed_wwid(const char *wwid)
{
	int ret = mock_type(int);
	assert_string_equal(wwid, mock_ptr_type(char *));
	return ret;
}

const char *__wrap_udev_device_get_syspath(struct udev_device *udevice)
{
	return mock_ptr_type(char *);
}

int __wrap_check_wwids_file(char *wwid, int write_wwid)
{
	bool passed = mock_type(bool);
	assert_int_equal(write_wwid, 0);
	assert_string_equal(wwid, mock_ptr_type(char *));
	if (passed)
		return 0;
	else
		return -1;
}

int __wrap_dm_find_map_by_wwid(const char *wwid, char *name,
			       struct dm_info *dmi)
{
	int ret = mock_type(int);
	assert_string_equal(wwid, mock_ptr_type(char *));
	return ret;
}

enum {
	STAGE_IS_MULTIPATHED,
	STAGE_CHECK_MULTIPATHD,
	STAGE_GET_UDEV_DEVICE,
	STAGE_PATHINFO_REAL,
	STAGE_PATHINFO,
	STAGE_FILTER_PROPERTY,
	STAGE_IS_FAILED,
	STAGE_CHECK_WWIDS,
	STAGE_UUID_PRESENT,
};

enum {
	CHECK_MPATHD_RUNNING,
	CHECK_MPATHD_EAGAIN,
	CHECK_MPATHD_SKIP,
};

/* setup the test to continue past the given stage in is_path_valid() */
static void setup_passing(char *name, char *wwid, unsigned int check_multipathd,
			  unsigned int stage)
{
	will_return(__wrap_sysfs_is_multipathed, false);
	if (stage == STAGE_IS_MULTIPATHED)
		return;
	if (check_multipathd == CHECK_MPATHD_RUNNING)
		will_return(__wrap___mpath_connect, true);
	else if (check_multipathd == CHECK_MPATHD_EAGAIN) {
		will_return(__wrap___mpath_connect, false);
		will_return(__wrap___mpath_connect, EAGAIN);
	}

	/* nothing for CHECK_MPATHD_SKIP */
	if (stage == STAGE_CHECK_MULTIPATHD)
		return;
	will_return(__wrap_udev_device_new_from_subsystem_sysname, true);
	will_return(__wrap_udev_device_new_from_subsystem_sysname,
		    name);
	expect_string(__wrap_udev_device_get_property_value, property, "DEVTYPE");
	will_return(__wrap_udev_device_get_property_value, "disk");
	if (stage == STAGE_GET_UDEV_DEVICE)
		return;
	if  (stage == STAGE_PATHINFO_REAL) {
		/* special case for test_filter_property() */
		will_return(__wrap_pathinfo, PATHINFO_REAL);
		will_return(__wrap_pathinfo, name);
		expect_string(__wrap_udev_device_get_sysattr_value,
			      sysattr, "hidden");
		will_return(__wrap_udev_device_get_sysattr_value, NULL);
		will_return(__wrap_add_foreign, FOREIGN_IGNORED);
		will_return(__wrap_filter_devnode, MATCH_NOTHING);
		will_return(__wrap_udev_device_get_devnum, makedev(259, 0));
		will_return(__wrap_sysfs_get_size, 0);
		will_return(__wrap_select_getuid, "ID_TEST");
		return;
	}
	will_return(__wrap_pathinfo, PATHINFO_OK);
	will_return(__wrap_pathinfo, name);
	will_return(__wrap_pathinfo, wwid);
	if (stage == STAGE_PATHINFO)
		return;
	if (stage == STAGE_FILTER_PROPERTY)
		return;
	will_return(__wrap_is_failed_wwid, WWID_IS_NOT_FAILED);
	will_return(__wrap_is_failed_wwid, wwid);
	/* avoid real is_device_in_use() check */
	if (conf.find_multipaths == FIND_MULTIPATHS_GREEDY ||
	    conf.find_multipaths == FIND_MULTIPATHS_SMART)
		will_return(__wrap_udev_device_get_syspath, NULL);
	if (stage == STAGE_IS_FAILED)
		return;
	will_return(__wrap_check_wwids_file, false);
	will_return(__wrap_check_wwids_file, wwid);
	if (stage == STAGE_CHECK_WWIDS)
		return;
	will_return(__wrap_dm_find_map_by_wwid, 0);
	will_return(__wrap_dm_find_map_by_wwid, wwid);
}

static void test_bad_arguments(void **state)
{
	struct path pp;
	char too_long[FILE_NAME_SIZE + 1];

	memset(&pp, 0, sizeof(pp));
	/* test NULL pointers */
	assert_int_equal(is_path_valid("test", &conf, NULL, true),
			 PATH_IS_ERROR);
	assert_int_equal(is_path_valid("test", NULL, &pp, true),
			 PATH_IS_ERROR);
	assert_int_equal(is_path_valid(NULL, &conf, &pp, true),
			 PATH_IS_ERROR);
	/* test undefined find_multipaths */
	conf.find_multipaths = FIND_MULTIPATHS_UNDEF;
	assert_int_equal(is_path_valid("test", &conf, &pp, true),
			 PATH_IS_ERROR);
	/* test name too long */
	memset(too_long, 'x', sizeof(too_long));
	too_long[sizeof(too_long) - 1] = '\0';
	conf.find_multipaths = FIND_MULTIPATHS_STRICT;
	assert_int_equal(is_path_valid(too_long, &conf, &pp, true),
			 PATH_IS_ERROR);
}

static void test_sysfs_is_multipathed(void **state)
{
	struct path pp;
	char *name = "test";
	char *wwid = "test_wwid";

	memset(&pp, 0, sizeof(pp));
	conf.find_multipaths = FIND_MULTIPATHS_STRICT;
	/* test for already existing multipathed device */
	will_return(__wrap_sysfs_is_multipathed, true);
	will_return(__wrap_sysfs_is_multipathed, wwid);
	assert_int_equal(is_path_valid(name, &conf, &pp, true),
			 PATH_IS_VALID_NO_CHECK);
	assert_string_equal(pp.dev, name);
	assert_string_equal(pp.wwid, wwid);
	/* test for wwid device with empty wwid */
	will_return(__wrap_sysfs_is_multipathed, true);
	will_return(__wrap_sysfs_is_multipathed, "");
	assert_int_equal(is_path_valid(name, &conf, &pp, true),
			 PATH_IS_ERROR);
}

static void test_check_multipathd(void **state)
{
	struct path pp;
	char *name = "test";

	memset(&pp, 0, sizeof(pp));
	conf.find_multipaths = FIND_MULTIPATHS_STRICT;
	/* test failed check to see if multipathd is active */
	will_return(__wrap_sysfs_is_multipathed, false);
	will_return(__wrap___mpath_connect, false);
	will_return(__wrap___mpath_connect, ECONNREFUSED);

	assert_int_equal(is_path_valid(name, &conf, &pp, true),
			 PATH_IS_NOT_VALID);
	assert_string_equal(pp.dev, name);
	/* test pass because connect returned EAGAIN. fail getting udev */
	setup_passing(name, NULL, CHECK_MPATHD_EAGAIN, STAGE_CHECK_MULTIPATHD);
	will_return(__wrap_udev_device_new_from_subsystem_sysname, false);
	will_return(__wrap_udev_device_new_from_subsystem_sysname,
		    name);
	assert_int_equal(is_path_valid(name, &conf, &pp, true),
			 PATH_IS_ERROR);
	/* test pass because connect succeeded. fail getting udev */
	memset(&pp, 0, sizeof(pp));
	setup_passing(name, NULL, CHECK_MPATHD_RUNNING, STAGE_CHECK_MULTIPATHD);
	will_return(__wrap_udev_device_new_from_subsystem_sysname, false);
	will_return(__wrap_udev_device_new_from_subsystem_sysname,
		    name);
	assert_int_equal(is_path_valid(name, &conf, &pp, true),
			 PATH_IS_ERROR);
	assert_string_equal(pp.dev, name);

	/* test pass because connect succeeded. succeed getting udev. Wrong DEVTYPE  */
	memset(&pp, 0, sizeof(pp));
	setup_passing(name, NULL, CHECK_MPATHD_RUNNING, STAGE_CHECK_MULTIPATHD);
	will_return(__wrap_udev_device_new_from_subsystem_sysname, true);
	will_return(__wrap_udev_device_new_from_subsystem_sysname,
		    name);
	expect_string(__wrap_udev_device_get_property_value, property, "DEVTYPE");
	will_return(__wrap_udev_device_get_property_value, "partition");
	assert_int_equal(is_path_valid(name, &conf, &pp, true),
			 PATH_IS_NOT_VALID);
	assert_string_equal(pp.dev, name);

	/* test pass because connect succeeded. succeed getting udev. Bad DEVTYPE  */
	memset(&pp, 0, sizeof(pp));
	setup_passing(name, NULL, CHECK_MPATHD_RUNNING, STAGE_CHECK_MULTIPATHD);
	will_return(__wrap_udev_device_new_from_subsystem_sysname, true);
	will_return(__wrap_udev_device_new_from_subsystem_sysname,
		    name);
	expect_string(__wrap_udev_device_get_property_value, property, "DEVTYPE");
	will_return(__wrap_udev_device_get_property_value, NULL);
	assert_int_equal(is_path_valid(name, &conf, &pp, true),
			 PATH_IS_NOT_VALID);
	assert_string_equal(pp.dev, name);
}

static void test_pathinfo(void **state)
{
	struct path pp;
	char *name = "test";

	memset(&pp, 0, sizeof(pp));
	conf.find_multipaths = FIND_MULTIPATHS_STRICT;
	/* Test pathinfo blacklisting device */
	setup_passing(name, NULL, CHECK_MPATHD_SKIP, STAGE_GET_UDEV_DEVICE);
	will_return(__wrap_pathinfo, PATHINFO_SKIPPED);
	will_return(__wrap_pathinfo, name);
	assert_int_equal(is_path_valid(name, &conf, &pp, false),
			 PATH_IS_NOT_VALID);
	assert_string_equal(pp.dev, name);
	assert_ptr_equal(pp.udev, &test_udev);
	/* Test pathinfo failing */
	memset(&pp, 0, sizeof(pp));
	setup_passing(name, NULL, CHECK_MPATHD_SKIP, STAGE_GET_UDEV_DEVICE);
	will_return(__wrap_pathinfo, PATHINFO_FAILED);
	will_return(__wrap_pathinfo, name);
	assert_int_equal(is_path_valid(name, &conf, &pp, false),
			 PATH_IS_ERROR);
	/* Test blank wwid */
	memset(&pp, 0, sizeof(pp));
	setup_passing(name, NULL, CHECK_MPATHD_SKIP, STAGE_GET_UDEV_DEVICE);
	will_return(__wrap_pathinfo, PATHINFO_OK);
	will_return(__wrap_pathinfo, name);
	will_return(__wrap_pathinfo, "");
	assert_int_equal(is_path_valid(name, &conf, &pp, false),
			 PATH_IS_NOT_VALID);
}

static void test_filter_property(void **state)
{
	struct path pp;
	char *name = "test";
	char *wwid = "test-wwid";

	/* test blacklist property */
	memset(&pp, 0, sizeof(pp));
	conf.find_multipaths = FIND_MULTIPATHS_STRICT;
	setup_passing(name, wwid, CHECK_MPATHD_SKIP, STAGE_PATHINFO_REAL);
	will_return(__wrap_filter_property, MATCH_PROPERTY_BLIST);
	assert_int_equal(is_path_valid(name, &conf, &pp, false),
			 PATH_IS_NOT_VALID);
	assert_ptr_equal(pp.udev, &test_udev);

	/* test missing property */
	memset(&pp, 0, sizeof(pp));
	setup_passing(name, wwid, CHECK_MPATHD_SKIP, STAGE_PATHINFO_REAL);
	will_return(__wrap_filter_property, MATCH_PROPERTY_BLIST_MISSING);
	assert_int_equal(is_path_valid(name, &conf, &pp, false),
			 PATH_IS_NOT_VALID);

	/* test MATCH_NOTHING fail on filter_device */
	memset(&pp, 0, sizeof(pp));
	setup_passing(name, wwid, CHECK_MPATHD_SKIP, STAGE_PATHINFO_REAL);
	will_return(__wrap_filter_property, MATCH_NOTHING);
	will_return(__wrap_filter_device, MATCH_DEVICE_BLIST);
	assert_int_equal(is_path_valid(name, &conf, &pp, false),
			 PATH_IS_NOT_VALID);
}

static void test_is_failed_wwid(void **state)
{
	struct path pp;
	char *name = "test";
	char *wwid = "test-wwid";

	memset(&pp, 0, sizeof(pp));
	conf.find_multipaths = FIND_MULTIPATHS_STRICT;
	/* Test wwid failed */
	setup_passing(name, wwid, CHECK_MPATHD_SKIP, STAGE_FILTER_PROPERTY);
	will_return(__wrap_is_failed_wwid, WWID_IS_FAILED);
	will_return(__wrap_is_failed_wwid, wwid);
	assert_int_equal(is_path_valid(name, &conf, &pp, false),
			 PATH_IS_NOT_VALID);
	assert_string_equal(pp.dev, name);
	assert_ptr_equal(pp.udev, &test_udev);
	assert_string_equal(pp.wwid, wwid);
	/* test is_failed_wwid error */
	setup_passing(name, wwid, CHECK_MPATHD_SKIP, STAGE_FILTER_PROPERTY);
	will_return(__wrap_is_failed_wwid, WWID_FAILED_ERROR);
	will_return(__wrap_is_failed_wwid, wwid);
	assert_int_equal(is_path_valid(name, &conf, &pp, false),
			 PATH_IS_ERROR);
}

static void test_greedy(void **state)
{
	struct path pp;
	char *name = "test";
	char *wwid = "test-wwid";

	/* test greedy success with checking multipathd */
	memset(&pp, 0, sizeof(pp));
	conf.find_multipaths = FIND_MULTIPATHS_GREEDY;
	setup_passing(name, wwid, CHECK_MPATHD_RUNNING, STAGE_IS_FAILED);
	assert_int_equal(is_path_valid(name, &conf, &pp, true),
			 PATH_IS_VALID);
	assert_string_equal(pp.dev, name);
	assert_ptr_equal(pp.udev, &test_udev);
	assert_string_equal(pp.wwid, wwid);
	/* test greedy success without checking multipathd */
	memset(&pp, 0, sizeof(pp));
	setup_passing(name, wwid, CHECK_MPATHD_SKIP, STAGE_IS_FAILED);
	assert_int_equal(is_path_valid(name, &conf, &pp, false),
			 PATH_IS_VALID);
}

static void test_check_wwids(void **state)
{
	struct path pp;
	char *name = "test";
	char *wwid = "test-wwid";

	memset(&pp, 0, sizeof(pp));
	conf.find_multipaths = FIND_MULTIPATHS_STRICT;
	setup_passing(name, wwid, CHECK_MPATHD_EAGAIN, STAGE_IS_FAILED);
	will_return(__wrap_check_wwids_file, true);
	will_return(__wrap_check_wwids_file, wwid);
	assert_int_equal(is_path_valid(name, &conf, &pp, true),
			 PATH_IS_VALID_NO_CHECK);
	assert_string_equal(pp.dev, name);
	assert_ptr_equal(pp.udev, &test_udev);
	assert_string_equal(pp.wwid, wwid);
}

static void test_check_uuid_present(void **state)
{
	struct path pp;
	char *name = "test";
	char *wwid = "test-wwid";

	memset(&pp, 0, sizeof(pp));
	conf.find_multipaths = FIND_MULTIPATHS_STRICT;
	setup_passing(name, wwid, CHECK_MPATHD_RUNNING, STAGE_CHECK_WWIDS);
	will_return(__wrap_dm_find_map_by_wwid, 1);
	will_return(__wrap_dm_find_map_by_wwid, wwid);
	assert_int_equal(is_path_valid(name, &conf, &pp, true),
			 PATH_IS_VALID);
	assert_string_equal(pp.dev, name);
	assert_ptr_equal(pp.udev, &test_udev);
	assert_string_equal(pp.wwid, wwid);
}


static void test_find_multipaths(void **state)
{
	struct path pp;
	char *name = "test";
	char *wwid = "test-wwid";

	/* test find_multipaths = FIND_MULTIPATHS_STRICT */
	memset(&pp, 0, sizeof(pp));
	conf.find_multipaths = FIND_MULTIPATHS_STRICT;
	setup_passing(name, wwid, CHECK_MPATHD_SKIP, STAGE_UUID_PRESENT);
	assert_int_equal(is_path_valid(name, &conf, &pp, false),
			 PATH_IS_NOT_VALID);
	assert_string_equal(pp.dev, name);
	assert_ptr_equal(pp.udev, &test_udev);
	assert_string_equal(pp.wwid, wwid);
	/* test find_multipaths = FIND_MULTIPATHS_OFF */
	memset(&pp, 0, sizeof(pp));
	conf.find_multipaths = FIND_MULTIPATHS_OFF;
	setup_passing(name, wwid, CHECK_MPATHD_SKIP, STAGE_UUID_PRESENT);
	assert_int_equal(is_path_valid(name, &conf, &pp, false),
			 PATH_IS_NOT_VALID);
	/* test find_multipaths = FIND_MULTIPATHS_ON */
	memset(&pp, 0, sizeof(pp));
	conf.find_multipaths = FIND_MULTIPATHS_ON;
	setup_passing(name, wwid, CHECK_MPATHD_SKIP, STAGE_UUID_PRESENT);
	assert_int_equal(is_path_valid(name, &conf, &pp, false),
			 PATH_IS_NOT_VALID);
	/* test find_multipaths = FIND_MULTIPATHS_SMART */
	memset(&pp, 0, sizeof(pp));
	conf.find_multipaths = FIND_MULTIPATHS_SMART;
	setup_passing(name, wwid, CHECK_MPATHD_SKIP, STAGE_UUID_PRESENT);
	assert_int_equal(is_path_valid(name, &conf, &pp, false),
			 PATH_IS_MAYBE_VALID);
	assert_string_equal(pp.dev, name);
	assert_ptr_equal(pp.udev, &test_udev);
	assert_string_equal(pp.wwid, wwid);
}

int test_valid(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_bad_arguments),
		cmocka_unit_test(test_sysfs_is_multipathed),
		cmocka_unit_test(test_check_multipathd),
		cmocka_unit_test(test_pathinfo),
		cmocka_unit_test(test_filter_property),
		cmocka_unit_test(test_is_failed_wwid),
		cmocka_unit_test(test_greedy),
		cmocka_unit_test(test_check_wwids),
		cmocka_unit_test(test_check_uuid_present),
		cmocka_unit_test(test_find_multipaths),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}

int main(void)
{
	int ret = 0;

	init_test_verbosity(-1);
	ret += test_valid();
	return ret;
}
