#include <setjmp.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cmocka-compat.h"
#include "log.h"
#include "test-log.h"
#include "debug.h"


__attribute__((format(printf, 2, 0)))
void __wrap_dlog (int prio, const char * fmt, ...)
{
	char buff[MAX_MSG_SIZE];
	va_list ap;
	const char *expected;

	va_start(ap, fmt);
	vsnprintf(buff, MAX_MSG_SIZE, fmt, ap);
	va_end(ap);
	fprintf(stderr, "%s(%d): %s", __func__, prio, buff);
	expected = mock_ptr_type(const char *);
	if (memcmp(expected, buff, strlen(expected)))
		fprintf(stderr, "%s(expected): %s", __func__, expected);
	check_expected_int(prio);
	assert_memory_equal(buff, expected, strlen(expected));
}

void expect_condlog(int prio, char *string)
{
	if (prio > MAX_VERBOSITY || prio > libmp_verbosity)
		return;
	expect_int_value(__wrap_dlog, prio, prio);
	will_return(__wrap_dlog, string);
}
