#include <setjmp.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>
#include "log.h"
#include "test-log.h"
#include "debug.h"


__attribute__((format(printf, 2, 0)))
void __wrap_dlog (int prio, const char * fmt, ...)
{
	char buff[MAX_MSG_SIZE];
	va_list ap;
	char *expected;

	check_expected(prio);
	va_start(ap, fmt);
	vsnprintf(buff, MAX_MSG_SIZE, fmt, ap);
	va_end(ap);
	expected = mock_ptr_type(char *);
	assert_memory_equal(buff, expected, strlen(expected));
}

void expect_condlog(int prio, char *string)
{
	if (prio > MAX_VERBOSITY || prio > libmp_verbosity)
		return;
	expect_value(__wrap_dlog, prio, prio);
	will_return(__wrap_dlog, string);
}
