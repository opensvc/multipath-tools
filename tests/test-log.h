#ifndef _TEST_LOG_H
#define _TEST_LOG_H

__attribute__((format(printf, 2, 0)))
void __wrap_dlog (int prio, const char * fmt, ...);
void expect_condlog(int prio, char *string);

#endif
