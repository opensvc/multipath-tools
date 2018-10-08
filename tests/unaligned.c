#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <cmocka.h>
#include "unaligned.h"

#define SIZE 16
static const char memory[8] = {
	0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef
};

static const uint64_t intval64 = 0x0123456789abcdef;
static const uint32_t intval32 = 0x01234567;
static const uint16_t intval16 = 0x0123;

#include "globals.c"

static int setup(void **state)
{
	return posix_memalign(state, 16, 2 * SIZE);
}

static int teardown(void **state)
{
	free(*state);
	return 0;
}


#define make_test(bits, offset) \
	static void test_ ## bits ## _ ## offset(void **state)	\
{								\
	int len = bits/8;					\
	uint8_t *c = *state;					\
	uint8_t *p = *state + SIZE;				\
	uint64_t u;						\
								\
	assert_in_range(len, 1, SIZE);				\
	assert_in_range(offset + len, 1, SIZE);			\
	memset(c, 0, 2 * SIZE);					\
	memcpy(c + offset, memory, len);			\
								\
	u = get_unaligned_be##bits(c + offset);			\
	assert_int_equal(u, intval##bits);			\
	put_unaligned_be##bits(u, p + offset);			\
	assert_memory_equal(c + offset, p  + offset, len);	\
}

make_test(16, 0);
make_test(16, 1);
make_test(32, 0);
make_test(32, 1);
make_test(32, 2);
make_test(32, 3);
make_test(64, 0);
make_test(64, 1);
make_test(64, 2);
make_test(64, 3);
make_test(64, 4);
make_test(64, 5);
make_test(64, 6);
make_test(64, 7);

int test_unaligned(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_16_0),
		cmocka_unit_test(test_16_1),
		cmocka_unit_test(test_32_0),
		cmocka_unit_test(test_32_1),
		cmocka_unit_test(test_32_2),
		cmocka_unit_test(test_32_3),
		cmocka_unit_test(test_64_0),
		cmocka_unit_test(test_64_1),
		cmocka_unit_test(test_64_2),
		cmocka_unit_test(test_64_3),
		cmocka_unit_test(test_64_4),
		cmocka_unit_test(test_64_5),
		cmocka_unit_test(test_64_6),
		cmocka_unit_test(test_64_7),
	};
	return cmocka_run_group_tests(tests, setup, teardown);
}

int main(void)
{
	int ret = 0;

	ret += test_unaligned();
	return ret;
}
