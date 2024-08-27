#ifndef GLOBALS_H_INCLUDED
#define GLOBALS_H_INCLUDED

struct config;

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
