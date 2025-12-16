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
#include "cmocka-compat.h"

#include <errno.h>

#include "vector.h"
#include "cli.h"

#include "globals.c"
#define HANDLER(x) NULL
#include "callbacks.c"

/* See cli.c */
#define INVALID_FINGERPRINT ((uint32_t)(0))

static int setup(void **state)
{
	return cli_init();
}

static int teardown(void **state)
{
	cli_exit();
	return 0;
}

/*
 * @NAME: test name
 * @CMD: test command
 * @R: retcode of get_cmdvec()
 * @FPR: fingerprint (only if R==0)
 * @GOOD: expect to find handler (only if R==0)
 */
#define client_test(NAME, CMD, R, FPR, GOOD)			\
static void client_test_##NAME(void **state)			\
{								\
	vector v = NULL;					\
	char cmd[] = CMD;					\
								\
	assert_int_equal(get_cmdvec(cmd, &v, false), R);	\
	if (R == 0) {						\
		assert_ptr_not_equal(v, NULL);			\
		assert_uint_equal(fingerprint(v), FPR);		\
		if (GOOD)					\
			assert_ptr_not_equal(find_handler_for_cmdvec(v), NULL); \
		else						\
			assert_ptr_equal(find_handler_for_cmdvec(v), NULL); \
		free_keys(v);					\
	} else							\
		assert_ptr_equal(v, NULL);			\
}

/*
 * @NAME: test name
 * @CMD: test command
 * @FPR: fingerprint
 * @PAR: value of parameter for keyword 1
 */
#define client_param(NAME, CMD, FPR, PAR)			\
static void client_param_##NAME(void **state)			\
{								\
	vector v = NULL;					\
	char cmd[] = CMD;					\
								\
	assert_int_equal(get_cmdvec(cmd, &v, false), 0);	\
	assert_ptr_not_equal(v, NULL);				\
	assert_uint_equal(fingerprint(v), FPR);			\
	assert_ptr_not_equal(find_handler_for_cmdvec(v), NULL);	\
	assert_string_equal(((struct key *)VECTOR_SLOT(v, 1))->param, PAR); \
	free_keys(v);						\
}

/*
 * @NAME: test name
 * @CMD: test command
 * @FPR: fingerprint
 * @PAR1: value of parameter for keyword 1
 * @N: index of 2nd parameter keyword
 * @PARN: value of parameter for keyword N
 */
#define client_2param(NAME, CMD, FPR, PAR1, N, PARN)		\
static void client_2param_##NAME(void **state)			\
{								\
	vector v = NULL;					\
	char cmd[] = CMD;					\
								\
	assert_int_equal(get_cmdvec(cmd, &v, false), 0);	\
	assert_ptr_not_equal(v, NULL);				\
	assert_uint_equal(fingerprint(v), FPR);			\
	assert_ptr_not_equal(find_handler_for_cmdvec(v), NULL);	\
	assert_string_equal(((struct key *)VECTOR_SLOT(v, 1))->param, PAR1); \
	assert_string_equal(((struct key *)VECTOR_SLOT(v, N))->param, PARN); \
	free_keys(v);						\
}

static void client_test_null(void **state)
{
	vector v = NULL;

	/* alloc_strvec() returns ENOMEM for NULL cmd */
	assert_int_equal(get_cmdvec(NULL, &v, false), ENOMEM);
	assert_ptr_equal(v, NULL);
}

/* alloc_strvec() returns ENOMEM for empty string */
client_test(empty, "", ENOMEM, 0, false);
client_test(bogus, "bogus", ESRCH, 0, false);
client_test(list, "list", 0, VRB_LIST, false);
client_test(show, "show", 0, VRB_LIST, false);
client_test(s, "s", ESRCH, 0, false);
/* partial match works if it's unique */
client_test(sh, "sh", ESRCH, 0, false);
client_test(sho, "sho", 0, VRB_LIST, false);
client_test(add, "add", 0, VRB_ADD, false);
client_test(resume, "resume", 0, VRB_RESUME, false);
client_test(disablequeueing, "disablequeueing", 0, VRB_DISABLEQ, false);
/* "disable" -> disablequeueing, "queueing" -> not found */
client_test(disable_queueing, "disable queueing", ESRCH, 0, false);
/* ENOENT because the not-found keyword is not last pos */
client_test(queueing_disable, "queueing disable", ENOENT, 0, false);
client_test(disable, "disable", 0, VRB_DISABLEQ, false);
client_test(setprkey, "setprkey", 0, VRB_SETPRKEY, false);
client_test(quit, "quit", 0, VRB_QUIT, true);
client_test(exit, "exit", 0, VRB_QUIT, true);
client_test(show_maps, "show maps", 0, VRB_LIST|Q1_MAPS, true);
client_test(sh_maps, "sh maps", ENOENT, 0, 0);
client_test(sho_maps, "sho maps", 0, VRB_LIST|Q1_MAPS, true);
client_test(sho_multipaths, "sho multipaths", 0, VRB_LIST|Q1_MAPS, true);
/* Needs a parameter */
client_test(show_map, "show map", EINVAL, 0, 0);
client_test(show_ma, "show ma", ESRCH, 0, 0);
client_test(show_list_maps, "show list maps", 0,
	    VRB_LIST|(VRB_LIST<<8)|(KEY_MAPS<<16), false);
client_test(show_maps_list, "show maps list", 0,
	    VRB_LIST|(VRB_LIST<<16)|(KEY_MAPS<<8), false);
client_test(maps_show, "maps show ", 0, (VRB_LIST<<8)|KEY_MAPS, false);
client_test(show_maps_list_json, "show maps list json", 0,
	    VRB_LIST|(VRB_LIST<<16)|(KEY_MAPS<<8)|(KEY_JSON<<24), false);
/* More than 4 keywords */
client_test(show_maps_list_json_raw, "show maps list json raw", 0,
	    INVALID_FINGERPRINT, false);
client_test(show_list_show_list, "show list show list", 0,
	    VRB_LIST|(VRB_LIST<<8)|(VRB_LIST<<16)|(VRB_LIST<<24), false);
client_test(show_list_show_list_show, "show list show list  show", 0,
	    INVALID_FINGERPRINT, false);
client_test(q_q_q_q, "q q q q", 0,
	    VRB_QUIT|(VRB_QUIT<<8)|(VRB_QUIT<<16)|(VRB_QUIT<<24), false);
client_test(show_path_xy, "show path xy", 0, VRB_LIST|Q1_PATH, true);
client_param(show_path_xy, "show path xy", VRB_LIST|Q1_PATH, "xy");
client_param(show_path_xy_2, "show path \"xy\"", VRB_LIST|Q1_PATH, "xy");
client_param(show_path_x_y, "show path \"x y\"", VRB_LIST|Q1_PATH, "x y");
client_param(show_path_2inch, "show path \"2\"\"\"", VRB_LIST|Q1_PATH, "2\"");
/* missing closing quote */
client_param(show_path_2inch_1, "show path \"2\"\"", VRB_LIST|Q1_PATH, "2\"");
client_test(show_map_xy, "show map xy", 0, VRB_LIST|Q1_MAP, false);
client_test(show_map_xy_bogus, "show map xy bogus", ESRCH, 0, false);
client_2param(show_map_xy_format_h, "show map xy form %h",
	      VRB_LIST|Q1_MAP|Q2_FMT, "xy", 2, "%h");
client_2param(show_map_xy_raw_format_h, "show map xy raw form %h",
	      VRB_LIST|Q1_MAP|Q2_RAW|Q3_FMT, "xy", 3, "%h");
client_test(show_map_xy_format_h_raw, "show map xy form %h raw", 0,
	    VRB_LIST|(KEY_MAP<<8)|(KEY_FMT<<16)|(KEY_RAW<<24), false);
client_param(list_path_sda, "list path sda", VRB_LIST|Q1_PATH, "sda");
client_param(add_path_sda, "add path sda", VRB_ADD|Q1_PATH, "sda");
client_test(list_list_path_sda, "list list path sda", 0,
	    VRB_LIST|(VRB_LIST<<8)|(KEY_PATH<<16), false);

static int client_tests(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(client_test_null),
		cmocka_unit_test(client_test_empty),
		cmocka_unit_test(client_test_bogus),
		cmocka_unit_test(client_test_list),
		cmocka_unit_test(client_test_show),
		cmocka_unit_test(client_test_s),
		cmocka_unit_test(client_test_sh),
		cmocka_unit_test(client_test_sho),
		cmocka_unit_test(client_test_add),
		cmocka_unit_test(client_test_resume),
		cmocka_unit_test(client_test_disablequeueing),
		cmocka_unit_test(client_test_disable_queueing),
		cmocka_unit_test(client_test_queueing_disable),
		cmocka_unit_test(client_test_disable),
		cmocka_unit_test(client_test_setprkey),
		cmocka_unit_test(client_test_quit),
		cmocka_unit_test(client_test_exit),
		cmocka_unit_test(client_test_show_maps),
		cmocka_unit_test(client_test_sh_maps),
		cmocka_unit_test(client_test_sho_maps),
		cmocka_unit_test(client_test_sho_multipaths),
		cmocka_unit_test(client_test_show_map),
		cmocka_unit_test(client_test_show_ma),
		cmocka_unit_test(client_test_maps_show),
		cmocka_unit_test(client_test_show_list_maps),
		cmocka_unit_test(client_test_show_maps_list),
		cmocka_unit_test(client_test_show_maps_list_json),
		cmocka_unit_test(client_test_show_maps_list_json_raw),
		cmocka_unit_test(client_test_show_list_show_list),
		cmocka_unit_test(client_test_show_list_show_list_show),
		cmocka_unit_test(client_test_q_q_q_q),
		cmocka_unit_test(client_test_show_map_xy),
		cmocka_unit_test(client_test_show_map_xy_bogus),
		cmocka_unit_test(client_test_show_path_xy),
		cmocka_unit_test(client_param_show_path_xy),
		cmocka_unit_test(client_param_show_path_xy_2),
		cmocka_unit_test(client_param_show_path_x_y),
		cmocka_unit_test(client_param_show_path_2inch),
		cmocka_unit_test(client_param_show_path_2inch_1),
		cmocka_unit_test(client_2param_show_map_xy_format_h),
		cmocka_unit_test(client_2param_show_map_xy_raw_format_h),
		cmocka_unit_test(client_test_show_map_xy_format_h_raw),
		cmocka_unit_test(client_param_list_path_sda),
		cmocka_unit_test(client_param_add_path_sda),
		cmocka_unit_test(client_test_list_list_path_sda),
	};

	return cmocka_run_group_tests(tests, setup, teardown);
}

int main(void)
{
	int ret = 0;

	init_test_verbosity(-1);
	ret += client_tests();
	return ret;
}
