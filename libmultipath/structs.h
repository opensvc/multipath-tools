#ifndef _STRUCTS_H
#define _STRUCTS_H

#define WWID_SIZE		64
#define SERIAL_SIZE		17
#define NODE_NAME_SIZE		19
#define PATH_STR_SIZE  		16
#define PARAMS_SIZE		1024
#define FILE_NAME_SIZE		256
#define CALLOUT_MAX_SIZE	128
#define BLK_DEV_SIZE		33

#define SCSI_VENDOR_SIZE	9
#define SCSI_PRODUCT_SIZE	17
#define SCSI_REV_SIZE		5

#define KEEP_PATHS		0
#define FREE_PATHS		1

#define FAILBACK_UNDEF		0
#define FAILBACK_MANUAL		-1
#define FAILBACK_IMMEDIATE	-2

#define SYSFS_BUS_NONE		0
#define SYSFS_BUS_SCSI		1
#define SYSFS_BUS_IDE		2

enum pathstates {
	PSTATE_RESERVED,
	PSTATE_FAILED,
	PSTATE_ACTIVE
};

enum pgstates {
	PGSTATE_RESERVED,
	PGSTATE_ENABLED,
	PGSTATE_DISABLED,
	PGSTATE_ACTIVE
};

struct scsi_idlun {
	int dev_id;
	int host_unique_id;
	int host_no;
};

struct sg_id {
	int host_no;
	int channel;
	int scsi_id;
	int lun;
	short h_cmd_per_lun;
	short d_queue_depth;
	int unused1;
	int unused2;
};

struct scsi_dev {
	char dev[FILE_NAME_SIZE];
	struct scsi_idlun scsi_id;
	int host_no;
};

struct path {
	char dev[FILE_NAME_SIZE];
	char dev_t[BLK_DEV_SIZE];
	struct scsi_idlun scsi_id;
	struct sg_id sg_id;
	char wwid[WWID_SIZE];
	char vendor_id[SCSI_VENDOR_SIZE];
	char product_id[SCSI_PRODUCT_SIZE];
	char rev[SCSI_REV_SIZE];
	char serial[SERIAL_SIZE];
	char tgt_node_name[NODE_NAME_SIZE];
	unsigned long size;
	unsigned int checkint;
	unsigned int tick;
	int state;
	int bus;
	int dmstate;
	int failcount;
	int priority;
	int claimed;
	int pgindex;
	char * getuid;
	char * getprio;
	int (*checkfn) (int, char *, void **);
	void * checker_context;
	struct multipath * mpp;
	int fd;
	
	/* configlet pointers */
	struct hwentry * hwe;
};

struct multipath {
	char wwid[WWID_SIZE];
	int minor;
	int pgpolicy;
	int nextpg;
	int queuedio;
	int action;
	int pgfailback;
	int failback_tick;
	unsigned long size;
	vector paths;
	vector pg;
	char params[PARAMS_SIZE];
	char status[PARAMS_SIZE];

	/* configlet pointers */
	char * alias;
	char * selector;
	char * features;
	char * hwhandler;
	struct mpentry * mpe;
	struct hwentry * hwe;

	/* daemon store a data blob for DM event waiter threads */
	void * waiter;
};

struct pathgroup {
	int status;
	int priority;
	vector paths;
};

struct path * alloc_path (void);
struct pathgroup * alloc_pathgroup (void);
struct multipath * alloc_multipath (void);
void free_path (struct path *);
void free_pathvec (vector vec, int free_paths);
void free_pathgroup (struct pathgroup * pgp, int free_paths);
void free_pgvec (vector pgvec, int free_paths);
void free_multipath (struct multipath *, int free_paths);
void free_multipathvec (vector mpvec, int free_paths);

int store_path (vector pathvec, struct path * pp);
int store_pathgroup (vector pgvec, struct pathgroup * pgp);

struct multipath * find_mp (vector mp, char * alias);
struct multipath * find_mp_by_wwid (vector mp, char * wwid);
struct multipath * find_mp_by_minor (vector mp, int minor);
	
struct path * find_path_by_devt (vector pathvec, char * devt);
struct path * find_path_by_dev (vector pathvec, char * dev);

char sysfs_path[FILE_NAME_SIZE];

#endif
