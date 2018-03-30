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
 * along with this program; if not, write to the Free Software
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
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}

int main(void)
{
	int ret = 0;

	ret += test_basenamecpy();
	return ret;
}
