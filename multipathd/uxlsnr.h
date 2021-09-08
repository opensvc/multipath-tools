#ifndef _UXLSNR_H
#define _UXLSNR_H

#include <stdbool.h>

void uxsock_cleanup(void *arg);
void *uxsock_listen(long ux_sock,
		    void * trigger_data);

#endif
