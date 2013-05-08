#ifndef _UXLSNR_H
#define _UXLSNR_H

void * uxsock_listen(int (*uxsock_trigger)
			(char *, char **, int *, void *),
			void * trigger_data);

extern volatile sig_atomic_t reconfig_sig;
extern volatile sig_atomic_t log_reset_sig;
#endif

