/*
 * Copyright (c) 2018 SUSE Linux GmbH
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

#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <cmocka.h>
#include "list.h"
#include "uevent.h"

#include "globals.c"

/* Private prototypes missing in uevent.h */
struct uevent * alloc_uevent(void);
void uevent_get_wwid(struct uevent *uev);

/* Stringify helpers */
#define _str_(x) #x
#define str(x) _str_(x)

#define MAJOR 17
#define MINOR 217
#define DISK_RO 0
#define DM_NAME "spam"
#define WWID "foo"

static int setup_uev(void **state)
{
	static char test_uid_attrs[] =
		"dasd:ID_SPAM   sd:ID_BOGUS nvme:ID_EGGS    ";

	struct uevent *uev = alloc_uevent();
	struct config *conf;

	if (uev == NULL)
		return -1;

	*state = uev;
	uev->kernel = "sdo";
	uev->envp[0] = "MAJOR=" str(MAJOR);
	uev->envp[1] = "ID_SPAM=nonsense";
	uev->envp[1] = "ID_BOGUS=" WWID;
	uev->envp[2] = "MINOR=" str(MINOR);
	uev->envp[3] = "DM_NAME=" DM_NAME;
	uev->envp[4] = "DISK_RO=" str(DISK_RO);
	uev->envp[5] = NULL;

	conf = get_multipath_config();
	parse_uid_attrs(test_uid_attrs, conf);
	put_multipath_config(conf);
	return 0;
}

static int teardown(void **state)
{
	free(*state);
	return 0;
}

static void test_major_good(void **state)
{
	struct uevent *uev = *state;

	assert_int_equal(uevent_get_major(uev), MAJOR);
}

static void test_minor_good(void **state)
{
	struct uevent *uev = *state;

	assert_int_equal(uevent_get_minor(uev), MINOR);
}

static void test_ro_good(void **state)
{
	struct uevent *uev = *state;

	assert_int_equal(uevent_get_disk_ro(uev), DISK_RO);
}

static void test_uid_attrs(void **state)
{
	/* see test_uid_attrs above */
	struct config *conf = get_multipath_config();
	vector attrs = &conf->uid_attrs;

	assert_int_equal(VECTOR_SIZE(attrs), 3);
	assert_null(get_uid_attribute_by_attrs(conf, "hda"));
	assert_string_equal("ID_BOGUS",
			    get_uid_attribute_by_attrs(conf, "sdaw"));
	assert_string_equal("ID_SPAM",
			    get_uid_attribute_by_attrs(conf, "dasdu"));
	assert_string_equal("ID_EGGS",
			    get_uid_attribute_by_attrs(conf, "nvme2n4"));
	put_multipath_config(conf);
}

static void test_wwid(void **state)
{
	struct uevent *uev = *state;
	uevent_get_wwid(uev);

	assert_string_equal(uev->wwid, WWID);
}

static void test_major_bad_0(void **state)
{
	struct uevent *uev = *state;

	uev->envp[0] = "MAJOR" str(MAJOR);
	assert_int_equal(uevent_get_major(uev), -1);
}

static void test_major_bad_1(void **state)
{
	struct uevent *uev = *state;

	uev->envp[0] = "MAJOr=" str(MAJOR);
	assert_int_equal(uevent_get_major(uev), -1);
}

static void test_major_bad_2(void **state)
{
	struct uevent *uev = *state;

	uev->envp[0] = "MAJORIE=" str(MAJOR);
	assert_int_equal(uevent_get_major(uev), -1);
}

static void test_major_bad_3(void **state)
{
	struct uevent *uev = *state;

	uev->envp[0] = "MAJOR=max";
	assert_int_equal(uevent_get_major(uev), -1);
}

static void test_major_bad_4(void **state)
{
	struct uevent *uev = *state;

	uev->envp[0] = "MAJOR=0x10";
	assert_int_equal(uevent_get_major(uev), -1);
}

static void test_major_bad_5(void **state)
{
	struct uevent *uev = *state;

	uev->envp[0] = "MAJO=" str(MAJOR);
	assert_int_equal(uevent_get_major(uev), -1);
}

static void test_major_bad_6(void **state)
{
	struct uevent *uev = *state;

	uev->envp[0] = "MAJOR=" str(-MAJOR);
	assert_int_equal(uevent_get_major(uev), -1);
}

static void test_major_bad_7(void **state)
{
	struct uevent *uev = *state;

	uev->envp[0] = "MAJOR=";
	assert_int_equal(uevent_get_major(uev), -1);
}

static void test_major_bad_8(void **state)
{
	struct uevent *uev = *state;

	uev->envp[0] = "MAJOR";
	assert_int_equal(uevent_get_major(uev), -1);
}

static void test_dm_name_good(void **state)
{
	struct uevent *uev = *state;
	char *name = uevent_get_dm_name(uev);

	assert_string_equal(name, DM_NAME);
	FREE(name);
}

static void test_dm_name_bad_0(void **state)
{
	struct uevent *uev = *state;
	char *name;

	uev->envp[3] = "DM_NAME" DM_NAME;
	name = uevent_get_dm_name(uev);
	assert_ptr_equal(name, NULL);
	FREE(name);
}

static void test_dm_name_bad_1(void **state)
{
	struct uevent *uev = *state;
	char *name;

	uev->envp[3] = "DM_NAMES=" DM_NAME;
	name = uevent_get_dm_name(uev);
	assert_ptr_equal(name, NULL);
	FREE(name);
}

static void test_dm_name_good_1(void **state)
{
	struct uevent *uev = *state;
	char *name;

	/* Note we change index 2 here */
	uev->envp[2] = "DM_NAME=" DM_NAME;
	name = uevent_get_dm_name(uev);
	assert_string_equal(name, DM_NAME);
	FREE(name);
}

static void test_dm_uuid_false_0(void **state)
{
	struct uevent *uev = *state;

	assert_false(uevent_is_mpath(uev));
}

static void test_dm_uuid_true_0(void **state)
{
	struct uevent *uev = *state;

	uev->envp[3] = "DM_UUID=mpath-foo";
	assert_true(uevent_is_mpath(uev));
}

static void test_dm_uuid_false_1(void **state)
{
	struct uevent *uev = *state;

	uev->envp[3] = "DM_UUID.mpath-foo";
	assert_false(uevent_is_mpath(uev));
}

static void test_dm_uuid_false_2(void **state)
{
	struct uevent *uev = *state;

	uev->envp[3] = "DM_UUID=mpath-";
	assert_false(uevent_is_mpath(uev));
}

static void test_dm_uuid_false_3(void **state)
{
	struct uevent *uev = *state;

	uev->envp[3] = "DM_UU=mpath-foo";
	assert_false(uevent_is_mpath(uev));
}

static void test_dm_uuid_false_4(void **state)
{
	struct uevent *uev = *state;

	uev->envp[3] = "DM_UUID=mpathfoo";
	assert_false(uevent_is_mpath(uev));
}

static void test_dm_uuid_false_5(void **state)
{
	struct uevent *uev = *state;

	uev->envp[3] = "DM_UUID=";
	assert_false(uevent_is_mpath(uev));
}

int test_uevent_get_XXX(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_major_good),
		cmocka_unit_test(test_minor_good),
		cmocka_unit_test(test_ro_good),
		cmocka_unit_test(test_dm_name_good),
		cmocka_unit_test(test_uid_attrs),
		cmocka_unit_test(test_wwid),
		cmocka_unit_test(test_major_bad_0),
		cmocka_unit_test(test_major_bad_1),
		cmocka_unit_test(test_major_bad_2),
		cmocka_unit_test(test_major_bad_3),
		cmocka_unit_test(test_major_bad_4),
		cmocka_unit_test(test_major_bad_5),
		cmocka_unit_test(test_major_bad_6),
		cmocka_unit_test(test_major_bad_7),
		cmocka_unit_test(test_major_bad_8),
		cmocka_unit_test(test_dm_name_bad_0),
		cmocka_unit_test(test_dm_name_bad_1),
		cmocka_unit_test(test_dm_name_good_1),
		cmocka_unit_test(test_dm_uuid_false_0),
		cmocka_unit_test(test_dm_uuid_true_0),
		cmocka_unit_test(test_dm_uuid_false_1),
		cmocka_unit_test(test_dm_uuid_false_2),
		cmocka_unit_test(test_dm_uuid_false_3),
		cmocka_unit_test(test_dm_uuid_false_4),
		cmocka_unit_test(test_dm_uuid_false_5),
	};
	return cmocka_run_group_tests(tests, setup_uev, teardown);
}

int main(void)
{
	int ret = 0;

	ret += test_uevent_get_XXX();
	return ret;
}
