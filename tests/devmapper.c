// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Unit tests for internal helper functions of libmultipath/devmapper.c
 *
 * dm_reassign_deps() builds the string of a dm table entry with the
 * device number of a dependency ("dep", usually a path device, e.g.
 * "8:16") replaced by the device number of the multipath map ("newdep",
 * e.g. "253:1234"). The replacement string is usually *longer* than the
 * string it replaces, so the result must not be written in place into
 * the buffer holding the original table (which is sized to fit the
 * original string only).
 */

#define _GNU_SOURCE 1
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include "cmocka-compat.h"
#include "util.h"
#include "devmapper.h"
#include "globals.c"
/* I have to do this to get at the static functions */
#include "../libmultipath/devmapper.c"

/* newdep is longer than dep: this used to write past the end of buff */
static void test_reassign_deps_longer(void **state)
{
	char *res = dm_reassign_deps("1 0 linear 8:0 0", "8:0", "253:123");

	assert_non_null(res);
	assert_string_equal(res, "1 0 linear 253:123 0");
	free(res);
}

/* newdep is shorter than dep */
static void test_reassign_deps_shorter(void **state)
{
	char *res = dm_reassign_deps(
		"0 2097152 linear 253:123 0", "253:123", "8:0");

	assert_non_null(res);
	assert_string_equal(res, "0 2097152 linear 8:0 0");
	free(res);
}

/* dep at the beginning of the table string */
static void test_reassign_deps_prefix(void **state)
{
	char *res = dm_reassign_deps("8:0 0 1", "8:0", "253:0");

	assert_non_null(res);
	assert_string_equal(res, "253:0 0 1");
	free(res);
}

/* dep is the entire table string */
static void test_reassign_deps_whole(void **state)
{
	char *res = dm_reassign_deps("8:0", "8:0", "253:0");

	assert_non_null(res);
	assert_string_equal(res, "253:0");
	free(res);
}

/* dep doesn't occur in the table string */
static void test_reassign_deps_not_found(void **state)
{
	assert_null(dm_reassign_deps("1 0 linear 8:0 0", "9:0", "253:123"));
}

/*
 * dm_reassign_deps() replaces all occurrences of dep. A single target
 * may reference the same device more than once (e.g. striped or verity
 * targets).
 */
static void test_reassign_deps_all_occurrences(void **state)
{
	char *res = dm_reassign_deps("mirror 2 8:0 0 8:0 1", "8:0", "253:7");

	assert_non_null(res);
	assert_string_equal(res, "mirror 2 253:7 0 253:7 1");
	free(res);
}

/* multiple occurrences of a partial match must all be skipped */
static void test_reassign_deps_all_skips_partial(void **state)
{
	char *res = dm_reassign_deps("0 1 striped 2 128:0 8:0 0 8:0 1",
				     "8:0", "253:5");

	assert_non_null(res);
	assert_string_equal(res, "0 1 striped 2 128:0 253:5 0 253:5 1");
	free(res);
}

/* all occurrences, with newdep shorter than dep */
static void test_reassign_deps_all_shorter(void **state)
{
	char *res = dm_reassign_deps("0 0 striped 2 253:123 0 253:123 1",
				     "253:123", "8:0");

	assert_non_null(res);
	assert_string_equal(res, "0 0 striped 2 8:0 0 8:0 1");
	free(res);
}

/* dep must not match inside a longer minor number, e.g. "8:03" for "8:0" */
static void test_reassign_deps_no_partial_minor(void **state)
{
	assert_null(dm_reassign_deps("1 0 linear 8:03 0", "8:0", "253:5"));
}

/* dep must not match inside a longer major number, e.g. "18:0" for "8:0" */
static void test_reassign_deps_no_partial_major(void **state)
{
	assert_null(dm_reassign_deps("1 0 linear 18:0 0", "8:0", "253:5"));
}

/* a partial match must be skipped, the standalone one replaced */
static void test_reassign_deps_skips_partial_match(void **state)
{
	char *res = dm_reassign_deps("0 1 striped 2 128:0 8:0 0 1",
				     "8:0", "253:5");

	assert_non_null(res);
	assert_string_equal(res, "0 1 striped 2 128:0 253:5 0 1");
	free(res);
}

/* dep at the very end of the table string is still matched */
static void test_reassign_deps_at_end(void **state)
{
	char *res = dm_reassign_deps("1 0 linear 8:0", "8:0", "253:5");

	assert_non_null(res);
	assert_string_equal(res, "1 0 linear 253:5");
	free(res);
}

/*
 * dep must be delimited by whitespace (or the string bounds).
 * A match preceded by a non-digit, non-whitespace character
 * is not a device number token.
 */
static void test_reassign_deps_no_nonspace_left(void **state)
{
	assert_null(dm_reassign_deps("1 0 linear -8:0 0", "8:0", "253:5"));
}

/* same for a non-digit, non-whitespace character after the match */
static void test_reassign_deps_no_nonspace_right(void **state)
{
	assert_null(dm_reassign_deps("1 0 linear 8:0p 0", "8:0", "253:5"));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_reassign_deps_longer),
		cmocka_unit_test(test_reassign_deps_shorter),
		cmocka_unit_test(test_reassign_deps_prefix),
		cmocka_unit_test(test_reassign_deps_whole),
		cmocka_unit_test(test_reassign_deps_not_found),
		cmocka_unit_test(test_reassign_deps_all_occurrences),
		cmocka_unit_test(test_reassign_deps_no_partial_minor),
		cmocka_unit_test(test_reassign_deps_no_partial_major),
		cmocka_unit_test(test_reassign_deps_skips_partial_match),
		cmocka_unit_test(test_reassign_deps_at_end),
		cmocka_unit_test(test_reassign_deps_no_nonspace_left),
		cmocka_unit_test(test_reassign_deps_no_nonspace_right),
		cmocka_unit_test(test_reassign_deps_all_skips_partial),
		cmocka_unit_test(test_reassign_deps_all_shorter),
	};

	init_test_verbosity(-1);
	return cmocka_run_group_tests(tests, NULL, NULL);
}
