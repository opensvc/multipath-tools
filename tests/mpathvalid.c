/*
 * Copyright (c) 2020 Benjamin Marzinski, Red Hat
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <unistd.h>
#include "mt-udev-wrap.h"
#include "cmocka-compat.h"
#include "structs.h"
#include "config.h"
#include "mpath_valid.h"
#include "util.h"
#include "debug.h"

const char *test_dev = "test_name";
#define TEST_WWID "WWID_123"
#define CONF_TEMPLATE "mpathvalid-testconf-XXXXXXXX"
char conf_name[] = CONF_TEMPLATE;
bool initialized;

#if 0
static int mode_to_findmp(unsigned int mode)
{
	switch (mode) {
	case MPATH_SMART:
		return FIND_MULTIPATHS_SMART;
	case MPATH_GREEDY:
		return FIND_MULTIPATHS_GREEDY;
	case MPATH_STRICT:
		return FIND_MULTIPATHS_STRICT;
	}
	fail_msg("invalid mode: %u", mode);
	return FIND_MULTIPATHS_UNDEF;
}
#endif

static unsigned int findmp_to_mode(int findmp)
{
	switch (findmp) {
	case FIND_MULTIPATHS_SMART:
		return MPATH_SMART;
	case FIND_MULTIPATHS_GREEDY:
		return MPATH_GREEDY;
	case FIND_MULTIPATHS_STRICT:
	case FIND_MULTIPATHS_OFF:
	case FIND_MULTIPATHS_ON:
		return MPATH_STRICT;
	}
	fail_msg("invalid find_multipaths value: %d", findmp);
	return MPATH_DEFAULT;
}

int __wrap_is_path_valid(const char *name, struct config *conf, struct path *pp,
			 bool check_multipathd)
{
	int r = mock_type(int);
	int findmp = mock_type(int);

	assert_ptr_equal(name, test_dev);
	assert_ptr_not_equal(conf, NULL);
	assert_ptr_not_equal(pp, NULL);
	assert_true(check_multipathd);

	assert_int_equal(findmp, conf->find_multipaths);
	if (r == MPATH_IS_ERROR || r == MPATH_IS_NOT_VALID)
		return r;

	strlcpy(pp->wwid, mock_ptr_type(const char *), WWID_SIZE);
	return r;
}

int __wrap_libmultipath_init(void)
{
	int r = mock_type(int);

	assert_false(initialized);
	if (r != 0)
		return r;
	initialized = true;
	return 0;
}

void __wrap_libmultipath_exit(void)
{
	assert_true(initialized);
	initialized = false;
}

int __wrap_dm_prereq(unsigned int *v)
{
	assert_ptr_not_equal(v, NULL);
	return mock_type(int);
}

int __real_init_config(const char *file);

int __wrap_init_config(const char *file)
{
	int r = mock_type(int);
	struct config *conf;

	assert_string_equal(file, DEFAULT_CONFIGFILE);
	if (r != 0)
		return r;

	assert_string_not_equal(conf_name, CONF_TEMPLATE);
	r = __real_init_config(conf_name);
	conf = get_multipath_config();
	assert_ptr_not_equal(conf, NULL);
	assert_int_equal(conf->find_multipaths, mock_type(int));
	return 0;
}

static const char * const find_multipaths_optvals[] = {
        [FIND_MULTIPATHS_OFF] = "off",
        [FIND_MULTIPATHS_ON] = "on",
        [FIND_MULTIPATHS_STRICT] = "strict",
        [FIND_MULTIPATHS_GREEDY] = "greedy",
        [FIND_MULTIPATHS_SMART] = "smart",
};

void make_config_file(int findmp)
{
	int r, fd;
	char buf[64];

	assert_true(findmp > FIND_MULTIPATHS_UNDEF &&
		    findmp < FIND_MULTIPATHS_LAST__);

	r = snprintf(buf, sizeof(buf), "defaults {\nfind_multipaths %s\n}\n",
		     find_multipaths_optvals[findmp]);
	assert_true(r > 0 && (long unsigned int)r < sizeof(buf));

	memcpy(conf_name, CONF_TEMPLATE, sizeof(conf_name));
	fd = mkstemp(conf_name);
	assert_true(fd >= 0);
	assert_int_equal(safe_write(fd, buf, r), 0);
	assert_int_equal(close(fd), 0);
}

int setup(void **state)
{
	initialized = false;
	udev = udev_new();
	if (udev == NULL)
		return -1;
	return 0;
}

int teardown(void **state)
{
	struct config *conf;
	conf = get_multipath_config();
	put_multipath_config(conf);
	if (conf)
		uninit_config();
	if (strcmp(conf_name, CONF_TEMPLATE) != 0)
		unlink(conf_name);
	udev_unref(udev);
	udev = NULL;
	return 0;
}

static void check_config(bool valid_config)
{
	struct config *conf;

	conf = get_multipath_config();
	put_multipath_config(conf);
	if (valid_config)
		assert_ptr_not_equal(conf, NULL);
}

/* libmultipath_init fails */
static void test_mpathvalid_init_bad1(void **state)
{
	will_return(__wrap_libmultipath_init, 1);
	assert_int_equal(mpathvalid_init(MPATH_LOG_PRIO_DEBUG,
					 MPATH_LOG_STDERR), -1);
	assert_false(initialized);
	check_config(false);
}

/* init_config fails */
static void test_mpathvalid_init_bad2(void **state)
{
	will_return(__wrap_libmultipath_init, 0);
	will_return(__wrap_init_config, 1);
	assert_int_equal(mpathvalid_init(MPATH_LOG_PRIO_ERR,
					 MPATH_LOG_STDERR_TIMESTAMP), -1);
	assert_false(initialized);
	check_config(false);
}

/* dm_prereq fails */
static void test_mpathvalid_init_bad3(void **state)
{
	make_config_file(FIND_MULTIPATHS_STRICT);
	will_return(__wrap_libmultipath_init, 0);
	will_return(__wrap_init_config, 0);
	will_return(__wrap_init_config, FIND_MULTIPATHS_STRICT);
	will_return(__wrap_dm_prereq, 1);
	assert_int_equal(mpathvalid_init(MPATH_LOG_STDERR, MPATH_LOG_PRIO_ERR),
			 -1);
	assert_false(initialized);
	check_config(false);
}

static void check_mpathvalid_init(int findmp, int prio, int log_style)
{
	make_config_file(findmp);
	will_return(__wrap_libmultipath_init, 0);
	will_return(__wrap_init_config, 0);
	will_return(__wrap_init_config, findmp);
	will_return(__wrap_dm_prereq, 0);
	assert_int_equal(mpathvalid_init(prio, log_style), 0);	
	assert_true(initialized);
	check_config(true);
	assert_int_equal(logsink, log_style);
	assert_int_equal(libmp_verbosity, prio);
	assert_uint_equal(findmp_to_mode(findmp), mpathvalid_get_mode());
}

static void check_mpathvalid_exit(void)
{
	assert_int_equal(mpathvalid_exit(), 0);
	assert_false(initialized);
	check_config(false);
}

static void test_mpathvalid_init_good1(void **state)
{
	check_mpathvalid_init(FIND_MULTIPATHS_OFF, MPATH_LOG_PRIO_ERR,
			      MPATH_LOG_STDERR_TIMESTAMP);
}

static void test_mpathvalid_init_good2(void **state)
{
	check_mpathvalid_init(FIND_MULTIPATHS_STRICT, MPATH_LOG_PRIO_DEBUG,
			      MPATH_LOG_STDERR);
}

static void test_mpathvalid_init_good3(void **state)
{
	check_mpathvalid_init(FIND_MULTIPATHS_ON, MPATH_LOG_PRIO_NOLOG,
			      MPATH_LOG_SYSLOG);
}

static void test_mpathvalid_exit(void **state)
{
	check_mpathvalid_init(FIND_MULTIPATHS_ON, MPATH_LOG_PRIO_ERR,
			      MPATH_LOG_STDERR);
	check_mpathvalid_exit();
}

/* fails if config hasn't been set */
static void test_mpathvalid_get_mode_bad(void **state)
{
#if 1
	assert_uint_equal(mpathvalid_get_mode(), MPATH_MODE_ERROR);
#else
	assert_uint_equal(mpathvalid_get_mode(), 1);
#endif
}

/*fails if config hasn't been set */
static void test_mpathvalid_reload_config_bad1(void **state)
{
#if 1
	will_return(__wrap_init_config, 1);
#endif
	assert_int_equal(mpathvalid_reload_config(), -1);
	check_config(false);
}

/* init_config fails */
static void test_mpathvalid_reload_config_bad2(void **state)
{
	check_mpathvalid_init(FIND_MULTIPATHS_ON, MPATH_LOG_PRIO_ERR,
			      MPATH_LOG_STDERR);
	will_return(__wrap_init_config, 1);
	assert_int_equal(mpathvalid_reload_config(), -1);
	check_config(false);
	check_mpathvalid_exit();
}

static void check_mpathvalid_reload_config(int findmp)
{
	assert_string_not_equal(conf_name, CONF_TEMPLATE);
	unlink(conf_name);
	make_config_file(findmp);
	will_return(__wrap_init_config, 0);
	will_return(__wrap_init_config, findmp);
	assert_int_equal(mpathvalid_reload_config(), 0);
	check_config(true);
	assert_uint_equal(findmp_to_mode(findmp), mpathvalid_get_mode());
}

static void test_mpathvalid_reload_config_good(void **state)
{
	check_mpathvalid_init(FIND_MULTIPATHS_OFF, MPATH_LOG_PRIO_ERR,
			      MPATH_LOG_STDERR);
	check_mpathvalid_reload_config(FIND_MULTIPATHS_ON);
	check_mpathvalid_reload_config(FIND_MULTIPATHS_GREEDY);
	check_mpathvalid_reload_config(FIND_MULTIPATHS_SMART);
	check_mpathvalid_reload_config(FIND_MULTIPATHS_STRICT);
	check_mpathvalid_exit();
}

/* NULL name */
static void test_mpathvalid_is_path_bad1(void **state)
{
	assert_int_equal(mpathvalid_is_path(NULL, MPATH_STRICT, NULL, NULL, 0),
			 MPATH_IS_ERROR);
}

/* bad mode */
static void test_mpathvalid_is_path_bad2(void **state)
{
	assert_int_equal(mpathvalid_is_path(test_dev, MPATH_MODE_ERROR, NULL,
					    NULL, 0), MPATH_IS_ERROR);
}

/* NULL path_wwids and non-zero nr_paths */
static void test_mpathvalid_is_path_bad3(void **state)
{
	assert_int_equal(mpathvalid_is_path(test_dev, MPATH_MODE_ERROR, NULL,
			 		    NULL, 1), MPATH_IS_ERROR);
}

/*fails if config hasn't been set */
static void test_mpathvalid_is_path_bad4(void **state)
{
#if 0
	will_return(__wrap_is_path_valid, MPATH_IS_ERROR);
	will_return(__wrap_is_path_valid, FIND_MULTIPATHS_STRICT);
#endif
	assert_int_equal(mpathvalid_is_path(test_dev, MPATH_STRICT, NULL,
					    NULL, 0), MPATH_IS_ERROR);
}

/* is_path_valid fails */
static void test_mpathvalid_is_path_bad5(void **state)
{
	check_mpathvalid_init(FIND_MULTIPATHS_OFF, MPATH_LOG_PRIO_ERR,
			      MPATH_LOG_STDERR);
	will_return(__wrap_is_path_valid, MPATH_IS_ERROR);
	will_return(__wrap_is_path_valid, FIND_MULTIPATHS_GREEDY);
	assert_int_equal(mpathvalid_is_path(test_dev, MPATH_GREEDY, NULL,
					    NULL, 0), MPATH_IS_ERROR);
	check_mpathvalid_exit();
}

static void test_mpathvalid_is_path_good1(void **state)
{
	char *wwid;
	check_mpathvalid_init(FIND_MULTIPATHS_STRICT, MPATH_LOG_PRIO_ERR,
			      MPATH_LOG_STDERR);
	will_return(__wrap_is_path_valid, MPATH_IS_NOT_VALID);
	will_return(__wrap_is_path_valid, FIND_MULTIPATHS_STRICT);
	assert_int_equal(mpathvalid_is_path(test_dev, MPATH_DEFAULT, &wwid,
			 		    NULL, 0), MPATH_IS_NOT_VALID);
	assert_ptr_equal(wwid, NULL);
	check_mpathvalid_exit();
}

static void test_mpathvalid_is_path_good2(void **state)
{
	const char *wwids[] = { "WWID_A", "WWID_B", "WWID_C", "WWID_D" };
	char *wwid;
	check_mpathvalid_init(FIND_MULTIPATHS_ON, MPATH_LOG_PRIO_ERR,
			      MPATH_LOG_STDERR);
	will_return(__wrap_is_path_valid, MPATH_IS_VALID);
	will_return(__wrap_is_path_valid, FIND_MULTIPATHS_ON);
	will_return(__wrap_is_path_valid, TEST_WWID);
	assert_int_equal(mpathvalid_is_path(test_dev, MPATH_DEFAULT, &wwid,
					    wwids, 4), MPATH_IS_VALID);
	assert_string_equal(wwid, TEST_WWID);
	free(wwid);
}

static void test_mpathvalid_is_path_good3(void **state)
{
	const char *wwids[] = { "WWID_A", "WWID_B", "WWID_C", "WWID_D" };
	char *wwid;
	check_mpathvalid_init(FIND_MULTIPATHS_OFF, MPATH_LOG_PRIO_ERR,
			      MPATH_LOG_STDERR);
	will_return(__wrap_is_path_valid, MPATH_IS_VALID);
	will_return(__wrap_is_path_valid, FIND_MULTIPATHS_SMART);
	will_return(__wrap_is_path_valid, TEST_WWID);
	assert_int_equal(mpathvalid_is_path(test_dev, MPATH_SMART, &wwid,
					    wwids, 4), MPATH_IS_VALID);
	assert_string_equal(wwid, TEST_WWID);
	free(wwid);
}

/* maybe valid with no matching paths */
static void test_mpathvalid_is_path_good4(void **state)
{
	const char *wwids[] = { "WWID_A", "WWID_B", "WWID_C", "WWID_D" };
	char *wwid;
	check_mpathvalid_init(FIND_MULTIPATHS_SMART, MPATH_LOG_PRIO_ERR,
			      MPATH_LOG_STDERR);
	will_return(__wrap_is_path_valid, MPATH_IS_MAYBE_VALID);
	will_return(__wrap_is_path_valid, FIND_MULTIPATHS_SMART);
	will_return(__wrap_is_path_valid, TEST_WWID);
	assert_int_equal(mpathvalid_is_path(test_dev, MPATH_DEFAULT, &wwid,
					    wwids, 4), MPATH_IS_MAYBE_VALID);
	assert_string_equal(wwid, TEST_WWID);
	free(wwid);
}

/* maybe valid with matching paths */
static void test_mpathvalid_is_path_good5(void **state)
{
	const char *wwids[] = { "WWID_A", "WWID_B", TEST_WWID, "WWID_D" };
	char *wwid;
	check_mpathvalid_init(FIND_MULTIPATHS_SMART, MPATH_LOG_PRIO_ERR,
			      MPATH_LOG_STDERR);
	will_return(__wrap_is_path_valid, MPATH_IS_MAYBE_VALID);
	will_return(__wrap_is_path_valid, FIND_MULTIPATHS_SMART);
	will_return(__wrap_is_path_valid, TEST_WWID);
	assert_int_equal(mpathvalid_is_path(test_dev, MPATH_DEFAULT, &wwid,
					    wwids, 4), MPATH_IS_VALID);
	assert_string_equal(wwid, TEST_WWID);
	free(wwid);
}

#define setup_test(name) \
	cmocka_unit_test_setup_teardown(name, setup, teardown)

int test_mpathvalid(void)
{
	const struct CMUnitTest tests[] = {
		setup_test(test_mpathvalid_init_bad1),
		setup_test(test_mpathvalid_init_bad2),
		setup_test(test_mpathvalid_init_bad3),
		setup_test(test_mpathvalid_init_good1),
		setup_test(test_mpathvalid_init_good2),
		setup_test(test_mpathvalid_init_good3),
		setup_test(test_mpathvalid_exit),
		setup_test(test_mpathvalid_get_mode_bad),
		setup_test(test_mpathvalid_reload_config_bad1),
		setup_test(test_mpathvalid_reload_config_bad2),
		setup_test(test_mpathvalid_reload_config_good),
		setup_test(test_mpathvalid_is_path_bad1),
		setup_test(test_mpathvalid_is_path_bad2),
		setup_test(test_mpathvalid_is_path_bad3),
		setup_test(test_mpathvalid_is_path_bad4),
		setup_test(test_mpathvalid_is_path_bad5),
		setup_test(test_mpathvalid_is_path_good1),
		setup_test(test_mpathvalid_is_path_good2),
		setup_test(test_mpathvalid_is_path_good3),
		setup_test(test_mpathvalid_is_path_good4),
		setup_test(test_mpathvalid_is_path_good5),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}

int main(void)
{
	int r = 0;

	r += test_mpathvalid();
	return r;
}
