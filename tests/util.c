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

#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <cmocka.h>
#include "util.h"

#include "globals.c"

#define BITARR_SZ 4

static void test_basenamecpy_good0(void **state)
{
	char dst[10];

	assert_int_equal(basenamecpy("foobar", dst, sizeof(dst)), 6);
	assert_string_equal(dst, "foobar");
}

static void test_basenamecpy_good1(void **state)
{
	char dst[10];

	assert_int_equal(basenamecpy("foo/bar", dst, sizeof(dst)), 3);
	assert_string_equal(dst, "bar");
}

static void test_basenamecpy_good2(void **state)
{
	char dst[10];

	assert_int_equal(basenamecpy("/thud/blat", dst, sizeof(dst)), 4);
	assert_string_equal(dst, "blat");
}

static void test_basenamecpy_good3(void **state)
{
	char dst[4];

	assert_int_equal(basenamecpy("foo/bar", dst, sizeof(dst)), 3);
	assert_string_equal(dst, "bar");
}

static void test_basenamecpy_good4(void **state)
{
	char dst[10];

	assert_int_equal(basenamecpy("/xyzzy", dst, sizeof(dst)), 5);
	assert_string_equal(dst, "xyzzy");
}

static void test_basenamecpy_good5(void **state)
{
	char dst[4];

	assert_int_equal(basenamecpy("/foo/bar\n", dst, sizeof(dst)), 3);
	assert_string_equal(dst, "bar");
}

/* multipath expects any trailing whitespace to be stripped off the basename,
 * so that it will match pp->dev */
static void test_basenamecpy_good6(void **state)
{
        char dst[6];

        assert_int_equal(basenamecpy("/xyzzy/plugh   ", dst, sizeof(dst)), 5);
        assert_string_equal(dst, "plugh");
}

static void test_basenamecpy_good7(void **state)
{
	char src[] = "/foo/bar";
	char dst[10];

	assert_int_equal(basenamecpy(src, dst, sizeof(dst)), 3);

	strcpy(src, "badbadno");
	assert_string_equal(dst, "bar");
}

/* buffer too small */
static void test_basenamecpy_bad0(void **state)
{
        char dst[3];

        assert_int_equal(basenamecpy("baz", dst, sizeof(dst)), 0);
}

/* ends in slash */
static void test_basenamecpy_bad1(void **state)
{
        char dst[10];

        assert_int_equal(basenamecpy("foo/bar/", dst, sizeof(dst)), 0);
}

static void test_basenamecpy_bad2(void **state)
{
        char dst[10];

        assert_int_equal(basenamecpy(NULL, dst, sizeof(dst)), 0);
}

static void test_basenamecpy_bad3(void **state)
{
        char dst[10];

        assert_int_equal(basenamecpy("", dst, sizeof(dst)), 0);
}

static void test_basenamecpy_bad4(void **state)
{
        char dst[10];

        assert_int_equal(basenamecpy("/", dst, sizeof(dst)), 0);
}

static void test_basenamecpy_bad5(void **state)
{
        char dst[10];

        assert_int_equal(basenamecpy("baz/qux", NULL, sizeof(dst)), 0);
}

static void test_bitmask_1(void **state)
{
	uint64_t arr[BITARR_SZ];
	int i, j, k, m, b;

	memset(arr, 0, sizeof(arr));

	for (j = 0; j < BITARR_SZ; j++) {
		for (i = 0; i < 64; i++) {
			b = 64 * j + i;
			assert(!is_bit_set_in_array(b, arr));
			set_bit_in_array(b, arr);
			for (k = 0; k < BITARR_SZ; k++) {
				printf("b = %d j = %d k = %d a = %"PRIx64"\n",
				       b, j, k, arr[k]);
				if (k == j)
					assert_int_equal(arr[j], 1ULL << i);
				else
					assert_int_equal(arr[k], 0ULL);
			}
			for (m = 0; m < 64; m++)
				if (i == m)
					assert(is_bit_set_in_array(64 * j + m,
								   arr));
				else
					assert(!is_bit_set_in_array(64 * j + m,
								    arr));
			clear_bit_in_array(b, arr);
			assert(!is_bit_set_in_array(b, arr));
			for (k = 0; k < BITARR_SZ; k++)
				assert_int_equal(arr[k], 0ULL);
		}
	}
}

static void test_bitmask_2(void **state)
{
	uint64_t arr[BITARR_SZ];
	int i, j, k, m, b;

	memset(arr, 0, sizeof(arr));

	for (j = 0; j < BITARR_SZ; j++) {
		for (i = 0; i < 64; i++) {
			b = 64 * j + i;
			assert(!is_bit_set_in_array(b, arr));
			set_bit_in_array(b, arr);
			for (m = 0; m < 64; m++)
				if (m <= i)
					assert(is_bit_set_in_array(64 * j + m,
								   arr));
				else
					assert(!is_bit_set_in_array(64 * j + m,
								    arr));
			assert(is_bit_set_in_array(b, arr));
			for (k = 0; k < BITARR_SZ; k++) {
				if (k < j || (k == j && i == 63))
					assert_int_equal(arr[k], ~0ULL);
				else if (k > j)
					assert_int_equal(arr[k], 0ULL);
				else
					assert_int_equal(
						arr[k],
						(1ULL << (i + 1)) - 1);
			}
		}
	}
	for (j = 0; j < BITARR_SZ; j++) {
		for (i = 0; i < 64; i++) {
			b = 64 * j + i;
			assert(is_bit_set_in_array(b, arr));
			clear_bit_in_array(b, arr);
			for (m = 0; m < 64; m++)
				if (m <= i)
					assert(!is_bit_set_in_array(64 * j + m,
								    arr));
				else
					assert(is_bit_set_in_array(64 * j + m,
								   arr));
			assert(!is_bit_set_in_array(b, arr));
			for (k = 0; k < BITARR_SZ; k++) {
				if (k < j || (k == j && i == 63))
					assert_int_equal(arr[k], 0ULL);
				else if (k > j)
					assert_int_equal(arr[k], ~0ULL);
				else
					assert_int_equal(
						arr[k],
						~((1ULL << (i + 1)) - 1));
			}
		}
	}
}

int test_basenamecpy(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_basenamecpy_good0),
		cmocka_unit_test(test_basenamecpy_good1),
		cmocka_unit_test(test_basenamecpy_good2),
		cmocka_unit_test(test_basenamecpy_good3),
		cmocka_unit_test(test_basenamecpy_good4),
		cmocka_unit_test(test_basenamecpy_good5),
		cmocka_unit_test(test_basenamecpy_good6),
		cmocka_unit_test(test_basenamecpy_good7),
		cmocka_unit_test(test_basenamecpy_bad0),
		cmocka_unit_test(test_basenamecpy_bad1),
		cmocka_unit_test(test_basenamecpy_bad2),
		cmocka_unit_test(test_basenamecpy_bad3),
		cmocka_unit_test(test_basenamecpy_bad4),
		cmocka_unit_test(test_basenamecpy_bad5),
		cmocka_unit_test(test_bitmask_1),
		cmocka_unit_test(test_bitmask_2),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}

static const char src_str[] = "Hello";

/* strlcpy with length 0 */
static void test_strlcpy_0(void **state)
{
	char tst[] = "word";
	int rc;

	rc = strlcpy(tst, src_str, 0);
	assert_int_equal(rc, strlen(src_str));
	assert_string_equal(tst, "word");
}

/* strlcpy with length 1 */
static void test_strlcpy_1(void **state)
{
	char tst[] = "word";
	int rc;

	rc = strlcpy(tst, src_str, 1);
	assert_int_equal(rc, strlen(src_str));
	assert_int_equal(tst[0], '\0');
	assert_string_equal(tst + 1, "ord");
}

/* strlcpy with length 2 */
static void test_strlcpy_2(void **state)
{
	char tst[] = "word";
	int rc;

	rc = strlcpy(tst, src_str, 2);
	assert_int_equal(rc, strlen(src_str));
	assert_int_equal(tst[0], src_str[0]);
	assert_int_equal(tst[1], '\0');
	assert_string_equal(tst + 2, "rd");
}

/* strlcpy with dst length < src length */
static void test_strlcpy_3(void **state)
{
	char tst[] = "word";
	int rc;

	rc = strlcpy(tst, src_str, sizeof(tst));
	assert_int_equal(rc, strlen(src_str));
	assert_int_equal(sizeof(tst) - 1, strlen(tst));
	assert_true(strncmp(tst, src_str, sizeof(tst) - 1) == 0);
}

/* strlcpy with dst length > src length */
static void test_strlcpy_4(void **state)
{
	static const char old[] = "0123456789";
	char *tst;
	int rc;

	tst = strdup(old);
	rc = strlcpy(tst, src_str, sizeof(old));
	assert_int_equal(rc, strlen(src_str));
	assert_string_equal(src_str, tst);
	assert_string_equal(tst + sizeof(src_str), old + sizeof(src_str));
	free(tst);
}

/* strlcpy with dst length = src length, dst not terminated */
static void test_strlcpy_5(void **state)
{
	char *tst;
	int rc;

	tst = malloc(sizeof(src_str));
	memset(tst, 'f', sizeof(src_str));

	rc = strlcpy(tst, src_str, sizeof(src_str));
	assert_int_equal(rc, strlen(src_str));
	assert_string_equal(src_str, tst);

	free(tst);
}

/* strlcpy with dst length > src length, dst not terminated */
static void test_strlcpy_6(void **state)
{
	char *tst;
	int rc;

	tst = malloc(sizeof(src_str) + 2);
	memset(tst, 'f', sizeof(src_str) + 2);

	rc = strlcpy(tst, src_str, sizeof(src_str) + 2);
	assert_int_equal(rc, strlen(src_str));
	assert_string_equal(src_str, tst);
	assert_int_equal(tst[sizeof(src_str)], 'f');
	assert_int_equal(tst[sizeof(src_str) + 1], 'f');

	free(tst);
}

/* strlcpy with empty src */
static void test_strlcpy_7(void **state)
{
	char tst[] = "word";
	static const char empty[] = "";
	int rc;

	rc = strlcpy(tst, empty, sizeof(tst));
	assert_int_equal(rc, strlen(empty));
	assert_string_equal(empty, tst);
	assert_string_equal(tst + 1, "ord");
}

/* strlcpy with empty src, length 0 */
static void test_strlcpy_8(void **state)
{
	char tst[] = "word";
	static const char empty[] = "";
	int rc;

	rc = strlcpy(tst, empty, 0);
	assert_int_equal(rc, strlen(empty));
	assert_string_equal("word", tst);
}

static int test_strlcpy(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_strlcpy_0),
		cmocka_unit_test(test_strlcpy_1),
		cmocka_unit_test(test_strlcpy_2),
		cmocka_unit_test(test_strlcpy_3),
		cmocka_unit_test(test_strlcpy_4),
		cmocka_unit_test(test_strlcpy_5),
		cmocka_unit_test(test_strlcpy_6),
		cmocka_unit_test(test_strlcpy_7),
		cmocka_unit_test(test_strlcpy_8),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

int main(void)
{
	int ret = 0;

	ret += test_basenamecpy();
	ret += test_strlcpy();
	return ret;
}
