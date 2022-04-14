#ifndef _CONFIG_H
#define _CONFIG_H

#include <sys/types.h>
#include <stdint.h>
#include <urcu.h>
#include <inttypes.h>
#include "byteorder.h"

#define ORIGIN_DEFAULT 0
#define ORIGIN_CONFIG  1

enum devtypes {
	DEV_NONE,
	DEV_DEVT,
	DEV_DEVNODE,
	DEV_DEVMAP,
	DEV_UEVENT
};

enum mpath_cmds {
	CMD_NONE,
	CMD_CREATE,
	CMD_DRY_RUN,
	CMD_LIST_SHORT,
	CMD_LIST_LONG,
	CMD_VALID_PATH,
	CMD_REMOVE_WWID,
	CMD_RESET_WWIDS,
	CMD_ADD_WWID,
	CMD_USABLE_PATHS,
	CMD_DUMP_CONFIG,
	CMD_FLUSH_ONE,
	CMD_FLUSH_ALL,
};

enum force_reload_types {
	FORCE_RELOAD_NONE,
	FORCE_RELOAD_YES,
	FORCE_RELOAD_WEAK,
};

#define PCE_INVALID -1
struct pcentry {
	int type;
	int fast_io_fail;
	unsigned int dev_loss;
	int eh_deadline;
};

struct hwentry {
	char * vendor;
	char * product;
	char * revision;
	char * uid_attribute;
	char * getuid;
	char * features;
	char * hwhandler;
	char * selector;
	char * checker_name;
	char * prio_name;
	char * prio_args;
	char * alias_prefix;

	int pgpolicy;
	int pgfailback;
	int rr_weight;
	int no_path_retry;
	int minio;
	int minio_rq;
	int flush_on_last_del;
	int fast_io_fail;
	unsigned int dev_loss;
	int eh_deadline;
	int user_friendly_names;
	int retain_hwhandler;
	int detect_prio;
	int detect_checker;
	int deferred_remove;
	int delay_watch_checks;
	int delay_wait_checks;
	int san_path_err_threshold;
	int san_path_err_forget_rate;
	int san_path_err_recovery_time;
	int marginal_path_err_sample_time;
	int marginal_path_err_rate_threshold;
	int marginal_path_err_recheck_gap_time;
	int marginal_path_double_failed_time;
	int skip_kpartx;
	int max_sectors_kb;
	int ghost_delay;
	int all_tg_pt;
	int vpd_vendor_id;
	int recheck_wwid;
	char * bl_product;

	vector pctable;
};

struct mpentry {
	char * wwid;
	char * alias;
	char * uid_attribute;
	char * getuid;
	char * selector;
	char * features;

	char * prio_name;
	char * prio_args;
	int prkey_source;
	struct be64 reservation_key;
	uint8_t sa_flags;
	int pgpolicy;
	int pgfailback;
	int rr_weight;
	int no_path_retry;
	int minio;
	int minio_rq;
	int flush_on_last_del;
	int attribute_flags;
	int user_friendly_names;
	int deferred_remove;
	int delay_watch_checks;
	int delay_wait_checks;
	int san_path_err_threshold;
	int san_path_err_forget_rate;
	int san_path_err_recovery_time;
	int marginal_path_err_sample_time;
	int marginal_path_err_rate_threshold;
	int marginal_path_err_recheck_gap_time;
	int marginal_path_double_failed_time;
	int skip_kpartx;
	int max_sectors_kb;
	int ghost_delay;
	uid_t uid;
	gid_t gid;
	mode_t mode;
};

struct config {
	struct rcu_head rcu;
	int verbosity;
	int pgpolicy_flag;
	int pgpolicy;
	int minio;
	int minio_rq;
	unsigned int checkint;
	unsigned int max_checkint;
	bool use_watchdog;
	int pgfailback;
	int rr_weight;
	int no_path_retry;
	int user_friendly_names;
	int bindings_read_only;
	int max_fds;
	int force_reload;
	int queue_without_daemon;
	int checker_timeout;
	int flush_on_last_del;
	int attribute_flags;
	int fast_io_fail;
	unsigned int dev_loss;
	int eh_deadline;
	int log_checker_err;
	int allow_queueing;
	int allow_usb_devices;
	int find_multipaths;
	uid_t uid;
	gid_t gid;
	mode_t mode;
	int reassign_maps;
	int retain_hwhandler;
	int detect_prio;
	int detect_checker;
	int force_sync;
	int deferred_remove;
	int processed_main_config;
	int delay_watch_checks;
	int delay_wait_checks;
	int san_path_err_threshold;
	int san_path_err_forget_rate;
	int san_path_err_recovery_time;
	int marginal_path_err_sample_time;
	int marginal_path_err_rate_threshold;
	int marginal_path_err_recheck_gap_time;
	int marginal_path_double_failed_time;
	int uxsock_timeout;
	int strict_timing;
	int retrigger_tries;
	int retrigger_delay;
	int uev_wait_timeout;
	int skip_kpartx;
	int remove_retries;
	int max_sectors_kb;
	int ghost_delay;
	int find_multipaths_timeout;
	int marginal_pathgroups;
	int skip_delegate;
	unsigned int sequence_nr;
	int recheck_wwid;

	char * selector;
	struct _vector uid_attrs;
	char * uid_attribute;
	char * getuid;
	char * features;
	char * hwhandler;
	char * bindings_file;
	char * wwids_file;
	char * prkeys_file;
	char * prio_name;
	char * prio_args;
	char * checker_name;
	char * alias_prefix;
	char * partition_delim;
	int prkey_source;
	int all_tg_pt;
	struct be64 reservation_key;
	uint8_t sa_flags;

	vector keywords;
	vector mptable;
	vector hwtable;
	struct hwentry *overrides;

	vector blist_devnode;
	vector blist_wwid;
	vector blist_device;
	vector blist_property;
	vector blist_protocol;
	vector elist_devnode;
	vector elist_wwid;
	vector elist_device;
	vector elist_property;
	vector elist_protocol;
	char *enable_foreign;
};

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
 * after initiazing udev with udev_new().
 */
extern struct udev *udev;

/**
 * libmultipath_init() - library initialization
 *
 * This function initializes libmultipath data structures.
 * It is light-weight; some other initializations, like device-mapper
 * initialization, are done lazily when the respective functionality
 * is required.
 *
 * Clean up by libmultipath_exit() when the program terminates.
 * It is an error to call libmultipath_init() after libmultipath_exit().
 * Return: 0 on success, 1 on failure.
 */
int libmultipath_init(void);

/**
 * libmultipath_exit() - library un-initialization
 *
 * This function un-initializes libmultipath data structures.
 * It is recommended to call this function at program exit.
 * If the application also calls dm_lib_exit(), it should do so
 * after libmultipath_exit().
 *
 * Calls to libmultipath_init() after libmultipath_exit() will fail
 * (in other words, libmultipath can't be re-initialized).
 * Any other libmultipath calls after libmultipath_exit() may cause
 * undefined behavior.
 */
void libmultipath_exit(void);

int find_hwe (const struct _vector *hwtable,
	      const char * vendor, const char * product, const char *revision,
	      vector result);
struct mpentry * find_mpe (vector mptable, char * wwid);
const char *get_mpe_wwid (const struct _vector *mptable, const char *alias);

struct hwentry * alloc_hwe (void);
struct mpentry * alloc_mpe (void);
struct pcentry * alloc_pce (void);

void free_hwe (struct hwentry * hwe);
void free_hwtable (vector hwtable);
void free_mpe (struct mpentry * mpe);
void free_mptable (vector mptable);

int store_hwe (vector hwtable, struct hwentry *);

struct config *load_config (const char *file);
void free_config (struct config * conf);
int init_config(const char *file);
void uninit_config(void);

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
struct config *libmp_get_multipath_config(void);
struct config *get_multipath_config(void);
void libmp_put_multipath_config(void *);
void put_multipath_config(void *);

int parse_uid_attrs(char *uid_attrs, struct config *conf);
const char *get_uid_attribute_by_attrs(const struct config *conf,
				       const char *path_dev);

#endif
