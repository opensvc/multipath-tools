#ifndef UXLSNR_H_INCLUDED
#define UXLSNR_H_INCLUDED

#include <stdbool.h>

bool waiting_clients(void);
void uxsock_cleanup(void *arg);
void *uxsock_listen(int n_socks, long *ux_sock, void *trigger_data);

#endif
