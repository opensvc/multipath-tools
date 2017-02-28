#ifndef _CONFIG_H
#define _CONFIG_H

#include <sys/types.h>
#include <stdint.h>
#include <urcu.h>

#define ORIGIN_DEFAULT 0
#define ORIGIN_CONFIG  1

/*
 * In kernel, fast_io_fail == 0 means immediate failure on rport delete.
 * OTOH '0' means not-configured in various places in multipath-tools.
 */
#define MP_FAST_IO_FAIL_UNSET (0)
#define MP_FAST_IO_FAIL_OFF (-1)
#define MP_FAST_IO_FAIL_ZERO (-2)

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
};

enum force_reload_types {
	FORCE_RELOAD_NONE,
	FORCE_RELOAD_YES,
	FORCE_RELOAD_WEAK,
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
	int skip_kpartx;
	int max_sectors_kb;
	char * bl_product;
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
	unsigned char * reservation_key;
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
	int skip_kpartx;
	int max_sectors_kb;
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
	int checkint;
	int max_checkint;
	int pgfailback;
	int remove;
	int rr_weight;
	int no_path_retry;
	int user_friendly_names;
	int bindings_read_only;
	int max_fds;
	int force_reload;
	int queue_without_daemon;
	int ignore_wwids;
	int checker_timeout;
	int flush_on_last_del;
	int attribute_flags;
	int fast_io_fail;
	unsigned int dev_loss;
	int log_checker_err;
	int allow_queueing;
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
	int uxsock_timeout;
	int strict_timing;
	int retrigger_tries;
	int retrigger_delay;
	int ignore_new_devs;
	int delayed_reconfig;
	int uev_wait_timeout;
	int skip_kpartx;
	int disable_changed_wwids;
	int remove_retries;
	int max_sectors_kb;
	unsigned int version[3];

	char * multipath_dir;
	char * selector;
	char * uid_attrs;
	char * uid_attribute;
	char * getuid;
	char * features;
	char * hwhandler;
	char * bindings_file;
	char * wwids_file;
	char * prio_name;
	char * prio_args;
	char * checker_name;
	char * alias_prefix;
	char * partition_delim;
	char * config_dir;
	unsigned char * reservation_key;

	vector keywords;
	vector mptable;
	vector hwtable;
	struct hwentry *overrides;

	vector blist_devnode;
	vector blist_wwid;
	vector blist_device;
	vector blist_property;
	vector elist_devnode;
	vector elist_wwid;
	vector elist_device;
	vector elist_property;
};

extern struct udev * udev;

struct hwentry * find_hwe (vector hwtable, char * vendor, char * product, char *revision);
struct mpentry * find_mpe (vector mptable, char * wwid);
char * get_mpe_wwid (vector mptable, char * alias);

struct hwentry * alloc_hwe (void);
struct mpentry * alloc_mpe (void);

void free_hwe (struct hwentry * hwe);
void free_hwtable (vector hwtable);
void free_mpe (struct mpentry * mpe);
void free_mptable (vector mptable);

int store_hwe (vector hwtable, struct hwentry *);

struct config *load_config (char * file);
struct config * alloc_config (void);
void free_config (struct config * conf);
extern struct config *get_multipath_config(void);
extern void put_multipath_config(struct config *);

#endif
