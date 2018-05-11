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
#ifndef _DM_GENERIC_H
#define _DM_GENERIC_H
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

#endif /* _DM_GENERIC_H */
