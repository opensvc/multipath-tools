#ifndef TEST_LOG_H_INCLUDED
#define TEST_LOG_H_INCLUDED

__attribute__((format(printf, 2, 0)))
void __wrap_dlog (int prio, const char * fmt, ...);
void expect_condlog(int prio, char *string);

#endif
