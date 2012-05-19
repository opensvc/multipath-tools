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

struct event_thread * alloc_waiter (void);
void free_waiter (void *data);
void stop_waiter_thread (struct multipath *mpp, struct vectors *vecs);
int start_waiter_thread (struct multipath *mpp, struct vectors *vecs);
int waiteventloop (struct event_thread *waiter);
void *waitevent (void *et);

#endif /* _WAITER_H */
