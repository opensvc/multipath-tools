void condlog (int prio, char * fmt, ...);

#if DAEMON
#include <pthread.h>
#include "../multipathd/log_pthread.h"
#define condlog(prio, fmt, args...) \
	log_safe(prio + 3, fmt, ##args)
#endif
