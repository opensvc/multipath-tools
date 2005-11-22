#ifndef _STRUCTS_VEC_H
#define _STRUCTS_VEC_H

struct vectors {
#if DAEMON
	pthread_mutex_t *lock;
#endif
	vector pathvec;
	vector mpvec;
};

#if DAEMON
struct event_thread {
	struct dm_task *dmt;
	pthread_t thread;
	int event_nr;
	char mapname[WWID_SIZE];
	struct vectors *vecs;
	struct multipath *mpp;
};
#endif

typedef void (stop_waiter_thread_func) (struct multipath *, struct vectors *);
typedef int (start_waiter_thread_func) (struct multipath *, struct vectors *);

void set_no_path_retry(struct multipath *mpp);

int adopt_paths (vector pathvec, struct multipath * mpp);
void orphan_paths (vector pathvec, struct multipath * mpp);
void orphan_path (struct path * pp);

int verify_paths(struct multipath * mpp, struct vectors * vecs, vector rpvec);
int update_mpp_paths(struct multipath * mpp);
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

#endif /* _STRUCTS_VEC_H */
