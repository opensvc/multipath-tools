#ifndef _UXLSNR_H
#define _UXLSNR_H

#include <stdbool.h>

typedef int (uxsock_trigger_fn)(char *, char **, int *, bool, void *);

void uxsock_cleanup(void *arg);
void *uxsock_listen(uxsock_trigger_fn uxsock_trigger, long ux_sock,
		    void * trigger_data);

#endif
