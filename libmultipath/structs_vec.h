#ifndef _STRUCTS_VEC_H
#define _STRUCTS_VEC_H

#include "vector.h"
#include "config.h"
#include "lock.h"

struct vectors {
	vector pathvec;
	vector mpvec;
	struct mutex_lock lock; /* defined in lock.h */
};

void __set_no_path_retry(struct multipath *mpp, bool check_features);
#define set_no_path_retry(mpp) __set_no_path_retry(mpp, true)

int adopt_paths (vector pathvec, struct multipath * mpp);
void orphan_path (struct path * pp, const char *reason);
void set_path_removed(struct path *pp);

int verify_paths(struct multipath *mpp);
bool update_pathvec_from_dm(vector pathvec, struct multipath *mpp,
			    int pathinfo_flags);
int update_mpp_paths(struct multipath * mpp, vector pathvec);
int update_multipath_strings (struct multipath *mpp, vector pathvec);
void extract_hwe_from_path(struct multipath * mpp);

void remove_map (struct multipath *mpp, vector pathvec, vector mpvec);
void remove_map_by_alias(const char *alias, struct vectors * vecs);
void remove_maps (struct vectors * vecs);

void sync_map_state (struct multipath *);
struct multipath * add_map_with_path (struct vectors * vecs,
				struct path * pp, int add_vec);
void update_queue_mode_del_path(struct multipath *mpp);
void update_queue_mode_add_path(struct multipath *mpp);
int update_multipath_table (struct multipath *mpp, vector pathvec, int flags);
int update_multipath_status (struct multipath *mpp);
vector get_used_hwes(const struct _vector *pathvec);

#endif /* _STRUCTS_VEC_H */
