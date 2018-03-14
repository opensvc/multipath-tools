#ifndef _WAITER_H
#define _WAITER_H

extern pthread_attr_t waiter_attr;

struct event_thread {
	struct dm_task *dmt;
	pthread_t thread;
	int event_nr;
	char mapname[WWID_SIZE];
	struct vectors *vecs;
};

void stop_waiter_thread (struct multipath *mpp, struct vectors *vecs);
int start_waiter_thread (struct multipath *mpp, struct vectors *vecs);

#endif /* _WAITER_H */
