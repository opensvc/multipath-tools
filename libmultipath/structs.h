#ifndef _STRUCTS_H
#define _STRUCTS_H

#define WWID_SIZE		128
#define SERIAL_SIZE		64
#define NODE_NAME_SIZE		19
#define PATH_STR_SIZE  		16
#define PARAMS_SIZE		1024
#define FILE_NAME_SIZE		256
#define CALLOUT_MAX_SIZE	128
#define BLK_DEV_SIZE		33
#define PATH_SIZE		512
#define NAME_SIZE		128


#define SCSI_VENDOR_SIZE	9
#define SCSI_PRODUCT_SIZE	17
#define SCSI_REV_SIZE		5

#define NO_PATH_RETRY_UNDEF	0
#define NO_PATH_RETRY_FAIL	-1
#define NO_PATH_RETRY_QUEUE	-2

#define PRIO_UNDEF		-1
#define PRIO_DEFAULT		1

enum free_path_switch {
	KEEP_PATHS,
	FREE_PATHS
};

enum rr_weight_mode {
	RR_WEIGHT_UNDEF,
	RR_WEIGHT_NONE,
	RR_WEIGHT_PRIO
};

enum failback_mode {
	FAILBACK_UNDEF,
	FAILBACK_MANUAL,
	FAILBACK_IMMEDIATE
};

enum sysfs_buses {
	SYSFS_BUS_UNDEF,
	SYSFS_BUS_SCSI,
	SYSFS_BUS_IDE,
	SYSFS_BUS_CCW,
};

enum pathstates {
	PSTATE_UNDEF,
	PSTATE_FAILED,
	PSTATE_ACTIVE
};

enum pgstates {
	PGSTATE_UNDEF,
	PGSTATE_ENABLED,
	PGSTATE_DISABLED,
	PGSTATE_ACTIVE
};

enum pgtimeouts {
	PGTIMEOUT_UNDEF,
	PGTIMEOUT_NONE
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

struct sysfs_device {
	struct sysfs_device *parent;		/* parent device */
	char devpath[PATH_SIZE];
	char subsystem[NAME_SIZE];		/* $class, $bus, drivers, module */
	char kernel[NAME_SIZE];			/* device instance name */
	char kernel_number[NAME_SIZE];
	char driver[NAME_SIZE];			/* device driver name */
};

struct path {
	char dev[FILE_NAME_SIZE];
	char dev_t[BLK_DEV_SIZE];
	struct sysfs_device *sysdev;
	struct scsi_idlun scsi_id;
	struct sg_id sg_id;
	char wwid[WWID_SIZE];
	char vendor_id[SCSI_VENDOR_SIZE];
	char product_id[SCSI_PRODUCT_SIZE];
	char rev[SCSI_REV_SIZE];
	char serial[SERIAL_SIZE];
	char tgt_node_name[NODE_NAME_SIZE];
	unsigned long long size;
	unsigned int checkint;
	unsigned int tick;
	int bus;
	int state;
	int dmstate;
	int failcount;
	int priority;
	int pgindex;
	char * getuid;
	char * getprio;
	int getprio_selected;
	struct checker checker;
	struct multipath * mpp;
	int fd;
	
	/* configlet pointers */
	struct hwentry * hwe;
};

typedef int (pgpolicyfn) (struct multipath *);

struct multipath {
	char wwid[WWID_SIZE];
	char alias_old[WWID_SIZE];
	int pgpolicy;
	pgpolicyfn *pgpolicyfn;
	int nextpg;
	int bestpg;
	int queuedio;
	int action;
	int pgfailback;
	int failback_tick;
	int rr_weight;
	int nr_active;     /* current available(= not known as failed) paths */
	int no_path_retry; /* number of retries after all paths are down */
	int retry_tick;    /* remaining times for retries */
	int minio;
	int pg_timeout;
	unsigned long long size;
	vector paths;
	vector pg;
	char params[PARAMS_SIZE];
	char status[PARAMS_SIZE];
	struct dm_info * dmi;

	/* configlet pointers */
	char * alias;
	char * selector;
	char * features;
	char * hwhandler;
	struct mpentry * mpe;
	struct hwentry * hwe;

	/* threads */
	void * waiter;

	/* stats */
	unsigned int stat_switchgroup;
	unsigned int stat_path_failures;
	unsigned int stat_map_loads;
	unsigned int stat_total_queueing_time;
	unsigned int stat_queueing_timeouts;

	/* checkers shared data */
	void * mpcontext;
};

struct pathgroup {
	long id;
	int status;
	int priority;
	vector paths;
	char * selector;
};

struct path * alloc_path (void);
struct pathgroup * alloc_pathgroup (void);
struct multipath * alloc_multipath (void);
void free_path (struct path *);
void free_pathvec (vector vec, int free_paths);
void free_pathgroup (struct pathgroup * pgp, int free_paths);
void free_pgvec (vector pgvec, int free_paths);
void free_multipath (struct multipath *, int free_paths);
void free_multipath_attributes (struct multipath *);
void drop_multipath (vector mpvec, char * wwid, int free_paths);
void free_multipathvec (vector mpvec, int free_paths);

int store_path (vector pathvec, struct path * pp);
int store_pathgroup (vector pgvec, struct pathgroup * pgp);

struct multipath * find_mp_by_alias (vector mp, char * alias);
struct multipath * find_mp_by_wwid (vector mp, char * wwid);
struct multipath * find_mp_by_str (vector mp, char * wwid);
struct multipath * find_mp_by_minor (vector mp, int minor);
	
struct path * find_path_by_devt (vector pathvec, char * devt);
struct path * find_path_by_dev (vector pathvec, char * dev);
struct path * first_path (struct multipath * mpp);

int pathcountgr (struct pathgroup *, int);
int pathcount (struct multipath *, int);

extern char sysfs_path[PATH_SIZE];

#endif /* _STRUCTS_H */
