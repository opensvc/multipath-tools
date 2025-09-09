// SPDX-License-Identifier: GPL-2.0-or-later
/*
  Copyright (c) 2018 Martin Wilck, SUSE Linux GmbH
 */
#ifndef DM_GENERIC_H_INCLUDED
#define DM_GENERIC_H_INCLUDED
#include "generic.h"
#include "list.h" /* for container_of */
#include "structs.h"

#define dm_multipath_to_gen(mpp) (&((mpp)->generic_mp))
#define gen_multipath_to_dm(gm) \
	container_of_const((gm), struct multipath, generic_mp)

#define dm_pathgroup_to_gen(pg) (&(pg->generic_pg))
#define gen_pathgroup_to_dm(gpg) \
	container_of_const((gpg), struct pathgroup, generic_pg)

#define dm_path_to_gen(pp) (&((pp)->generic_path))
#define gen_path_to_dm(gp) \
	container_of_const((gp), struct path, generic_path)

extern const struct gen_multipath_ops dm_gen_multipath_ops;
extern const struct gen_pathgroup_ops dm_gen_pathgroup_ops;
extern const struct gen_path_ops dm_gen_path_ops;

#endif /* DM_GENERIC_H_INCLUDED */
