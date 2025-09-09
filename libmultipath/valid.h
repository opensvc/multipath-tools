// SPDX-License-Identifier: GPL-2.0-or-later
/*
  Copyright (c) 2020 Benjamin Marzinski, IBM
 */
#ifndef VALID_H_INCLUDED
#define VALID_H_INCLUDED

/*
 * PATH_IS_VALID_NO_CHECK is returned when multipath should claim
 * the path, regardless of whether is has been released to systemd
 * already.
 * PATH_IS_VALID is returned by is_path_valid, when the path is
 * valid only if it hasn't been released to systemd already.
 * PATH_IS_MAYBE_VALID is returned when the path would be valid
 * if other paths with the same wwid existed. It is up to the caller
 * to check for these other paths.
 */
enum is_path_valid_result {
	PATH_IS_ERROR = -1,
	PATH_IS_NOT_VALID,
	PATH_IS_VALID,
	PATH_IS_VALID_NO_CHECK,
	PATH_IS_MAYBE_VALID,
	PATH_MAX_VALID_RESULT, /* only for bounds checking */
};

int is_path_valid(const char *name, struct config *conf, struct path *pp,
		  bool check_multipathd);

#endif /* VALID_H_INCLUDED */
