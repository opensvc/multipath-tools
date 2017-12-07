#ifndef _STRUCTS_VEC_H
#define _STRUCTS_VEC_H

#include "vector.h"
#include "config.h"
#include "lock.h"

struct vectors {
	struct mutex_lock lock; /* defined in lock.h */
	vector pathvec;
	vector mpvec;
};

void set_no_path_retry(struct config *conf, struct multipath *mpp);

int adopt_paths (vector pathvec, struct multipath * mpp);
void orphan_paths (vector pathvec, struct multipath * mpp);
void orphan_path (struct path * pp, const char *reason);

int verify_paths(struct multipath * mpp, struct vectors * vecs);
int update_mpp_paths(struct multipath * mpp, vector pathvec);
int __setup_multipath (struct vectors * vecs, struct multipath * mpp,
		       int reset, int is_daemon);
#define setup_multipath(vecs, mpp) __setup_multipath(vecs, mpp, 1, 1)
int update_multipath_strings (struct multipath *mpp, vector pathvec,
			      int is_daemon);
void extract_hwe_from_path(struct multipath * mpp);

void remove_map (struct multipath * mpp, struct vectors * vecs, int purge_vec);
void remove_map_and_stop_waiter (struct multipath * mpp, struct vectors * vecs, int purge_vec);
void remove_maps (struct vectors * vecs);
void remove_maps_and_stop_waiters (struct vectors * vecs);

void sync_map_state (struct multipath *);
int update_map (struct multipath *mpp, struct vectors *vecs);
struct multipath * add_map_without_path (struct vectors * vecs, char * alias);
struct multipath * add_map_with_path (struct vectors * vecs,
				struct path * pp, int add_vec);
int update_multipath (struct vectors *vecs, char *mapname, int reset);
void update_queue_mode_del_path(struct multipath *mpp);
void update_queue_mode_add_path(struct multipath *mpp);
int update_multipath_table (struct multipath *mpp, vector pathvec,
			    int is_daemon);
int update_multipath_status (struct multipath *mpp);

#endif /* _STRUCTS_VEC_H */
