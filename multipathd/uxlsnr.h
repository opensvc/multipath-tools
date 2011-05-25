#ifndef _UXLSNR_H
#define _UXLSNR_H

void * uxsock_listen(int (*uxsock_trigger)
			(char *, char **, int *, void *),
			void * trigger_data);
#endif

