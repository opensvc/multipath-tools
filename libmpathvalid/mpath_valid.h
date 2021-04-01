/*
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * This file is part of the device-mapper multipath userspace tools.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIB_MPATH_VALID_H
#define LIB_MPATH_VALID_H

#ifdef __cpluscplus
extern "C" {
#endif

enum mpath_valid_mode {
	MPATH_DEFAULT,
	MPATH_STRICT,
	MPATH_SMART,
	MPATH_GREEDY,
	MPATH_MODE_ERROR,
};

/*
 * MPATH_IS_VALID_NO_CHECK is used to indicate that it is safe to skip
 * checks to see if the device has already been released to the system
 * for use by things other that multipath.
 * MPATH_IS_MAYBE_VALID is used to indicate that this device would
 * be a valid multipath path device if another device with the same
 * wwid existed */
enum mpath_valid_result {
	MPATH_IS_ERROR = -1,
	MPATH_IS_NOT_VALID,
	MPATH_IS_VALID,
	MPATH_IS_VALID_NO_CHECK,
	MPATH_IS_MAYBE_VALID,
};

enum mpath_valid_log_style {
	MPATH_LOG_STDERR = -1,		/* log to STDERR */
	MPATH_LOG_STDERR_TIMESTAMP,	/* log to STDERR, with timestamps */
	MPATH_LOG_SYSLOG,		/* log to system log */
};

enum mpath_valid_verbosity {
	MPATH_LOG_PRIO_NOLOG = -1,	/* log nothing */
	MPATH_LOG_PRIO_ERR,
	MPATH_LOG_PRIO_WARN,
	MPATH_LOG_PRIO_NOTICE,
	MPATH_LOG_PRIO_INFO,
	MPATH_LOG_PRIO_DEBUG,
};

/* Function declarations */

/*
 * DESCRIPTION:
 * 	Initialize the device mapper multipath configuration. This
 * 	function must be invoked before calling any other
 * 	libmpathvalid functions. Call mpathvalid_exit() to cleanup.
 * @verbosity: the logging level (mpath_valid_verbosity)
 * @log_style: the logging style (mpath_valid_log_style)
 *
 * RESTRICTIONS:
 * 	Calling mpathvalid_init() after calling mpathvalid_exit() has no
 * 	effect.
 *
 * RETURNS: 0 = Success, -1 = Failure
 */
int mpathvalid_init(int verbosity, int log_style);


/*
 * DESCRIPTION:
 * 	Reread the multipath configuration files and reinitalize
 * 	the device mapper multipath configuration. This function can
 * 	be called as many times as necessary.
 *
 * RETURNS: 0 = Success, -1 = Failure
 */
int mpathvalid_reload_config(void);


/*
 * DESCRIPTION:
 * 	Release the device mapper multipath configuration. This
 * 	function must be called to cleanup resoures allocated by
 * 	mpathvalid_init(). After calling this function, no futher
 * 	libmpathvalid functions may be called.
 *
 * RETURNS: 0 = Success, -1 = Failure
 */
int mpathvalid_exit(void);

/*
 * DESCRIPTION:
 * 	Return the configured find_multipaths claim mode, using the
 * 	configuration from either mpathvalid_init() or
 * 	mpathvalid_reload_config()
 *
 * RETURNS:
 * 	MPATH_STRICT, MPATH_SMART, MPATH_GREEDY, or MPATH_MODE_ERROR
 *
 * 	MPATH_STRICT     = find_multiapths (yes|on|no|off)
 * 	MPATH_SMART      = find_multipaths smart
 * 	MPATH_GREEDY     = find_multipaths greedy
 * 	MPATH_MODE_ERROR = multipath configuration not initialized
 */
unsigned int mpathvalid_get_mode(void);
/*
 * DESCRIPTION:
 * 	Return whether device-mapper multipath claims a path device,
 * 	using the configuration read from either mpathvalid_init() or
 * 	mpathvalid_reload_config(). If the device is either claimed or
 * 	potentially claimed (MPATH_IS_VALID, MPATH_IS_VALID_NO_CHECK,
 * 	or MPATH_IS_MAYBE_VALID) and wwid is not NULL, then *wiid will
 * 	be set to point to the wwid of device. If set, *wwid must be
 * 	freed by the caller. path_wwids is an obptional parameter that
 * 	points to an array of wwids, that were returned from previous
 * 	calls to mpathvalid_is_path(). These are wwids of existing
 * 	devices that are or potentially are claimed by device-mapper
 * 	multipath. path_wwids is used with the MPATH_SMART claim mode,
 *	to claim devices when another device with the same wwid exists.
 * 	nr_paths must either be set to the number of elements of
 * 	path_wwids, or 0, if path_wwids is NULL.
 * @name: The kernel name of the device. input argument
 * @mode: the find_multipaths claim mode (mpath_valid_mode). input argument
 * @wwid: address of a pointer to the path wwid, or NULL. Output argument.
 * 	  Set if path is/may be claimed. If set, must be freed by caller
 * @path_wwids: Array of pointers to path wwids, or NULL. input argument
 * @nr_paths: number of elements in path_wwids array. input argument.
 *
 * RETURNS: device claim result (mpath_valid_result)
 * 	    Also sets *wwid if wwid is not NULL, and the claim result is
 * 	    MPATH_IS_VALID, MPATH_IS_VALID_NO_CHECK, or
 * 	    MPATH_IS_MAYBE_VALID
 */
int mpathvalid_is_path(const char *name, unsigned int mode, char **wwid,
		       const char **path_wwids, unsigned int nr_paths);

#ifdef __cplusplus
}
#endif
#endif /* LIB_PATH_VALID_H */
