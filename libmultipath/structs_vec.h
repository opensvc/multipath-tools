#ifndef STRUCTS_VEC_H_INCLUDED
#define STRUCTS_VEC_H_INCLUDED

#include "vector.h"
#include "config.h"
#include "lock.h"

struct vectors {
	vector pathvec;
	vector mpvec;
	struct mutex_lock lock; /* defined in lock.h */
};

void set_no_path_retry(struct multipath *mpp);

int adopt_paths (vector pathvec, struct multipath *mpp,
		 const struct multipath *current_mpp);
void orphan_path (struct path * pp, const char *reason);
void set_path_removed(struct path *pp);

int verify_paths(struct multipath *mpp);
int update_mpp_paths(struct multipath * mpp, vector pathvec);
int update_multipath_strings (struct multipath *mpp, vector pathvec);
void extract_hwe_from_path(struct multipath * mpp);

void remove_map (struct multipath *mpp, vector pathvec, vector mpvec);
void remove_map_by_alias(const char *alias, struct vectors * vecs);
void remove_maps (struct vectors * vecs);

void sync_map_state (struct multipath *);
struct multipath * add_map_with_path (struct vectors * vecs,
				      struct path * pp, int add_vec,
				      const struct multipath *current_mpp);
void update_queue_mode_del_path(struct multipath *mpp);
void update_queue_mode_add_path(struct multipath *mpp);
int update_multipath_table__ (struct multipath *mpp, vector pathvec, int flags,
			      const char *params, const char *status);
int update_multipath_table (struct multipath *mpp, vector pathvec, int flags);
int update_multipath_status (struct multipath *mpp);
vector get_used_hwes(const struct vector_s *pathvec);

#endif /* STRUCTS_VEC_H_INCLUDED */
