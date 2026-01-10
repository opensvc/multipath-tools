// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 Brian Bunker <brian@purestorage.com>
 * Copyright (C) 2025 Krishna Kant <krishna.kant@purestorage.com>
 */

#ifndef PURGE_H_INCLUDED
#define PURGE_H_INCLUDED

#include <pthread.h>
#include "list.h"

struct vectors;

/*
 * Purge thread synchronization.
 * The checker thread builds a list of paths to purge and queues them here.
 * The purge thread picks up the queue and processes it.
 */
extern pthread_mutex_t purge_mutex;
extern pthread_cond_t purge_cond;
extern struct list_head purge_queue;

/*
 * Build a list of paths to purge and add them to tmpq. Called by checker
 * thread while holding vecs->lock.
 */
void build_purge_list(struct vectors *vecs, struct list_head *tmpq);

/*
 * Cleanup handler for purge list. Frees all purge_path_info entries.
 * Can be called as a pthread cleanup handler or directly for shutdown cleanup.
 */
void cleanup_purge_list(void *arg);

/*
 * Main purge thread loop
 */
void *purgeloop(void *ap);

#endif /* PURGE_H_INCLUDED */
