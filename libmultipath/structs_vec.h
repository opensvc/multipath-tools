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

void enter_recovery_mode(struct multipath *mpp);

int adopt_paths (vector pathvec, struct multipath * mpp);
void orphan_paths(vector pathvec, struct multipath *mpp,
		  const char *reason);
void orphan_path (struct path * pp, const char *reason);

int verify_paths(struct multipath * mpp, struct vectors * vecs);
int update_mpp_paths(struct multipath * mpp, vector pathvec);
int update_multipath_strings (struct multipath *mpp, vector pathvec,
			      int is_daemon);
void extract_hwe_from_path(struct multipath * mpp);

#define PURGE_VEC 1

void remove_map (struct multipath * mpp, struct vectors * vecs, int purge_vec);
void remove_map_by_alias(const char *alias, struct vectors * vecs,
			 int purge_vec);
void remove_maps (struct vectors * vecs);

void sync_map_state (struct multipath *);
struct multipath * add_map_with_path (struct vectors * vecs,
				struct path * pp, int add_vec);
void update_queue_mode_del_path(struct multipath *mpp);
void update_queue_mode_add_path(struct multipath *mpp);
int update_multipath_table (struct multipath *mpp, vector pathvec,
			    int is_daemon);
int update_multipath_status (struct multipath *mpp);
vector get_used_hwes(const struct _vector *pathvec);

#endif /* _STRUCTS_VEC_H */
