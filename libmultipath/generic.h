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
#ifndef _GENERIC_H
#define _GENERIC_H
#include "vector.h"

struct gen_multipath;
struct gen_pathgroup;
struct gen_path;

/**
 * Methods implemented for gen_multipath "objects"
 */
struct gen_multipath_ops {
	/**
	 * method: get_pathgroups(gmp)
	 * caller is responsible to returned data using rel_pathgroups()
	 * caller is also responsible to lock the gmp (directly or indirectly)
	 * while working with the return value.
	 * @param gmp: generic multipath object to act on
	 * @returns a vector of const struct gen_pathgroup*
	 */
	const struct _vector* (*get_pathgroups)(const struct gen_multipath*);
	/**
	 * method: rel_pathgroups(gmp, v)
	 * free data allocated by get_pathgroups(), if any
	 * @param gmp: generic multipath object to act on
	 * @param v the value returned by get_pathgroups()
	 */
	void (*rel_pathgroups)(const struct gen_multipath*,
			       const struct _vector*);
	/**
	 * method: snprint(gmp, buf, len, wildcard)
	 * prints the property of the multipath map matching
	 * the passed-in wildcard character into "buf",
	 * 0-terminated, no more than "len" characters including trailing '\0'.
	 *
	 * @param gmp: generic multipath object to act on
	 * @param buf: output buffer
	 * @param buflen: buffer size
	 * @param wildcard: the multipath wildcard (see print.c)
	 * @returns the number of characters printed (without trailing '\0').
	 */
	int (*snprint)(const struct gen_multipath*,
		       char *buf, int len, char wildcard);
	/**
	 * method: style(gmp, buf, len, verbosity)
	 * returns the format string to be used for the multipath object,
	 * defined with the wildcards as defined in print.c
	 * generic_style() should work well in most cases.
	 * @param gmp: generic multipath object to act on
	 * @param buf: output buffer
	 * @param buflen: buffer size
	 * @param verbosity: verbosity level
	 * @returns number of format chars printed
	 */
	int (*style)(const struct gen_multipath*,
		     char *buf, int len, int verbosity);
};

/**
 * Methods implemented for gen_pathgroup "objects"
 */
struct gen_pathgroup_ops {
	/**
	 * method: get_paths(gpg)
	 * caller is responsible to returned data using rel_paths()
	 * @param gpg: generic pathgroup object to act on
	 * @returns a vector of const struct gen_path*
	 */
	const struct _vector* (*get_paths)(const struct gen_pathgroup*);
	/**
	 * method: rel_paths(gpg, v)
	 * free data allocated by get_paths(), if any
	 * @param gmp: generic pathgroup object to act on
	 * @param v the value returned by get_paths()
	 */
	void (*rel_paths)(const struct gen_pathgroup*, const struct _vector*);
	/**
	 * Method snprint()
	 * see gen_multipath_ops->snprint() above
	 */
	int (*snprint)(const struct gen_pathgroup*,
		       char *buf, int len, char wildcard);
};

struct gen_path_ops {
	/**
	 * Method snprint()
	 * see gen_multipath_ops->snprint() above
	 */
	int (*snprint)(const struct gen_path*,
		       char *buf, int len, char wildcard);
};

struct gen_multipath {
	const struct gen_multipath_ops *ops;
};

struct gen_pathgroup {
	const struct gen_pathgroup_ops *ops;
};

struct gen_path {
	const struct gen_path_ops *ops;
};

/**
 * Helper functions for setting up the various generic_X_ops
 */

/**
 * generic_style()
 * A simple style() method (see above) that should fit most
 * foreign libraries.
 */
int generic_style(const struct gen_multipath*,
		  char *buf, int len, int verbosity);

#endif /* _GENERIC_H */
