#ifndef _STRUCTS_H
#define _STRUCTS_H

#include <sys/types.h>

#include "prio.h"

#define WWID_SIZE		128
#define SERIAL_SIZE		65
#define NODE_NAME_SIZE		224
#define PATH_STR_SIZE		16
#define PARAMS_SIZE		4096
#define FILE_NAME_SIZE		256
#define CALLOUT_MAX_SIZE	256
#define BLK_DEV_SIZE		33
#define PATH_SIZE		512
#define NAME_SIZE		512


#define SCSI_VENDOR_SIZE	9
#define SCSI_PRODUCT_SIZE	17
#define SCSI_REV_SIZE		5
#define SCSI_STATE_SIZE		19

#define NO_PATH_RETRY_UNDEF	0
#define NO_PATH_RETRY_FAIL	-1
#define NO_PATH_RETRY_QUEUE	-2


enum free_path_mode {
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
	FAILBACK_IMMEDIATE,
	FAILBACK_FOLLOWOVER
};

enum sysfs_buses {
	SYSFS_BUS_UNDEF,
	SYSFS_BUS_SCSI,
	SYSFS_BUS_IDE,
	SYSFS_BUS_CCW,
	SYSFS_BUS_CCISS,
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

enum queue_without_daemon_states {
	QUE_NO_DAEMON_OFF,
	QUE_NO_DAEMON_ON,
	QUE_NO_DAEMON_FORCE,
};

enum pgtimeouts {
	PGTIMEOUT_UNDEF,
	PGTIMEOUT_NONE
};

enum attribute_bits {
	ATTR_UID,
	ATTR_GID,
	ATTR_MODE,
};

enum flush_states {
	FLUSH_UNDEF,
	FLUSH_DISABLED,
	FLUSH_ENABLED,
	FLUSH_IN_PROGRESS,
};

enum log_checker_err_states {
	LOG_CHKR_ERR_ALWAYS,
	LOG_CHKR_ERR_ONCE,
};

enum user_friendly_names_states {
	USER_FRIENDLY_NAMES_UNDEF,
	USER_FRIENDLY_NAMES_OFF,
	USER_FRIENDLY_NAMES_ON,
};

enum retain_hwhandler_states {
	RETAIN_HWHANDLER_UNDEF,
	RETAIN_HWHANDLER_OFF,
	RETAIN_HWHANDLER_ON,
};

enum detect_prio_states {
	DETECT_PRIO_UNDEF,
	DETECT_PRIO_OFF,
	DETECT_PRIO_ON,
};

enum scsi_protocol {
	SCSI_PROTOCOL_FCP = 0,	/* Fibre Channel */
	SCSI_PROTOCOL_SPI = 1,	/* parallel SCSI */
	SCSI_PROTOCOL_SSA = 2,	/* Serial Storage Architecture - Obsolete */
	SCSI_PROTOCOL_SBP = 3,	/* firewire */
	SCSI_PROTOCOL_SRP = 4,	/* Infiniband RDMA */
	SCSI_PROTOCOL_ISCSI = 5,
	SCSI_PROTOCOL_SAS = 6,
	SCSI_PROTOCOL_ADT = 7,	/* Media Changers */
	SCSI_PROTOCOL_ATA = 8,
	SCSI_PROTOCOL_UNSPEC = 0xf, /* No specific protocol */
};

struct sg_id {
	int host_no;
	int channel;
	int scsi_id;
	int lun;
	short h_cmd_per_lun;
	short d_queue_depth;
	enum scsi_protocol proto_id;
	int transport_id;
};

# ifndef HDIO_GETGEO
#  define HDIO_GETGEO	0x0301	/* get device geometry */

struct hd_geometry {
      unsigned char heads;
      unsigned char sectors;
      unsigned short cylinders;
      unsigned long start;
};
#endif

struct path {
	char dev[FILE_NAME_SIZE];
	char dev_t[BLK_DEV_SIZE];
	struct udev_device *udev;
	struct sg_id sg_id;
	struct hd_geometry geom;
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
	int offline;
	int state;
	int dmstate;
	int chkrstate;
	int failcount;
	int priority;
	int pgindex;
	int detect_prio;
	char * uid_attribute;
	char * getuid;
	struct prio prio;
	char * prio_args;
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
	int flush_on_last_del;
	int attribute_flags;
	int fast_io_fail;
	int retain_hwhandler;
	unsigned int dev_loss;
	uid_t uid;
	gid_t gid;
	mode_t mode;
	unsigned long long size;
	vector paths;
	vector pg;
	struct dm_info * dmi;

	/* configlet pointers */
	char * alias;
	char * alias_prefix;
	char * selector;
	char * features;
	char * hwhandler;
	struct mpentry * mpe;
	struct hwentry * hwe;

	/* threads */
	pthread_t waiter;

	/* stats */
	unsigned int stat_switchgroup;
	unsigned int stat_path_failures;
	unsigned int stat_map_loads;
	unsigned int stat_total_queueing_time;
	unsigned int stat_queueing_timeouts;

	/* checkers shared data */
	void * mpcontext;
	
	/* persistent management data*/
	unsigned char * reservation_key;
	unsigned char prflag;
};

struct pathgroup {
	long id;
	int status;
	int priority;
	int enabled_paths;
	vector paths;
	char * selector;
};

struct path * alloc_path (void);
struct pathgroup * alloc_pathgroup (void);
struct multipath * alloc_multipath (void);
void free_path (struct path *);
void free_pathvec (vector vec, enum free_path_mode free_paths);
void free_pathgroup (struct pathgroup * pgp, enum free_path_mode free_paths);
void free_pgvec (vector pgvec, enum free_path_mode free_paths);
void free_multipath (struct multipath *, enum free_path_mode free_paths);
void free_multipath_attributes (struct multipath *);
void drop_multipath (vector mpvec, char * wwid, enum free_path_mode free_paths);
void free_multipathvec (vector mpvec, enum free_path_mode free_paths);

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
int pathcmp (struct pathgroup *, struct pathgroup *);
void setup_feature(struct multipath *, char *);
int add_feature (char **, char *);
int remove_feature (char **, char *);

extern char sysfs_path[PATH_SIZE];

#endif /* _STRUCTS_H */
