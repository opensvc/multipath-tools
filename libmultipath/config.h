#ifndef _CONFIG_H
#define _CONFIG_H

#define ORIGIN_DEFAULT 0
#define ORIGIN_CONFIG  1

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
	char * getuid;
	char * features;
	char * hwhandler;
	char * selector;
	char * checker_name;
	char * prio_name;

	int pgpolicy;
	int pgfailback;
	int rr_weight;
	int no_path_retry;
	int minio;
	int pg_timeout;
	struct prio * prio;
	struct checker * checker;
	char * bl_product;
};

struct mpentry {
	char * wwid;
	char * alias;
	char * getuid;
	char * selector;

	int pgpolicy;
	int pgfailback;
	int rr_weight;
	int no_path_retry;
	int minio;
	int pg_timeout;
};

struct config {
	int verbosity;
	int dry_run;
	int list;
	int pgpolicy_flag;
	int with_sysfs;
	int pgpolicy;
	struct prio * prio;
	struct checker * checker;
	enum devtypes dev_type;
	int minio;
	int checkint;
	int max_checkint;
	int pgfailback;
	int remove;
	int rr_weight;
	int no_path_retry;
	int user_friendly_names;
	int pg_timeout;
	int max_fds;

	char * dev;
	char * sysfs_dir;
	char * udev_dir;
	char * multipath_dir;
	char * selector;
	char * getuid;
	char * features;
	char * hwhandler;
	char * bindings_file;

	vector keywords;
	vector mptable;
	vector hwtable;

	vector blist_devnode;
	vector blist_wwid;
	vector blist_device;
	vector elist_devnode;
	vector elist_wwid;
	vector elist_device;
};

struct config * conf;

struct hwentry * find_hwe (vector hwtable, char * vendor, char * product, char *revision);
struct mpentry * find_mpe (char * wwid);
char * get_mpe_wwid (char * alias);

struct mpentry * alloc_mpe (void);

void free_hwe (struct hwentry * hwe);
void free_hwtable (vector hwtable);
void free_mpe (struct mpentry * mpe);
void free_mptable (vector mptable);

int store_hwe (vector hwtable, struct hwentry *);

int load_config (char * file);
struct config * alloc_config (void);
void free_config (struct config * conf);

#endif
