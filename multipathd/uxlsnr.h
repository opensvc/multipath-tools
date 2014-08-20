#ifndef _UXLSNR_H
#define _UXLSNR_H

typedef int (uxsock_trigger_fn)(char *, char **, int *, void *);

void * uxsock_listen(uxsock_trigger_fn uxsock_trigger,
		     void * trigger_data);

extern volatile sig_atomic_t reconfig_sig;
extern volatile sig_atomic_t log_reset_sig;

#endif

