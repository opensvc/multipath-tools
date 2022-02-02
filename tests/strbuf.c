/*
 * Copyright (c) 2021 SUSE LLC
 * SPDX-License-Identifier: GPL-2.0-only
 */

#define _GNU_SOURCE
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <cmocka.h>
#include <errno.h>
#include "strbuf.h"
#include "debug.h"
#include "globals.c"

void *__real_realloc(void *ptr, size_t size);

static bool mock_realloc = false;
void *__wrap_realloc(void *ptr, size_t size)
{
	void *p;
	if (!mock_realloc)
		return __real_realloc(ptr, size);

	p = mock_ptr_type(void *);
	condlog(4, "%s: %p, %zu -> %p", __func__, ptr, size, p);
	return p;
}

static void test_strbuf_00(void **state)
{
	STRBUF_ON_STACK(buf);
	char *p;

	assert_ptr_equal(buf.buf, NULL);
	assert_int_equal(buf.size, 0);
	assert_int_equal(buf.offs, 0);
	assert_int_equal(get_strbuf_len(&buf), 0);
	assert_string_equal(get_strbuf_str(&buf), "");
	p = steal_strbuf_str(&buf);
	assert_ptr_equal(p, NULL);

	assert_ptr_equal(buf.buf, NULL);
	assert_int_equal(buf.size, 0);
	assert_int_equal(buf.offs, 0);
	assert_int_equal(get_strbuf_len(&buf), 0);
	assert_string_equal(get_strbuf_str(&buf), "");

	assert_int_equal(append_strbuf_str(&buf, "moin"), 4);
	assert_int_equal(get_strbuf_len(&buf), 4);
	assert_in_range(buf.size, 5, SIZE_MAX);
	assert_string_equal(get_strbuf_str(&buf), "moin");
	p = steal_strbuf_str(&buf);
	assert_string_equal(p, "moin");
	free(p);

	assert_ptr_equal(buf.buf, NULL);
	assert_int_equal(buf.size, 0);
	assert_int_equal(buf.offs, 0);
	assert_int_equal(get_strbuf_len(&buf), 0);
	assert_string_equal(get_strbuf_str(&buf), "");

	assert_int_equal(append_strbuf_str(&buf, NULL), -EINVAL);
	assert_int_equal(buf.size, 0);
	assert_int_equal(buf.offs, 0);
	assert_int_equal(get_strbuf_len(&buf), 0);
	assert_string_equal(get_strbuf_str(&buf), "");

	assert_int_equal(append_strbuf_str(&buf, ""), 0);
	/* appending a 0-length string allocates memory */
	assert_in_range(buf.size, 1, SIZE_MAX);
	assert_int_equal(buf.offs, 0);
	assert_int_equal(get_strbuf_len(&buf), 0);
	assert_string_equal(get_strbuf_str(&buf), "");
	p = steal_strbuf_str(&buf);
	assert_string_equal(p, "");
	free(p);

	assert_int_equal(__append_strbuf_str(&buf, "x", 0), 0);
	/* appending a 0-length string allocates memory */
	assert_in_range(buf.size, 1, SIZE_MAX);
	assert_int_equal(buf.offs, 0);
	assert_int_equal(get_strbuf_len(&buf), 0);
	assert_string_equal(get_strbuf_str(&buf), "");
}

static void test_strbuf_alloc_err(void **state)
{
	STRBUF_ON_STACK(buf);
	size_t sz, ofs;
	int rc;

	mock_realloc = true;
	will_return(__wrap_realloc, NULL);
	assert_int_equal(append_strbuf_str(&buf, "moin"), -ENOMEM);
	assert_int_equal(buf.size, 0);
	assert_int_equal(buf.offs, 0);
	assert_int_equal(get_strbuf_len(&buf), 0);
	assert_string_equal(get_strbuf_str(&buf), "");

	mock_realloc = false;
	assert_int_equal(append_strbuf_str(&buf, "moin"), 4);
	sz = buf.size;
	assert_in_range(sz, 5, SIZE_MAX);
	assert_int_equal(buf.offs, 4);
	assert_int_equal(get_strbuf_len(&buf), 4);
	assert_string_equal(get_strbuf_str(&buf), "moin");

	mock_realloc = true;
	will_return(__wrap_realloc, NULL);
	ofs = get_strbuf_len(&buf);
	while ((rc = append_strbuf_str(&buf, " hello")) >= 0) {
		condlog(3, "%s", get_strbuf_str(&buf));
		assert_int_equal(rc, 6);
		assert_int_equal(get_strbuf_len(&buf), ofs + 6);
		assert_memory_equal(get_strbuf_str(&buf), "moin", 4);
		assert_string_equal(get_strbuf_str(&buf) + ofs, " hello");
		ofs = get_strbuf_len(&buf);
	}
	assert_int_equal(rc, -ENOMEM);
	assert_int_equal(buf.size, sz);
	assert_int_equal(get_strbuf_len(&buf), ofs);
	assert_memory_equal(get_strbuf_str(&buf), "moin", 4);
	assert_string_equal(get_strbuf_str(&buf) + ofs - 6, " hello");

	reset_strbuf(&buf);
	assert_ptr_equal(buf.buf, NULL);
	assert_int_equal(buf.size, 0);
	assert_int_equal(buf.offs, 0);
	assert_int_equal(get_strbuf_len(&buf), 0);
	assert_string_equal(get_strbuf_str(&buf), "");

	mock_realloc = false;
}

static void test_strbuf_overflow(void **state)
{
	STRBUF_ON_STACK(buf);

	assert_int_equal(append_strbuf_str(&buf, "x"), 1);
	/* fake huge buffer */
	buf.size = SIZE_MAX - 1;
	buf.offs = buf.size - 1;
	assert_int_equal(append_strbuf_str(&buf, "x"), -EOVERFLOW);
}

static void test_strbuf_big(void **state)
{
	STRBUF_ON_STACK(buf);
	const char big[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789\n";
	char *bbig;
	int i;

	/* Under valgrind, 30000 iterations need ca. 30s on my laptop */
	for (i = 0; i < 30000; i++) {
		if (i % 1000 == 0)
			condlog(4, "%d", i);
		assert_int_equal(append_strbuf_str(&buf, big), sizeof(big) - 1);
		assert_int_equal(get_strbuf_len(&buf), (sizeof(big) - 1) * (i + 1));
		assert_memory_equal(get_strbuf_str(&buf), big, sizeof(big) - 1);
		assert_string_equal(get_strbuf_str(&buf) + get_strbuf_len(&buf)
				    - (sizeof(big) - 1), big);
	};
	bbig = steal_strbuf_str(&buf);

	assert_ptr_equal(buf.buf, NULL);
	assert_int_equal(buf.size, 0);
	assert_int_equal(buf.offs, 0);
	assert_int_equal(get_strbuf_len(&buf), 0);
	assert_string_equal(get_strbuf_str(&buf), "");

	assert_int_equal(strlen(bbig), i * (sizeof(big) - 1));
	assert_memory_equal(bbig, big, sizeof(big) - 1);
	free(bbig);
}

static void test_strbuf_nul(void **state)
{
	STRBUF_ON_STACK(buf);
	char greet[] = "hello, sir!";

	assert_int_equal(__append_strbuf_str(&buf, greet, 6), 6);
	assert_string_equal(get_strbuf_str(&buf), "hello,");
	assert_int_equal(__append_strbuf_str(&buf, greet, 6), 6);
	assert_string_equal(get_strbuf_str(&buf), "hello,hello,");

	/* overwrite comma with NUL; append_strbuf_str() stops at NUL byte */
	greet[5] = '\0';
	reset_strbuf(&buf);
	assert_int_equal(append_strbuf_str(&buf, greet), 5);
	assert_int_equal(get_strbuf_len(&buf), 5);
	assert_string_equal(get_strbuf_str(&buf), "hello");
	assert_int_equal(append_strbuf_str(&buf, greet), 5);
	assert_int_equal(get_strbuf_len(&buf), 10);
	assert_string_equal(get_strbuf_str(&buf), "hellohello");

	/* __append_strbuf_str() appends full memory, including NUL bytes */
	reset_strbuf(&buf);
	assert_int_equal(__append_strbuf_str(&buf, greet, sizeof(greet) - 1),
			 sizeof(greet) - 1);
	assert_int_equal(get_strbuf_len(&buf), sizeof(greet) - 1);
	assert_string_equal(get_strbuf_str(&buf), "hello");
	assert_string_equal(get_strbuf_str(&buf) + get_strbuf_len(&buf) - 5, " sir!");
	assert_int_equal(__append_strbuf_str(&buf, greet, sizeof(greet) - 1),
			 sizeof(greet) - 1);
	assert_string_equal(get_strbuf_str(&buf), "hello");
	assert_int_equal(get_strbuf_len(&buf), 2 * (sizeof(greet) - 1));
	assert_string_equal(get_strbuf_str(&buf) + get_strbuf_len(&buf) - 5, " sir!");
}

static void test_strbuf_quoted(void **state)
{
	STRBUF_ON_STACK(buf);
	const char said[] = "She said ";
	const char greet[] = "hi, man!";
	char *p;
	size_t n;

	assert_int_equal(append_strbuf_str(&buf, said), sizeof(said) - 1);
	assert_int_equal(append_strbuf_quoted(&buf, greet), sizeof(greet) + 1);
	assert_string_equal(get_strbuf_str(&buf), "She said \"hi, man!\"");
	n = get_strbuf_len(&buf);
	p = steal_strbuf_str(&buf);
	assert_int_equal(append_strbuf_str(&buf, said), sizeof(said) - 1);
	assert_int_equal(append_strbuf_quoted(&buf, p), n + 4);
	assert_string_equal(get_strbuf_str(&buf),
			    "She said \"She said \"\"hi, man!\"\"\"");
	free(p);
	n = get_strbuf_len(&buf);
	p = steal_strbuf_str(&buf);
	assert_int_equal(append_strbuf_str(&buf, said), sizeof(said) - 1);
	assert_int_equal(append_strbuf_quoted(&buf, p), n + 8);
	assert_string_equal(get_strbuf_str(&buf),
			    "She said \"She said \"\"She said \"\"\"\"hi, man!\"\"\"\"\"\"\"");
	free(p);
}

static void test_strbuf_escaped(void **state)
{
	STRBUF_ON_STACK(buf);
	const char said[] = "She said \"hi, man\"";

	assert_int_equal(append_strbuf_quoted(&buf, said), sizeof(said) + 3);
	assert_string_equal(get_strbuf_str(&buf),
			    "\"She said \"\"hi, man\"\"\"");

	reset_strbuf(&buf);
	assert_int_equal(append_strbuf_quoted(&buf, "\""), 4);
	assert_string_equal(get_strbuf_str(&buf), "\"\"\"\"");

	reset_strbuf(&buf);
	assert_int_equal(append_strbuf_quoted(&buf, "\"\""), 6);
	assert_string_equal(get_strbuf_str(&buf), "\"\"\"\"\"\"");

	reset_strbuf(&buf);
	assert_int_equal(append_strbuf_quoted(&buf, "\"Hi\""), 8);
	assert_string_equal(get_strbuf_str(&buf), "\"\"\"Hi\"\"\"");
}

#define SENTENCE "yields, preceded by itself, falsehood"
static void test_print_strbuf(void **state)
{
	STRBUF_ON_STACK(buf);
	char sentence[] = SENTENCE;

	assert_int_equal(print_strbuf(&buf, "\"%s\" %s.", sentence, sentence),
			 2 * (sizeof(sentence) - 1) + 4);
	assert_string_equal(get_strbuf_str(&buf),
			    "\"" SENTENCE "\" " SENTENCE ".");
	condlog(3, "%s", get_strbuf_str(&buf));

	reset_strbuf(&buf);
	assert_int_equal(print_strbuf(&buf, "0x%08x", 0xdeadbeef), 10);
	assert_string_equal(get_strbuf_str(&buf), "0xdeadbeef");

	reset_strbuf(&buf);
	assert_int_equal(print_strbuf(&buf, "%d%% of %d is %0.2f",
				      5, 100, 0.05), 17);
	assert_string_equal(get_strbuf_str(&buf), "5% of 100 is 0.05");
}

static void test_truncate_strbuf(void **state)
{
	STRBUF_ON_STACK(buf);
	const char str[] = "hello my dear!\n";
	size_t sz, sz1;

	assert_int_equal(truncate_strbuf(&buf, 1), -EFAULT);
	assert_int_equal(truncate_strbuf(&buf, 0), -EFAULT);

	assert_int_equal(append_strbuf_str(&buf, str), sizeof(str) - 1);
	assert_int_equal(get_strbuf_len(&buf), sizeof(str) - 1);
	assert_string_equal(get_strbuf_str(&buf), str);

	assert_int_equal(truncate_strbuf(&buf, sizeof(str)), -ERANGE);
	assert_int_equal(get_strbuf_len(&buf), sizeof(str) - 1);
	assert_string_equal(get_strbuf_str(&buf), str);

	assert_int_equal(truncate_strbuf(&buf, sizeof(str) - 1), 0);
	assert_int_equal(get_strbuf_len(&buf), sizeof(str) - 1);
	assert_string_equal(get_strbuf_str(&buf), str);

	assert_int_equal(truncate_strbuf(&buf, sizeof(str) - 2), 0);
	assert_int_equal(get_strbuf_len(&buf), sizeof(str) - 2);
	assert_string_not_equal(get_strbuf_str(&buf), str);
	assert_memory_equal(get_strbuf_str(&buf), str, sizeof(str) - 2);

	assert_int_equal(truncate_strbuf(&buf, 5), 0);
	assert_int_equal(get_strbuf_len(&buf), 5);
	assert_string_not_equal(get_strbuf_str(&buf), str);
	assert_string_equal(get_strbuf_str(&buf), "hello");

	reset_strbuf(&buf);
	assert_int_equal(append_strbuf_str(&buf, str), sizeof(str) - 1);

	sz = buf.size;
	while (buf.size == sz)
		assert_int_equal(append_strbuf_str(&buf, str), sizeof(str) - 1);

	sz1  = buf.size;
	assert_in_range(get_strbuf_len(&buf), sz + 1, SIZE_MAX);
	assert_string_equal(get_strbuf_str(&buf) +
			    get_strbuf_len(&buf) - (sizeof(str) - 1), str);
	assert_int_equal(truncate_strbuf(&buf, get_strbuf_len(&buf) + 1),
			 -ERANGE);
	assert_int_equal(truncate_strbuf(&buf, get_strbuf_len(&buf)), 0);
	assert_int_equal(truncate_strbuf(&buf, get_strbuf_len(&buf)
					 - (sizeof(str) - 1)), 0);
	assert_in_range(get_strbuf_len(&buf), 1, sz);
	assert_string_equal(get_strbuf_str(&buf) +
			    get_strbuf_len(&buf) - (sizeof(str) - 1), str);
	assert_int_equal(buf.size, sz1);

	assert_int_equal(truncate_strbuf(&buf, 5), 0);
	assert_int_equal(get_strbuf_len(&buf), 5);
	assert_string_equal(get_strbuf_str(&buf), "hello");
	assert_int_equal(buf.size, sz1);

	assert_int_equal(truncate_strbuf(&buf, 0), 0);
	assert_int_equal(get_strbuf_len(&buf), 0);
	assert_string_equal(get_strbuf_str(&buf), "");
	assert_int_equal(buf.size, sz1);
}

static void test_fill_strbuf(void **state)
{
	STRBUF_ON_STACK(buf);
	int i;
	char *p;

	assert_int_equal(fill_strbuf(&buf, '+', -5), -EINVAL);

	assert_int_equal(fill_strbuf(&buf, '+', 0), 0);
	assert_int_equal(get_strbuf_len(&buf), 0);
	assert_string_equal(get_strbuf_str(&buf), "");

	assert_int_equal(fill_strbuf(&buf, '+', 1), 1);
	assert_int_equal(get_strbuf_len(&buf), 1);
	assert_string_equal(get_strbuf_str(&buf), "+");

	assert_int_equal(fill_strbuf(&buf, '-', 3), 3);
	assert_int_equal(get_strbuf_len(&buf), 4);
	assert_string_equal(get_strbuf_str(&buf), "+---");

	assert_int_equal(fill_strbuf(&buf, '\0', 3), 3);
	assert_int_equal(get_strbuf_len(&buf), 7);
	assert_string_equal(get_strbuf_str(&buf), "+---");

	truncate_strbuf(&buf, 4);
	assert_int_equal(fill_strbuf(&buf, '+', 4), 4);
	assert_int_equal(get_strbuf_len(&buf), 8);
	assert_string_equal(get_strbuf_str(&buf), "+---++++");

	reset_strbuf(&buf);
	assert_int_equal(fill_strbuf(&buf, 'x', 30000), 30000);
	assert_int_equal(get_strbuf_len(&buf), 30000);
	p = steal_strbuf_str(&buf);
	assert_int_equal(strlen(p), 30000);
	for (i = 0; i < 30000; i++)
		assert_int_equal(p[i], 'x');
	free(p);
}

static int test_strbuf(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_strbuf_00),
		cmocka_unit_test(test_strbuf_alloc_err),
		cmocka_unit_test(test_strbuf_overflow),
		cmocka_unit_test(test_strbuf_big),
		cmocka_unit_test(test_strbuf_nul),
		cmocka_unit_test(test_strbuf_quoted),
		cmocka_unit_test(test_strbuf_escaped),
		cmocka_unit_test(test_print_strbuf),
		cmocka_unit_test(test_truncate_strbuf),
		cmocka_unit_test(test_fill_strbuf),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

int main(void)
{
	int ret = 0;

	init_test_verbosity(-1);
	ret += test_strbuf();
	return ret;
}
