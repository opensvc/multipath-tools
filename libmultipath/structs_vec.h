#ifndef _STRUCTS_VEC_H
#define _STRUCTS_VEC_H

struct vectors {
#if DAEMON
	pthread_mutex_t *lock;
#endif
	vector pathvec;
	vector mpvec;
};

typedef void (stop_waiter_thread_func) (struct multipath *, struct vectors *);
typedef int (start_waiter_thread_func) (struct multipath *, struct vectors *);

void set_no_path_retry(struct multipath *mpp);

int adopt_paths (vector pathvec, struct multipath * mpp);
void orphan_paths (vector pathvec, struct multipath * mpp);
void orphan_path (struct path * pp);

int verify_paths(struct multipath * mpp, struct vectors * vecs, vector rpvec);
int update_mpp_paths(struct multipath * mpp, vector pathvec);
int setup_multipath (struct vectors * vecs, struct multipath * mpp);
int update_multipath_strings (struct multipath *mpp, vector pathvec);
	
void remove_map (struct multipath * mpp, struct vectors * vecs,
		 stop_waiter_thread_func *stop_waiter, int purge_vec);
void remove_maps (struct vectors * vecs,
		  stop_waiter_thread_func *stop_waiter);

struct multipath * add_map_without_path (struct vectors * vecs,
				int minor, char * alias,
				start_waiter_thread_func *start_waiter);
struct multipath * add_map_with_path (struct vectors * vecs,
				struct path * pp, int add_vec);
int update_multipath (struct vectors *vecs, char *mapname);
void update_queue_mode_del_path(struct multipath *mpp);
void update_queue_mode_add_path(struct multipath *mpp);

#endif /* _STRUCTS_VEC_H */
