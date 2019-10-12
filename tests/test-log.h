#ifndef _TEST_LOG_H
#define _TEST_LOG_H

void __wrap_dlog (int sink, int prio, const char * fmt, ...);
void expect_condlog(int prio, char *string);

#endif
