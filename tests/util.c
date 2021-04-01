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
#include <endian.h>
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

static int test_basenamecpy(void)
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

/*
 * On big endian systems, if bitfield_t is 32bit, we need
 * to swap the two 32 bit parts of a 64bit value to make
 * the tests below work.
 */
static uint64_t maybe_swap(uint64_t v)
{
	uint32_t *s = (uint32_t *)&v;

	if (sizeof(bitfield_t) == 4)
		/* this is identity for little endian */
		return ((uint64_t)s[1] << 32) | s[0];
	else
		return v;
}

static void test_bitmask_1(void **state)
{
	struct bitfield *bf;
	uint64_t *arr;
	int i, j, k, m, b;

	bf = alloc_bitfield(BITARR_SZ * 64);
	assert_non_null(bf);
	assert_int_equal(bf->len, BITARR_SZ * 64);
	arr = (uint64_t *)bf->bits;

	for (j = 0; j < BITARR_SZ; j++) {
		for (i = 0; i < 64; i++) {
			b = 64 * j + i;
			assert(!is_bit_set_in_bitfield(b, bf));
			set_bit_in_bitfield(b, bf);
			for (k = 0; k < BITARR_SZ; k++) {
#if 0
				printf("b = %d j = %d k = %d a = %"PRIx64"\n",
				       b, j, k, arr[k]);
#endif
				if (k == j)
					assert_int_equal(maybe_swap(arr[j]), 1ULL << i);
				else
					assert_int_equal(arr[k], 0ULL);
			}
			for (m = 0; m < 64; m++)
				if (i == m)
					assert(is_bit_set_in_bitfield(64 * j + m,
								      bf));
				else
					assert(!is_bit_set_in_bitfield(64 * j + m,
								       bf));
			clear_bit_in_bitfield(b, bf);
			assert(!is_bit_set_in_bitfield(b, bf));
			for (k = 0; k < BITARR_SZ; k++)
				assert_int_equal(arr[k], 0ULL);
		}
	}
	free(bf);
}

static void test_bitmask_2(void **state)
{
	struct bitfield *bf;
	uint64_t *arr;
	int i, j, k, m, b;

	bf = alloc_bitfield(BITARR_SZ * 64);
	assert_non_null(bf);
	assert_int_equal(bf->len, BITARR_SZ * 64);
	arr = (uint64_t *)bf->bits;

	for (j = 0; j < BITARR_SZ; j++) {
		for (i = 0; i < 64; i++) {
			b = 64 * j + i;
			assert(!is_bit_set_in_bitfield(b, bf));
			set_bit_in_bitfield(b, bf);
			for (m = 0; m < 64; m++)
				if (m <= i)
					assert(is_bit_set_in_bitfield(64 * j + m,
								      bf));
				else
					assert(!is_bit_set_in_bitfield(64 * j + m,
								       bf));
			assert(is_bit_set_in_bitfield(b, bf));
			for (k = 0; k < BITARR_SZ; k++) {
				if (k < j || (k == j && i == 63))
					assert_int_equal(arr[k], ~0ULL);
				else if (k > j)
					assert_int_equal(arr[k], 0ULL);
				else
					assert_int_equal(
						maybe_swap(arr[k]),
						(1ULL << (i + 1)) - 1);
			}
		}
	}
	for (j = 0; j < BITARR_SZ; j++) {
		for (i = 0; i < 64; i++) {
			b = 64 * j + i;
			assert(is_bit_set_in_bitfield(b, bf));
			clear_bit_in_bitfield(b, bf);
			for (m = 0; m < 64; m++)
				if (m <= i)
					assert(!is_bit_set_in_bitfield(64 * j + m,
								       bf));
				else
					assert(is_bit_set_in_bitfield(64 * j + m,
								      bf));
			assert(!is_bit_set_in_bitfield(b, bf));
			for (k = 0; k < BITARR_SZ; k++) {
				if (k < j || (k == j && i == 63))
					assert_int_equal(arr[k], 0ULL);
				else if (k > j)
					assert_int_equal(arr[k], ~0ULL);
				else
					assert_int_equal(
						maybe_swap(arr[k]),
						~((1ULL << (i + 1)) - 1));
			}
		}
	}
	free(bf);
}

/*
 *  Test operations on a 0-length bitfield
 */
static void test_bitmask_len_0(void **state)
{
	struct bitfield *bf;

	bf = alloc_bitfield(0);
	assert_null(bf);
}

/*
 * We use uint32_t in the "small bitmask" tests below.
 * This means that we may have to swap 32bit words if bitfield_t
 * is 64bit wide.
 */
static unsigned int maybe_swap_idx(unsigned int i)
{
	if (BYTE_ORDER == LITTLE_ENDIAN || sizeof(bitfield_t) == 4)
		return i;
	else
		/* 0<->1, 2<->3, ... */
		return i + (i % 2 == 0 ? 1 : -1);
}

static void _test_bitmask_small(unsigned int n)
{
	struct bitfield *bf;
	uint32_t *arr;
	unsigned int size = maybe_swap_idx((n - 1) / 32) + 1, i;

	assert(sizeof(bitfield_t) == 4 || sizeof(bitfield_t) == 8);
	assert(n <= 64);
	assert(n >= 1);

	bf = alloc_bitfield(n);
	assert_non_null(bf);
	assert_int_equal(bf->len, n);
	arr = (uint32_t *)bf->bits;

	for (i = 0; i < size; i++)
		assert_int_equal(arr[i], 0);

	set_bit_in_bitfield(n + 1, bf);
	for (i = 0; i < size; i++)
		assert_int_equal(arr[i], 0);

	set_bit_in_bitfield(n, bf);
	for (i = 0; i < size; i++)
		assert_int_equal(arr[i], 0);

	set_bit_in_bitfield(n - 1, bf);
	for (i = 0; i < size; i++) {
		unsigned int k = (n - 1) / 32;
		unsigned int j = (n - 1) - k * 32;
		unsigned int i1 = maybe_swap_idx(i);

		if (i == k)
			assert_int_equal(arr[i1], 1UL << j);
		else
			assert_int_equal(arr[i1], 0);
	}

	clear_bit_in_bitfield(n - 1, bf);
	for (i = 0; i < size; i++)
		assert_int_equal(arr[i], 0);

	set_bit_in_bitfield(0, bf);
	assert_int_equal(arr[maybe_swap_idx(0)], 1);
	for (i = 1; i < size; i++)
		assert_int_equal(arr[maybe_swap_idx(i)], 0);

	free(bf);
}

static void _test_bitmask_small_2(unsigned int n)
{
	struct bitfield *bf;
	uint32_t *arr;
	unsigned int size = maybe_swap_idx((n - 1) / 32) + 1, i;

	assert(n <= 128);
	assert(n >= 65);

	bf = alloc_bitfield(n);
	assert_non_null(bf);
	assert_int_equal(bf->len, n);
	arr = (uint32_t *)bf->bits;

	for (i = 0; i < size; i++)
		assert_int_equal(arr[i], 0);

	set_bit_in_bitfield(n + 1, bf);
	for (i = 0; i < size; i++)
		assert_int_equal(arr[i], 0);

	set_bit_in_bitfield(n, bf);
	for (i = 0; i < size; i++)
		assert_int_equal(arr[i], 0);

	set_bit_in_bitfield(n - 1, bf);
	assert_int_equal(arr[0], 0);
	for (i = 0; i < size; i++) {
		unsigned int k = (n - 1) / 32;
		unsigned int j = (n - 1) - k * 32;
		unsigned int i1 = maybe_swap_idx(i);

		if (i == k)
			assert_int_equal(arr[i1], 1UL << j);
		else
			assert_int_equal(arr[i1], 0);
	}

	set_bit_in_bitfield(0, bf);
	for (i = 0; i < size; i++) {
		unsigned int k = (n - 1) / 32;
		unsigned int j = (n - 1) - k * 32;
		unsigned int i1 = maybe_swap_idx(i);

		if (i == k && k == 0)
			assert_int_equal(arr[i1], (1UL << j) | 1);
		else if (i == k)
			assert_int_equal(arr[i1], 1UL << j);
		else if (i == 0)
			assert_int_equal(arr[i1], 1);
		else
			assert_int_equal(arr[i1], 0);
	}

	set_bit_in_bitfield(64, bf);
	for (i = 0; i < size; i++) {
		unsigned int k = (n - 1) / 32;
		unsigned int j = (n - 1) - k * 32;
		unsigned int i1 = maybe_swap_idx(i);

		if (i == k && (k == 0 || k == 2))
			assert_int_equal(arr[i1], (1UL << j) | 1);
		else if (i == k)
			assert_int_equal(arr[i1], 1UL << j);
		else if (i == 2 || i == 0)
			assert_int_equal(arr[i1], 1);
		else
			assert_int_equal(arr[i1], 0);
	}

	clear_bit_in_bitfield(0, bf);
	for (i = 0; i < size; i++) {
		unsigned int k = (n - 1) / 32;
		unsigned int j = (n - 1) - k * 32;
		unsigned int i1 = maybe_swap_idx(i);

		if (i == k && k == 2)
			assert_int_equal(arr[i1], (1UL << j) | 1);
		else if (i == k)
			assert_int_equal(arr[i1], 1UL << j);
		else if (i == 2)
			assert_int_equal(arr[i1], 1);
		else
			assert_int_equal(arr[i1], 0);
	}

	free(bf);
}

static void test_bitmask_len_1(void **state)
{
	_test_bitmask_small(1);
}

static void test_bitmask_len_2(void **state)
{
	_test_bitmask_small(2);
}

static void test_bitmask_len_3(void **state)
{
	_test_bitmask_small(3);
}

static void test_bitmask_len_23(void **state)
{
	_test_bitmask_small(23);
}

static void test_bitmask_len_63(void **state)
{
	_test_bitmask_small(63);
}

static void test_bitmask_len_64(void **state)
{
	_test_bitmask_small(63);
}

static void test_bitmask_len_65(void **state)
{
	_test_bitmask_small_2(65);
}

static void test_bitmask_len_66(void **state)
{
	_test_bitmask_small_2(66);
}

static void test_bitmask_len_67(void **state)
{
	_test_bitmask_small_2(67);
}

static void test_bitmask_len_103(void **state)
{
	_test_bitmask_small_2(103);
}

static void test_bitmask_len_126(void **state)
{
	_test_bitmask_small_2(126);
}

static void test_bitmask_len_127(void **state)
{
	_test_bitmask_small_2(127);
}

static void test_bitmask_len_128(void **state)
{
	_test_bitmask_small_2(128);
}


static int test_bitmasks(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_bitmask_1),
		cmocka_unit_test(test_bitmask_2),
		cmocka_unit_test(test_bitmask_len_0),
		cmocka_unit_test(test_bitmask_len_1),
		cmocka_unit_test(test_bitmask_len_2),
		cmocka_unit_test(test_bitmask_len_3),
		cmocka_unit_test(test_bitmask_len_23),
		cmocka_unit_test(test_bitmask_len_63),
		cmocka_unit_test(test_bitmask_len_64),
		cmocka_unit_test(test_bitmask_len_65),
		cmocka_unit_test(test_bitmask_len_66),
		cmocka_unit_test(test_bitmask_len_67),
		cmocka_unit_test(test_bitmask_len_103),
		cmocka_unit_test(test_bitmask_len_126),
		cmocka_unit_test(test_bitmask_len_127),
		cmocka_unit_test(test_bitmask_len_128),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}

#define DST_STR "Hello"
static const char dst_str[] = DST_STR;
/* length of src_str and dst_str should be different */
static const char src_str[] = " World";
/* Must be big enough to hold dst_str and src_str */
#define ARRSZ 16
#define FILL '@'

/* strlcpy with length 0 */
static void test_strlcpy_0(void **state)
{
	char tst[] = DST_STR;
	int rc;

	rc = strlcpy(tst, src_str, 0);
	assert_int_equal(rc, strlen(src_str));
	assert_string_equal(tst, dst_str);
}

/* strlcpy with length 1 */
static void test_strlcpy_1(void **state)
{
	char tst[] = DST_STR;
	int rc;

	rc = strlcpy(tst, src_str, 1);
	assert_int_equal(rc, strlen(src_str));
	assert_int_equal(tst[0], '\0');
	assert_string_equal(tst + 1, dst_str + 1);
}

/* strlcpy with length 2 */
static void test_strlcpy_2(void **state)
{
	char tst[] = DST_STR;
	int rc;

	rc = strlcpy(tst, src_str, 2);
	assert_int_equal(rc, strlen(src_str));
	assert_int_equal(tst[0], src_str[0]);
	assert_int_equal(tst[1], '\0');
	assert_string_equal(tst + 2, dst_str + 2);
}

/* strlcpy with dst length < src length */
static void test_strlcpy_3(void **state)
{
	char tst[] = DST_STR;
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
	const int sz = sizeof(src_str);

	tst = malloc(sz);
	assert_non_null(tst);
	memset(tst, 'f', sizeof(src_str));

	rc = strlcpy(tst, src_str, sz);
	assert_int_equal(rc, strlen(src_str));
	assert_string_equal(src_str, tst);

	free(tst);
}

/* strlcpy with dst length > src length, dst not terminated */
static void test_strlcpy_6(void **state)
{
	char *tst;
	int rc;
	const int sz = sizeof(src_str);

	tst = malloc(sz + 2);
	assert_non_null(tst);
	memset(tst, 'f', sz + 2);

	rc = strlcpy(tst, src_str, sz + 2);
	assert_int_equal(rc, strlen(src_str));
	assert_string_equal(src_str, tst);
	assert_int_equal(tst[sz], 'f');
	assert_int_equal(tst[sz + 1], 'f');

	free(tst);
}

/* strlcpy with empty src */
static void test_strlcpy_7(void **state)
{
	char tst[] = DST_STR;
	static const char empty[] = "";
	int rc;

	rc = strlcpy(tst, empty, sizeof(tst));
	assert_int_equal(rc, strlen(empty));
	assert_string_equal(empty, tst);
	assert_string_equal(tst + 1, dst_str + 1);
}

/* strlcpy with empty src, length 0 */
static void test_strlcpy_8(void **state)
{
	char tst[] = DST_STR;
	static const char empty[] = "";
	int rc;

	rc = strlcpy(tst, empty, 0);
	assert_int_equal(rc, strlen(empty));
	assert_string_equal(dst_str, tst);
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


/* 0-terminated string, filled with non-0 after the terminator */
static void prep_buf(char *buf, size_t size, const char *word)
{
	memset(buf, FILL, size);
	assert_in_range(strlen(word), 0, size - 1);
	memcpy(buf, word, strlen(word) + 1);
}

/* strlcat with size 0, dst not 0-terminated  */
static void test_strlcat_0(void **state)
{
	char tst[ARRSZ];
	int rc;

	prep_buf(tst, sizeof(tst), dst_str);
	rc = strlcat(tst, src_str, 0);
	assert_int_equal(rc, strlen(src_str));
	assert_string_equal(tst, dst_str);
	assert_int_equal(tst[sizeof(dst_str)], FILL);
}

/* strlcat with length 1, dst not 0-terminated */
static void test_strlcat_1(void **state)
{
	char tst[ARRSZ];
	int rc;

	prep_buf(tst, sizeof(tst), dst_str);
	rc = strlcat(tst, src_str, 1);
	assert_int_equal(rc, 1 + strlen(src_str));
	assert_string_equal(tst, dst_str);
	assert_int_equal(tst[sizeof(dst_str)], FILL);
}

/* strlcat with length = dst - 1 */
static void test_strlcat_2(void **state)
{
	char tst[ARRSZ];
	int rc;

	prep_buf(tst, sizeof(tst), dst_str);
	rc = strlcat(tst, src_str, strlen(dst_str));
	assert_int_equal(rc, strlen(src_str) + strlen(dst_str));
	assert_string_equal(tst, dst_str);
	assert_int_equal(tst[sizeof(dst_str)], FILL);
}

/* strlcat with length = dst */
static void test_strlcat_3(void **state)
{
	char tst[ARRSZ];
	int rc;

	prep_buf(tst, sizeof(tst), dst_str);
	rc = strlcat(tst, src_str, strlen(dst_str) + 1);
	assert_int_equal(rc, strlen(src_str) + strlen(dst_str));
	assert_string_equal(tst, dst_str);
	assert_int_equal(tst[sizeof(dst_str)], FILL);
}

/* strlcat with len = dst + 1 */
static void test_strlcat_4(void **state)
{
	char tst[ARRSZ];
	int rc;

	prep_buf(tst, sizeof(tst), dst_str);
	rc = strlcat(tst, src_str, strlen(dst_str) + 2);
	assert_int_equal(rc, strlen(src_str) + strlen(dst_str));
	assert_false(strncmp(tst, dst_str, strlen(dst_str)));
	assert_int_equal(tst[strlen(dst_str)], src_str[0]);
	assert_int_equal(tst[strlen(dst_str) + 1], '\0');
	assert_int_equal(tst[strlen(dst_str) + 2], FILL);
}

/* strlcat with len = needed - 1 */
static void test_strlcat_5(void **state)
{
	char tst[ARRSZ];
	int rc;

	prep_buf(tst, sizeof(tst), dst_str);
	rc = strlcat(tst, src_str, strlen(dst_str) + strlen(src_str));
	assert_int_equal(rc, strlen(src_str) + strlen(dst_str));
	assert_false(strncmp(tst, dst_str, strlen(dst_str)));
	assert_false(strncmp(tst + strlen(dst_str), src_str,
			     strlen(src_str) - 1));
	assert_int_equal(tst[strlen(dst_str) + strlen(src_str) - 1], '\0');
	assert_int_equal(tst[strlen(dst_str) + strlen(src_str)], FILL);
}

/* strlcat with exactly sufficient space */
static void test_strlcat_6(void **state)
{
	char tst[ARRSZ];
	int rc;

	prep_buf(tst, sizeof(tst), dst_str);
	rc = strlcat(tst, src_str, strlen(dst_str) + strlen(src_str) + 1);
	assert_int_equal(rc, strlen(src_str) + strlen(dst_str));
	assert_false(strncmp(tst, dst_str, strlen(dst_str)));
	assert_string_equal(tst + strlen(dst_str), src_str);
	assert_int_equal(tst[strlen(dst_str) + strlen(src_str) + 1], FILL);
}

/* strlcat with sufficient space */
static void test_strlcat_7(void **state)
{
	char tst[ARRSZ];
	int rc;

	prep_buf(tst, sizeof(tst), dst_str);
	rc = strlcat(tst, src_str, sizeof(tst));
	assert_int_equal(rc, strlen(src_str) + strlen(dst_str));
	assert_false(strncmp(tst, dst_str, strlen(dst_str)));
	assert_string_equal(tst + strlen(dst_str), src_str);
}

/* strlcat with 0-length string */
static void test_strlcat_8(void **state)
{
	char tst[ARRSZ];
	int rc;

	prep_buf(tst, sizeof(tst), dst_str);
	rc = strlcat(tst, "", sizeof(tst));
	assert_int_equal(rc, strlen(dst_str));
	assert_string_equal(tst, dst_str);
	assert_int_equal(tst[sizeof(dst_str)], FILL);
}

/* strlcat with empty dst */
static void test_strlcat_9(void **state)
{
	char tst[ARRSZ];
	int rc;

	prep_buf(tst, sizeof(tst), "");
	rc = strlcat(tst, src_str, ARRSZ);
	assert_int_equal(rc, strlen(src_str));
	assert_string_equal(tst, src_str);
	assert_int_equal(tst[sizeof(src_str)], FILL);
}

/* strlcat with empty dst and src */
static void test_strlcat_10(void **state)
{
	char tst[ARRSZ];
	int rc;

	prep_buf(tst, sizeof(tst), "");
	rc = strlcat(tst, "", ARRSZ);
	assert_int_equal(rc, 0);
	assert_string_equal(tst, "");
	assert_int_equal(tst[1], FILL);
}

/* strlcat with no space to store 0 */
static void test_strlcat_11(void **state)
{
	char tst[ARRSZ];
	int rc;

	prep_buf(tst, sizeof(tst), "");
	tst[0] = FILL;
	rc = strlcat(tst, src_str, 0);
	assert_int_equal(rc, strlen(src_str));
	assert_int_equal(tst[0], FILL);
}

static int test_strlcat(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_strlcat_0),
		cmocka_unit_test(test_strlcat_1),
		cmocka_unit_test(test_strlcat_2),
		cmocka_unit_test(test_strlcat_3),
		cmocka_unit_test(test_strlcat_4),
		cmocka_unit_test(test_strlcat_5),
		cmocka_unit_test(test_strlcat_6),
		cmocka_unit_test(test_strlcat_7),
		cmocka_unit_test(test_strlcat_8),
		cmocka_unit_test(test_strlcat_9),
		cmocka_unit_test(test_strlcat_10),
		cmocka_unit_test(test_strlcat_11),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

static void test_strchop_nochop(void **state)
{
	char hello[] = "hello";

	assert_int_equal(strchop(hello), 5);
	assert_string_equal(hello, "hello");
}

static void test_strchop_newline(void **state)
{
	char hello[] = "hello\n";

	assert_int_equal(strchop(hello), 5);
	assert_string_equal(hello, "hello");
}

static void test_strchop_space(void **state)
{
	char hello[] = " ello      ";

	assert_int_equal(strchop(hello), 5);
	assert_string_equal(hello, " ello");
}

static void test_strchop_mix(void **state)
{
	char hello[] = " el\no \t  \n\n \t    \n";

	assert_int_equal(strchop(hello), 5);
	assert_string_equal(hello, " el\no");
}

static void test_strchop_blank(void **state)
{
	char hello[] = "  \t  \n\n \t    \n";

	assert_int_equal(strchop(hello), 0);
	assert_string_equal(hello, "");
}

static void test_strchop_empty(void **state)
{
	char hello[] = "";

	assert_int_equal(strchop(hello), 0);
	assert_string_equal(hello, "");
}

static int test_strchop(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_strchop_nochop),
		cmocka_unit_test(test_strchop_newline),
		cmocka_unit_test(test_strchop_space),
		cmocka_unit_test(test_strchop_mix),
		cmocka_unit_test(test_strchop_blank),
		cmocka_unit_test(test_strchop_empty),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

int main(void)
{
	int ret = 0;

	init_test_verbosity(-1);
	ret += test_basenamecpy();
	ret += test_bitmasks();
	ret += test_strlcpy();
	ret += test_strlcat();
	ret += test_strchop();
	return ret;
}
