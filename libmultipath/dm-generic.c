// SPDX-License-Identifier: GPL-2.0-or-later
/*
  Copyright (c) 2018 Martin Wilck, SUSE Linux GmbH
 */

#include <stdint.h>
#include <sys/types.h>
#include "generic.h"
#include "dm-generic.h"
#include "structs.h"
#include "structs_vec.h"
#include "config.h"
#include "print.h"

static const struct vector_s*
dm_mp_get_pgs(const struct gen_multipath *gmp)
{
	return vector_convert(NULL, gen_multipath_to_dm(gmp)->pg,
			      struct pathgroup, dm_pathgroup_to_gen);
}

static void dm_mp_rel_pgs(__attribute__((unused))
			  const struct gen_multipath *gmp,
			  const struct vector_s* v)
{
	vector_free_const(v);
}

static const struct vector_s*
dm_pg_get_paths(const struct gen_pathgroup *gpg)
{
	return vector_convert(NULL, gen_pathgroup_to_dm(gpg)->paths,
			      struct path, dm_path_to_gen);
}

static void dm_mp_rel_paths(__attribute__((unused))
			    const struct gen_pathgroup *gpg,
			    const struct vector_s* v)
{
	vector_free_const(v);
}

const struct gen_multipath_ops dm_gen_multipath_ops = {
	.get_pathgroups = dm_mp_get_pgs,
	.rel_pathgroups = dm_mp_rel_pgs,
	.snprint = snprint_multipath_attr,
	.style = snprint_multipath_style,
};

const struct gen_pathgroup_ops dm_gen_pathgroup_ops = {
	.get_paths = dm_pg_get_paths,
	.rel_paths = dm_mp_rel_paths,
	.snprint = snprint_pathgroup_attr,
};

const struct gen_path_ops dm_gen_path_ops = {
	.snprint = snprint_path_attr,
};
