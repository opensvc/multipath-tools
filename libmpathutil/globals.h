#ifndef _GLOBALS_H
#define _GLOBALS_H

struct config;

/**
 * extern variable: udev
 *
 * A &struct udev instance used by libmultipath. libmultipath expects
 * a valid, initialized &struct udev in this variable.
 * An application can define this variable itself, in which case
 * the applications's instance will take precedence.
 * The application can initialize and destroy this variable by
 * calling libmultipath_init() and libmultipath_exit(), respectively,
 * whether or not it defines the variable itself.
 * An application can initialize udev with udev_new() before calling
 * libmultipath_init(), e.g. if it has to make libudev calls before
 * libmultipath calls. If an application wants to keep using the
 * udev variable after calling libmultipath_exit(), it should have taken
 * an additional reference on it beforehand. This is the case e.g.
 * after initializing udev with udev_new().
 */
extern struct udev *udev;

/*
 * libmultipath provides default implementations of
 * get_multipath_config() and put_multipath_config().
 * Applications using these should use init_config(file, NULL)
 * to load the configuration, rather than load_config(file).
 * Likewise, uninit_config() should be used for teardown, but
 * using free_config() for that is supported, too.
 * Applications can define their own {get,put}_multipath_config()
 * functions, which override the library-internal ones, but
 * could still call libmp_{get,put}_multipath_config().
 */
void put_multipath_config(void *);
struct config *get_multipath_config(void);

#endif
