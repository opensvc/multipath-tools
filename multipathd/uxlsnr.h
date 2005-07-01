struct pollfd *polls;

void free_polls(void);
void * uxsock_listen(char * (*uxsock_trigger)
			(char *, void * trigger_data),
			void * trigger_data);

