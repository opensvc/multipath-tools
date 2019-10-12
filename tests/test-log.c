#include <setjmp.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <cmocka.h>
#include "log.h"
#include "test-log.h"

void __wrap_dlog (int sink, int prio, const char * fmt, ...)
{
	char buff[MAX_MSG_SIZE];
	va_list ap;

	assert_int_equal(prio, mock_type(int));
	va_start(ap, fmt);
	vsnprintf(buff, MAX_MSG_SIZE, fmt, ap);
	va_end(ap);
	assert_string_equal(buff, mock_ptr_type(char *));
}

void expect_condlog(int prio, char *string)
{
	will_return(__wrap_dlog, prio);
	will_return(__wrap_dlog, string);
}

