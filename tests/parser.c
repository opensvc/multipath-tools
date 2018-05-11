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
// #include "list.h"
#include "parser.h"
#include "vector.h"

#include "globals.c"

/* Set these to 1 to get success for current broken behavior */
/* Strip leading whitespace between quotes */
#define LSTRIP_QUOTED_WSP 0
/* Stop parsing at 2nd quote */
#define TWO_QUOTES_ONLY 0

static char *test_file = "test.conf";

/* Missing declaration */
int validate_config_strvec(vector strvec, char *file);

/* Stringify helpers */
#define _str_(x) #x
#define str(x) _str_(x)

static int setup(void **state)
{
	return 0;
}

static int teardown(void **state)
{
	return 0;
}

static void test01(void **state)
{
	vector v = alloc_strvec("keyword value");
	char *val;

	assert_int_equal(validate_config_strvec(v, test_file), 0);
	assert_int_equal(VECTOR_SIZE(v), 2);
	assert_string_equal(VECTOR_SLOT(v, 0), "keyword");
	assert_string_equal(VECTOR_SLOT(v, 1), "value");

	val = set_value(v);
	assert_string_equal(val, "value");

	free(val);
	free_strvec(v);
}

static void test02(void **state)
{
	vector v = alloc_strvec("keyword \"value\"");
	char *val;

	assert_int_equal(validate_config_strvec(v, test_file), 0);
	assert_int_equal(VECTOR_SIZE(v), 4);
	assert_string_equal(VECTOR_SLOT(v, 0), "keyword");
	assert_true(is_quote(VECTOR_SLOT(v, 1)));;
	assert_string_equal(VECTOR_SLOT(v, 2), "value");
	assert_true(is_quote(VECTOR_SLOT(v, 3)));;

	val = set_value(v);
	assert_string_equal(val, "value");

	free(val);
	free_strvec(v);
}

static void test03(void **state)
{
	vector v = alloc_strvec("keyword value\n");
	char *val;

	assert_int_equal(validate_config_strvec(v, test_file), 0);
	assert_int_equal(VECTOR_SIZE(v), 2);
	assert_string_equal(VECTOR_SLOT(v, 0), "keyword");
	assert_string_equal(VECTOR_SLOT(v, 1), "value");

	val = set_value(v);
	assert_string_equal(val, "value");

	free(val);
	free_strvec(v);
}

static void test04(void **state)
{
	vector v = alloc_strvec("keyword \t   value   \t \n   ");
	char *val;

	assert_int_equal(validate_config_strvec(v, test_file), 0);
	assert_int_equal(VECTOR_SIZE(v), 2);
	assert_string_equal(VECTOR_SLOT(v, 0), "keyword");
	assert_string_equal(VECTOR_SLOT(v, 1), "value");

	val = set_value(v);
	assert_string_equal(val, "value");

	free(val);
	free_strvec(v);
}

static void test05(void **state)
{
	vector v = alloc_strvec("keyword \t   value   \t ! comment  ");
	char *val;

	assert_int_equal(validate_config_strvec(v, test_file), 0);
	assert_int_equal(VECTOR_SIZE(v), 2);
	assert_string_equal(VECTOR_SLOT(v, 0), "keyword");
	assert_string_equal(VECTOR_SLOT(v, 1), "value");

	val = set_value(v);
	assert_string_equal(val, "value");

	free(val);
	free_strvec(v);
}

static void test06(void **state)
{
	vector v = alloc_strvec("keyword \t   value   # \n comment  ");
	char *val;

	assert_int_equal(validate_config_strvec(v, test_file), 0);
	assert_int_equal(VECTOR_SIZE(v), 2);
	assert_string_equal(VECTOR_SLOT(v, 0), "keyword");
	assert_string_equal(VECTOR_SLOT(v, 1), "value");

	val = set_value(v);
	assert_string_equal(val, "value");

	free(val);
	free_strvec(v);
}

static void test07(void **state)
{
	vector v = alloc_strvec("keyword \t   value   more  ");
	char *val;

	assert_int_equal(validate_config_strvec(v, test_file), 0);
	assert_int_equal(VECTOR_SIZE(v), 3);
	assert_string_equal(VECTOR_SLOT(v, 0), "keyword");
	assert_string_equal(VECTOR_SLOT(v, 1), "value");
	assert_string_equal(VECTOR_SLOT(v, 2), "more");

	val = set_value(v);
	assert_string_equal(val, "value");

	free(val);
	free_strvec(v);
}

static void test08(void **state)
{
#define QUOTED08 "  value   more  "
#define QUOTED08B "value   more  "
	vector v = alloc_strvec("keyword \t \"" QUOTED08 "\"");
	char *val;

	assert_int_equal(validate_config_strvec(v, test_file), 0);
	assert_int_equal(VECTOR_SIZE(v), 4);
	assert_string_equal(VECTOR_SLOT(v, 0), "keyword");
	assert_true(is_quote(VECTOR_SLOT(v, 1)));;
#if LSTRIP_QUOTED_WSP
	assert_string_equal(VECTOR_SLOT(v, 2), QUOTED08B);
#else
	assert_string_equal(VECTOR_SLOT(v, 2), QUOTED08);
#endif
	assert_true(is_quote(VECTOR_SLOT(v, 3)));;

	val = set_value(v);
#if LSTRIP_QUOTED_WSP
	assert_string_equal(val, QUOTED08B);
#else
	assert_string_equal(val, QUOTED08);
#endif
	free(val);
	free_strvec(v);
}

static void test09(void **state)
{
#define QUOTED09 "value # more"
	vector v = alloc_strvec("keyword \"" QUOTED09 "\"");
	char *val;

	assert_int_equal(validate_config_strvec(v, test_file), 0);
	assert_int_equal(VECTOR_SIZE(v), 4);
	assert_string_equal(VECTOR_SLOT(v, 0), "keyword");
	assert_true(is_quote(VECTOR_SLOT(v, 1)));;
	assert_string_equal(VECTOR_SLOT(v, 2), QUOTED09);
	assert_true(is_quote(VECTOR_SLOT(v, 3)));;

	val = set_value(v);
	assert_string_equal(val, QUOTED09);

	free(val);
	free_strvec(v);
}

static void test10(void **state)
{
#define QUOTED10 "value ! more"
	vector v = alloc_strvec("keyword \"" QUOTED10 "\"");
	char *val;

	assert_int_equal(validate_config_strvec(v, test_file), 0);
	assert_int_equal(VECTOR_SIZE(v), 4);
	assert_string_equal(VECTOR_SLOT(v, 0), "keyword");
	assert_true(is_quote(VECTOR_SLOT(v, 1)));;
	assert_string_equal(VECTOR_SLOT(v, 2), QUOTED10);
	assert_true(is_quote(VECTOR_SLOT(v, 3)));;

	val = set_value(v);
	assert_string_equal(val, QUOTED10);

	free(val);
	free_strvec(v);
}

static void test11(void **state)
{
#define QUOTED11 "value comment"
	vector v = alloc_strvec("keyword\"" QUOTED11 "\"");
	char *val;

	assert_int_equal(validate_config_strvec(v, test_file), 0);
	assert_int_equal(VECTOR_SIZE(v), 4);
	assert_string_equal(VECTOR_SLOT(v, 0), "keyword");
	assert_true(is_quote(VECTOR_SLOT(v, 1)));;
	assert_string_equal(VECTOR_SLOT(v, 2), QUOTED11);
	assert_true(is_quote(VECTOR_SLOT(v, 3)));;

	val = set_value(v);
	assert_string_equal(val, QUOTED11);

	free(val);
	free_strvec(v);
}

static void test12(void **state)
{
	vector v = alloc_strvec("key\"word\"");
	char *val;

	assert_int_equal(validate_config_strvec(v, test_file), 0);
	assert_int_equal(VECTOR_SIZE(v), 4);
	assert_string_equal(VECTOR_SLOT(v, 0), "key");
	assert_true(is_quote(VECTOR_SLOT(v, 1)));;
	assert_string_equal(VECTOR_SLOT(v, 2), "word");
	assert_true(is_quote(VECTOR_SLOT(v, 3)));;

	val = set_value(v);
	assert_string_equal(val, "word");

	free(val);
	free_strvec(v);
}

static void test13(void **state)
{
	vector v = alloc_strvec("keyword value \"quoted\"");
	char *val;

	assert_int_equal(validate_config_strvec(v, test_file), 0);
	assert_int_equal(VECTOR_SIZE(v), 5);
	assert_string_equal(VECTOR_SLOT(v, 0), "keyword");
	assert_string_equal(VECTOR_SLOT(v, 1), "value");
	assert_true(is_quote(VECTOR_SLOT(v, 2)));;
	assert_string_equal(VECTOR_SLOT(v, 3), "quoted");
	assert_true(is_quote(VECTOR_SLOT(v, 4)));;

	val = set_value(v);
	assert_string_equal(val, "value");

	free(val);
	free_strvec(v);
}

static void test14(void **state)
{
	vector v = alloc_strvec("keyword \"value \"  comment\"\"");
	char *val;

	assert_int_equal(validate_config_strvec(v, test_file), 0);
	assert_int_equal(VECTOR_SIZE(v), 7);
	assert_string_equal(VECTOR_SLOT(v, 0), "keyword");
	assert_true(is_quote(VECTOR_SLOT(v, 1)));;
	assert_string_equal(VECTOR_SLOT(v, 2), "value ");
	assert_true(is_quote(VECTOR_SLOT(v, 3)));;
	assert_string_equal(VECTOR_SLOT(v, 4), "comment");
	assert_true(is_quote(VECTOR_SLOT(v, 5)));;
	assert_true(is_quote(VECTOR_SLOT(v, 6)));;

	val = set_value(v);
	assert_string_equal(val, "value ");

	free(val);
	free_strvec(v);
}

static void test15(void **state)
{
#define QUOTED15 "word  value\n  comment"
	vector v = alloc_strvec("key\"" QUOTED15 "\"");
	char *val;

	assert_int_equal(VECTOR_SIZE(v), 4);
	assert_string_equal(VECTOR_SLOT(v, 0), "key");
	assert_true(is_quote(VECTOR_SLOT(v, 1)));;
	assert_string_equal(VECTOR_SLOT(v, 2), QUOTED15);
	assert_true(is_quote(VECTOR_SLOT(v, 3)));;
	assert_int_equal(validate_config_strvec(v, test_file), 0);

	val = set_value(v);
	assert_string_equal(val, QUOTED15);

	free(val);
	free_strvec(v);
}

static void test16(void **state)
{
	vector v = alloc_strvec("keyword \"2.5\"\" SSD\"");
	char *val;

#if TWO_QUOTES_ONLY
	assert_int_equal(validate_config_strvec(v, test_file), 0);
	assert_int_equal(VECTOR_SIZE(v), 6);
	assert_string_equal(VECTOR_SLOT(v, 0), "keyword");
	assert_true(is_quote(VECTOR_SLOT(v, 1)));;
	assert_string_equal(VECTOR_SLOT(v, 2), "2.5");
	assert_true(is_quote(VECTOR_SLOT(v, 3)));;
	assert_string_equal(VECTOR_SLOT(v, 4), "SSD");
	assert_true(is_quote(VECTOR_SLOT(v, 5)));;

	val = set_value(v);
	assert_string_equal(val, "2.5");
#else
	assert_int_equal(VECTOR_SIZE(v), 4);
	assert_string_equal(VECTOR_SLOT(v, 0), "keyword");
	assert_true(is_quote(VECTOR_SLOT(v, 1)));;
	assert_string_equal(VECTOR_SLOT(v, 2), "2.5\" SSD");
	assert_true(is_quote(VECTOR_SLOT(v, 3)));;

	val = set_value(v);
	assert_string_equal(val, "2.5\" SSD");
#endif
	free(val);
	free_strvec(v);
}

static void test17(void **state)
{
	vector v = alloc_strvec("keyword \"\"\"\"\" is empty\"");
 	char *val;
#if TWO_QUOTES_ONLY
	assert_int_equal(validate_config_strvec(v, test_file), 0);
	assert_int_equal(VECTOR_SIZE(v), 6);
	assert_string_equal(VECTOR_SLOT(v, 0), "keyword");
	assert_true(is_quote(VECTOR_SLOT(v, 1)));;
	assert_true(is_quote(VECTOR_SLOT(v, 2)));;
	assert_true(is_quote(VECTOR_SLOT(v, 3)));;
#if LSTRIP_QUOTED_WSP
	assert_string_equal(VECTOR_SLOT(v, 4), "is empty");
#else
	assert_string_equal(VECTOR_SLOT(v, 4), " is empty");
#endif
	assert_true(is_quote(VECTOR_SLOT(v, 5)));;

	val = set_value(v);
	assert_string_equal(val, "");
#else
	assert_int_equal(validate_config_strvec(v, test_file), 0);
	assert_int_equal(VECTOR_SIZE(v), 4);
	assert_string_equal(VECTOR_SLOT(v, 0), "keyword");
	assert_true(is_quote(VECTOR_SLOT(v, 1)));;
	assert_string_equal(VECTOR_SLOT(v, 2), "\"\" is empty");
	assert_true(is_quote(VECTOR_SLOT(v, 3)));;

	val = set_value(v);
	assert_string_equal(val, "\"\" is empty");
#endif
	free(val);
	free_strvec(v);
}

static void test18(void **state)
{
	vector v = alloc_strvec("keyword \"\"\"\"");
 	char *val;
#if TWO_QUOTES_ONLY
	assert_int_equal(validate_config_strvec(v, test_file), 0);
	assert_int_equal(VECTOR_SIZE(v), 5);
	assert_string_equal(VECTOR_SLOT(v, 0), "keyword");
	assert_true(is_quote(VECTOR_SLOT(v, 1)));;
	assert_true(is_quote(VECTOR_SLOT(v, 2)));;
	assert_true(is_quote(VECTOR_SLOT(v, 3)));;
	assert_true(is_quote(VECTOR_SLOT(v, 4)));;

	val = set_value(v);
	assert_string_equal(val, "");
#else
	assert_int_equal(validate_config_strvec(v, test_file), 0);
	assert_int_equal(VECTOR_SIZE(v), 4);
	assert_string_equal(VECTOR_SLOT(v, 0), "keyword");
	assert_true(is_quote(VECTOR_SLOT(v, 1)));;
	assert_string_equal(VECTOR_SLOT(v, 2), "\"");
	assert_true(is_quote(VECTOR_SLOT(v, 3)));;

	val = set_value(v);
	assert_string_equal(val, "\"");
#endif
	free(val);
	free_strvec(v);
}

int test_config_parser(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test01),
		cmocka_unit_test(test02),
		cmocka_unit_test(test03),
		cmocka_unit_test(test04),
		cmocka_unit_test(test05),
		cmocka_unit_test(test06),
		cmocka_unit_test(test07),
		cmocka_unit_test(test08),
		cmocka_unit_test(test09),
		cmocka_unit_test(test10),
		cmocka_unit_test(test11),
		cmocka_unit_test(test12),
		cmocka_unit_test(test13),
		cmocka_unit_test(test14),
		cmocka_unit_test(test15),
		cmocka_unit_test(test16),
		cmocka_unit_test(test17),
		cmocka_unit_test(test18),
	};
	return cmocka_run_group_tests(tests, setup, teardown);
}

int main(void)
{
	int ret = 0;

	ret += test_config_parser();
	return ret;
}
