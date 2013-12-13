#ifndef _CONFIG_H
#define _CONFIG_H

#include <sys/types.h>
#include <stdint.h>

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
	DEV_DEVMAP
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
	uid_t uid;
	gid_t gid;
	mode_t mode;
};

struct config {
	int verbosity;
	int dry_run;
	int list;
	int pgpolicy_flag;
	int pgpolicy;
	enum devtypes dev_type;
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
	int checker_timeout;
	int daemon;
#ifdef USE_SYSTEMD
	int watchdog;
#endif
	int flush_on_last_del;
	int attribute_flags;
	int fast_io_fail;
	unsigned int dev_loss;
	int log_checker_err;
	int allow_queueing;
	uid_t uid;
	gid_t gid;
	mode_t mode;
	uint32_t cookie;
	int reassign_maps;
	int retain_hwhandler;
	int detect_prio;
	unsigned int version[3];

	char * dev;
	struct udev * udev;
	char * multipath_dir;
	char * selector;
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
	unsigned char * reservation_key;

	vector keywords;
	vector mptable;
	vector hwtable;

	vector blist_devnode;
	vector blist_wwid;
	vector blist_device;
	vector blist_property;
	vector elist_devnode;
	vector elist_wwid;
	vector elist_device;
	vector elist_property;
};

struct config * conf;

struct hwentry * find_hwe (vector hwtable, char * vendor, char * product, char *revision);
struct mpentry * find_mpe (char * wwid);
char * get_mpe_wwid (char * alias);

struct hwentry * alloc_hwe (void);
struct mpentry * alloc_mpe (void);

void free_hwe (struct hwentry * hwe);
void free_hwtable (vector hwtable);
void free_mpe (struct mpentry * mpe);
void free_mptable (vector mptable);

int store_hwe (vector hwtable, struct hwentry *);

int load_config (char * file, struct udev * udev);
struct config * alloc_config (void);
void free_config (struct config * conf);

#endif
