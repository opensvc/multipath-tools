#ifndef _CONFIG_H
#define _CONFIG_H

#ifndef _VECTOR_H
#include "vector.h"
#endif

enum devtypes {
	DEV_NONE,
	DEV_DEVT,
	DEV_DEVNODE,
	DEV_DEVMAP
};

struct hwentry {
	char * vendor;
	char * product;
	char * getuid;
	char * getprio;
	char * features;
	char * hwhandler;
	char * selector;

	int pgpolicy;
	int pgfailback;
	int rr_weight;
	int no_path_retry;
	int minio;
	int checker_index;
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
};

struct config {
	int verbosity;
	int dry_run;
	int list;
	int pgpolicy_flag;
	int with_sysfs;
	int default_pgpolicy;
	int default_checker_index;
	int dev_type;
	int minio;
	int checkint;
	int max_checkint;
	int pgfailback;
	int remove;
	int rr_weight;
	int no_path_retry;
	int user_friendly_names;

	char * dev;
	char * udev_dir;
	char * selector;
	char * default_getuid;
	char * default_getprio;
	char * features;
	char * default_hwhandler;
	char * bindings_file;

	vector mptable;
	vector hwtable;

	vector blist_devnode;
	vector blist_wwid;
	vector blist_device;
};

struct config * conf;

struct hwentry * find_hwe (vector hwtable, char * vendor, char * product);
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
