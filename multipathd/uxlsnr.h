struct pollfd *polls;

void free_polls(void);
void * uxsock_listen(int (*uxsock_trigger)
			(char *, char **, int *, void *),
			void * trigger_data);

