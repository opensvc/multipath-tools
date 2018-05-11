/*
  Copyright (c) 2018 Martin Wilck, SUSE Linux GmbH

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <sys/types.h>
#include "generic.h"
#include "dm-generic.h"
#include "structs.h"
#include "structs_vec.h"
#include "config.h"
#include "print.h"

static const struct _vector*
dm_mp_get_pgs(const struct gen_multipath *gmp)
{
	return vector_convert(NULL, gen_multipath_to_dm(gmp)->pg,
			      struct pathgroup, dm_pathgroup_to_gen);
}

static void dm_mp_rel_pgs(const struct gen_multipath *gmp,
			  const struct _vector* v)
{
	vector_free_const(v);
}

static const struct _vector*
dm_pg_get_paths(const struct gen_pathgroup *gpg)
{
	return vector_convert(NULL, gen_pathgroup_to_dm(gpg)->paths,
			      struct path, dm_path_to_gen);
}

static void dm_mp_rel_paths(const struct gen_pathgroup *gpg,
			    const struct _vector* v)
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
