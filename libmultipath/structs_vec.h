#ifndef _STRUCTS_VEC_H
#define _STRUCTS_VEC_H

#include "lock.h"
/*
struct mutex_lock {
	pthread_mutex_t *mutex;
	int depth;
}; */
struct vectors {
	struct mutex_lock lock; /* defined in lock.h */
	vector pathvec;
	vector mpvec;
};

void set_no_path_retry(struct multipath *mpp);

int adopt_paths (vector pathvec, struct multipath * mpp, int get_info);
void orphan_paths (vector pathvec, struct multipath * mpp);
void orphan_path (struct path * pp, const char *reason);

int verify_paths(struct multipath * mpp, struct vectors * vecs, vector rpvec);
int update_mpp_paths(struct multipath * mpp, vector pathvec);
int __setup_multipath (struct vectors * vecs, struct multipath * mpp,
		       int reset);
#define setup_multipath(vecs, mpp) __setup_multipath(vecs, mpp, 1)
int update_multipath_strings (struct multipath *mpp, vector pathvec);
	
void remove_map (struct multipath * mpp, struct vectors * vecs, int purge_vec);
void remove_map_and_stop_waiter (struct multipath * mpp, struct vectors * vecs, int purge_vec);
void remove_maps (struct vectors * vecs);
void remove_maps_and_stop_waiters (struct vectors * vecs);

struct multipath * add_map_without_path (struct vectors * vecs, char * alias);
struct multipath * add_map_with_path (struct vectors * vecs,
				struct path * pp, int add_vec);
int update_multipath (struct vectors *vecs, char *mapname, int reset);
void update_queue_mode_del_path(struct multipath *mpp);
void update_queue_mode_add_path(struct multipath *mpp);

#endif /* _STRUCTS_VEC_H */
