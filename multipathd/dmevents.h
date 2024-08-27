#ifndef DMEVENTS_H_INCLUDED
#define DMEVENTS_H_INCLUDED

#include "structs_vec.h"

int dmevent_poll_supported(void);
int init_dmevent_waiter(struct vectors *vecs);
void cleanup_dmevent_waiter(void);
int watch_dmevents(char *name);
void unwatch_all_dmevents(void);
void *wait_dmevents (void *unused);

#endif /* DMEVENTS_H_INCLUDED */
