// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2015 Martin Wilck, SUSE LLC
 */
#include <cmocka.h>

#if CMOCKA_VERSION < 0x020000
#define assert_int_in_range(x, low, high) assert_in_range(x, low, high)
#define assert_uint_in_range(x, low, high) assert_in_range(x, low, high)
#define assert_uint_equal(x, y) assert_int_equal(x, y)
#define check_expected_int(x) check_expected(x)
#define check_expected_uint(x) check_expected(x)
#define expect_int_value(f, x, y) expect_value(f, x, y)
#define expect_uint_value(f, x, y) expect_value(f, x, y)
#define assert_int_in_set(x, vals, count) assert_in_set(x, vals, count)
#endif
