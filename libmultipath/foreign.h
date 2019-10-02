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
#ifndef _FOREIGN_H
#define _FOREIGN_H
#include <stdbool.h>
#include <libudev.h>

#define LIBMP_FOREIGN_API ((1 << 8) | 0)

struct context;

/* return codes of functions below returning "int" */
enum foreign_retcode {
	FOREIGN_OK,
	FOREIGN_CLAIMED,
	FOREIGN_IGNORED,
	FOREIGN_UNCLAIMED,
	FOREIGN_NODEV,
	FOREIGN_ERR,
	__LAST_FOREIGN_RETCODE,
};

/**
 * Foreign multipath library API
 * Foreign libraries must implement the following methods.
 */
struct foreign {
	/**
	 * method: init(api, name)
	 * Initialize foreign library, and check API compatibility
	 * return pointer to opaque internal data strucure if successful,
	 * NULL otherwise.
	 *
	 * @param[in] api: API version
	 * @param[in] name: name to use for references to self in log messages,
	 *     doesn't need to be strdup'd
	 * @returns context pointer to use in future method calls.
	 */
	struct context* (*init)(unsigned int api, const char *name);

	/**
	 * method: cleanup(context)
	 * Free data structures used by foreign library, including
	 * context itself.
	 *
	 * @param[in] context foreign library context. This shouldn't be
	 * referenced any more after calling cleanup().
	 */
	void (*cleanup)(struct context *);

	/**
	 * method: add(context, udev)
	 * This is called during path detection, and for udev ADD events.
	 *
	 * @param[in] context foreign library context
	 * @param[in] udev udev device to add
	 * @returns status code
	 * @retval FOREIGN_CLAIMED: device newly claimed
	 * @retval FOREIGN_OK: device already registered, no action taken
	 * @retval FOREIGN_IGNORED: device is ignored, no action taken
	 * @retval FOREIGN_ERR: an error occurred (e.g. out-of-memory)
	 */
	int (*add)(struct context *, struct udev_device *);

	/**
	 * method: change
	 * This is called on udev CHANGE events.
	 *
	 * @param[in] context foreign library context
	 * @param[in] udev udev device that has generated the event
	 * @returns status code
	 * @retval FOREIGN_OK: event processed
	 * @retval FOREIGN_IGNORED: the device is ignored
	 * @retval FOREIGN_ERR: an error occurred (e.g. out-of-memory)
	 *
	 * Note: theoretically it can happen that the status of a foreign device
	 * (claimed vs. not claimed) changes in a change event.
	 * Supporting this correctly would require big efforts. For now, we
	 * don't support it. "multipathd reconfigure" starts foreign device
	 * detection from scratch and should be able to handle this situation.
	 */
	int (*change)(struct context *, struct udev_device *);

	/**
	 * method: delete
	 * This is called on udev DELETE events.
	 *
	 * @param[in] context foreign library context
	 * @param[in] udev udev device that has generated the event and
	 *	should be deleted
	 * @returns status code
	 * @retval FOREIGN_OK: processed correctly (device deleted)
	 * @retval FOREIGN_IGNORED: device wasn't registered internally
	 * @retval FOREIGN_ERR: error occurred.
	 */
	int (*delete)(struct context *, struct udev_device *);

	/**
	 * method: delete_all
	 * This is called if multipathd reconfigures itself.
	 * Deletes all registered devices (maps and paths)
	 *
	 * @param[in] context foreign library context
	 * @returns status code
	 * @retval FOREIGN_OK: processed correctly
	 * @retval FOREIGN_IGNORED: nothing to delete
	 * @retval FOREIGN_ERR: error occurred
	 */
	int (*delete_all)(struct context*);

	/**
	 * method: check
	 * This is called from multipathd's checker loop.
	 *
	 * Check status of managed devices, update internal status, and print
	 * log messages if appropriate.
	 * @param[in] context foreign library context
	 */
	void (*check)(struct context *);

	/**
	 * lock internal data structures.
	 * @param[in] ctx: foreign context
	 */
	void (*lock)(struct context *ctx);

	/**
	 * unlock internal data structures.
	 * @param[in] ctx: foreign context (void* in order to use the function
	 *	as argument to pthread_cleanup_push())
	 */
	void (*unlock)(void *ctx);

	/**
	 * method: get_multipaths(context)
	 * Returned vector must be freed by calling release_multipaths().
	 * Lock must be held until release_multipaths() is called.
	 *
	 * @param[in] context foreign library context
	 * @returns a vector of "struct gen_multipath*" with the map devices
	 * belonging to this library (see generic.h).
	 */
	const struct _vector* (*get_multipaths)(const struct context *);

	/**
	 * method: release_multipaths(context, mpvec)
	 * release data structures obtained with get_multipaths (if any)
	 *
	 * @param[in] ctx the foreign context
	 * @param[in] mpvec the vector allocated with get_multipaths()
	 */
	void (*release_multipaths)(const struct context *ctx,
				   const struct _vector* mpvec);

	/**
	 * method: get_paths
	 * Returned vector must be freed by calling release_paths().
	 * Lock must be held until release_paths() is called.
	 *
	 * @param[in] context foreign library context
	 * @returns a vector of "struct gen_path*" with the path devices
	 * belonging to this library (see generic.h)
	 */
	const struct _vector* (*get_paths)(const struct context *);

	/**
	 * release data structures obtained with get_multipaths (if any)
	 *
	 * @param[in] ctx the foreign context
	 * @param[in] ppvec the vector allocated with get_paths()
	 */
	void (*release_paths)(const struct context *ctx,
			      const struct _vector* ppvec);

	void *handle;
	struct context *context;
	const char name[0];
};

/**
 * init_foreign(dir)
 * load and initialize foreign multipath libraries in dir (libforeign-*.so).
 * @param dir: directory to search
 * @param enable: regex to match foreign library name ("*" above) against
 * @returns: 0 on success, negative value on failure.
 */
int init_foreign(const char *multipath_dir, const char *enable);

/**
 * cleanup_foreign(dir)
 * cleanup and free all data structures owned by foreign libraries
 */
void cleanup_foreign(void);

/**
 * add_foreign(udev)
 * check if a device belongs to any foreign library.
 * calls add() for all known foreign libs, in the order registered,
 * until the first one returns FOREIGN_CLAIMED or FOREIGN_OK.
 * @param udev: udev device to check
 * @returns: status code
 * @retval FOREIGN_CLAIMED: newly claimed by a foreign lib
 * @retval FOREIGN_OK: already claimed by a foreign lib
 * @retval FOREIGN_IGNORED: ignored by all foreign libs
 * @retval FOREIGN_ERR: an error occurred
 */
int add_foreign(struct udev_device *);

/**
 * change_foreign(udev)
 * Notify foreign libraries of an udev CHANGE event
 * @param udev: udev device to check
 * @returns: status code (see change() method above).
 */
int change_foreign(struct udev_device *);

/**
 * delete_foreign(udev)
 * @param udev: udev device being removed
 * @returns: status code (see remove() above)
 */
int delete_foreign(struct udev_device *);

/**
 * delete_all_foreign()
 * call delete_all() for all foreign libraries
 * @returns: status code (see delete_all() above)
 */
int delete_all_foreign(void);

/**
 * check_foreign()
 * call check() (see above) for all foreign libraries
 */
void check_foreign(void);

/**
 * foreign_path_layout()
 * call this before printing paths, after get_path_layout(), to determine
 * output field width.
 */
void foreign_path_layout(void);

/**
 * foreign_multipath_layout()
 * call this before printing maps, after get_multipath_layout(), to determine
 * output field width.
 */
void foreign_multipath_layout(void);

/**
 * snprint_foreign_topology(buf, len, verbosity);
 * prints topology information from foreign libraries into buffer,
 * '\0' - terminated.
 * @param buf: output buffer
 * @param len: size of output buffer
 * @param verbosity: verbosity level
 * @returns: number of printed characters excluding trailing '\0'.
 */
int snprint_foreign_topology(char *buf, int len, int verbosity);

/**
 * snprint_foreign_paths(buf, len, style, pad);
 * prints formatted path information from foreign libraries into buffer,
 * '\0' - terminated.
 * @param buf: output buffer
 * @param len: size of output buffer
 * @param style: format string
 * @param pad: whether to pad field width
 * @returns: number of printed characters excluding trailing '\0'.
 */
int snprint_foreign_paths(char *buf, int len, const char *style, int pad);

/**
 * snprint_foreign_multipaths(buf, len, style, pad);
 * prints formatted map information from foreign libraries into buffer,
 * '\0' - terminated.
 * @param buf: output buffer
 * @param len: size of output buffer
 * @param style: format string
 * @param pad: whether to pad field width
 * @returns: number of printed characters excluding trailing '\0'.
 */
int snprint_foreign_multipaths(char *buf, int len,
			       const char *style, int pretty);

/**
 * print_foreign_topology(v)
 * print foreign topology to stdout
 * @param verbosity: verbosity level
 */
void print_foreign_topology(int verbosity);

/**
 * is_claimed_by_foreign(ud)
 * @param udev: udev device
 * @returns: true if device is (newly or already) claimed by a foreign lib
 */
static inline bool
is_claimed_by_foreign(struct udev_device *ud)
{
	int rc = add_foreign(ud);

	return (rc == FOREIGN_CLAIMED || rc == FOREIGN_OK);
}

#endif /*  _FOREIGN_H */
