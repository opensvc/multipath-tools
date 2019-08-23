#ifndef _STRUCTS_H
#define _STRUCTS_H

#include <sys/types.h>
#include <inttypes.h>

#include "prio.h"
#include "byteorder.h"
#include "generic.h"

#define WWID_SIZE		128
#define SERIAL_SIZE		128
#define NODE_NAME_SIZE		224
#define PATH_STR_SIZE		16
#define PARAMS_SIZE		4096
#define FILE_NAME_SIZE		256
#define CALLOUT_MAX_SIZE	256
#define BLK_DEV_SIZE		33
#define PATH_SIZE		512
#define NAME_SIZE		512
#define HOST_NAME_LEN		16
#define SLOT_NAME_SIZE		40
#define PRKEY_SIZE		19

#define SCSI_VENDOR_SIZE	9
#define SCSI_PRODUCT_SIZE	17
#define SCSI_STATE_SIZE		19
#define NVME_MODEL_SIZE         41
#define NVME_REV_SIZE           9

/* This must be the maximum of SCSI and NVME sizes */
#define PATH_PRODUCT_SIZE NVME_MODEL_SIZE
#define PATH_REV_SIZE NVME_REV_SIZE

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
	SYSFS_BUS_CCW,
	SYSFS_BUS_CCISS,
	SYSFS_BUS_NVME,
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

enum yes_no_states {
	YN_NO,
	YN_YES,
};

enum queue_without_daemon_states {
	QUE_NO_DAEMON_OFF = YN_NO,
	QUE_NO_DAEMON_ON = YN_YES,
	QUE_NO_DAEMON_FORCE,
};

enum attribute_bits {
	ATTR_UID,
	ATTR_GID,
	ATTR_MODE,
};

enum yes_no_undef_states {
	YNU_UNDEF,
	YNU_NO,
	YNU_YES,
};

#define _FIND_MULTIPATHS_F (1 << 1)
#define _FIND_MULTIPATHS_I (1 << 2)
#define _FIND_MULTIPATHS_N (1 << 3)
/*
 * _FIND_MULTIPATHS_F must have the same value as YNU_YES.
 * Generate a compile time error if that isn't the case.
 */
char ___error1___[-(_FIND_MULTIPATHS_F != YNU_YES)];

#define find_multipaths_on(conf) \
	(!!((conf)->find_multipaths & _FIND_MULTIPATHS_F))
#define ignore_wwids_on(conf) \
	(!!((conf)->find_multipaths & _FIND_MULTIPATHS_I))
#define ignore_new_devs_on(conf) \
	(!!((conf)->find_multipaths & _FIND_MULTIPATHS_N))

enum find_multipaths_states {
	FIND_MULTIPATHS_UNDEF = YNU_UNDEF,
	FIND_MULTIPATHS_OFF = YNU_NO,
	FIND_MULTIPATHS_ON = _FIND_MULTIPATHS_F,
	FIND_MULTIPATHS_GREEDY = _FIND_MULTIPATHS_I,
	FIND_MULTIPATHS_SMART = _FIND_MULTIPATHS_F|_FIND_MULTIPATHS_I,
	FIND_MULTIPATHS_STRICT = _FIND_MULTIPATHS_F|_FIND_MULTIPATHS_N,
	__FIND_MULTIPATHS_LAST,
};

enum flush_states {
	FLUSH_UNDEF = YNU_UNDEF,
	FLUSH_DISABLED = YNU_NO,
	FLUSH_ENABLED = YNU_YES,
};

enum log_checker_err_states {
	LOG_CHKR_ERR_ALWAYS,
	LOG_CHKR_ERR_ONCE,
};

enum user_friendly_names_states {
	USER_FRIENDLY_NAMES_UNDEF = YNU_UNDEF,
	USER_FRIENDLY_NAMES_OFF = YNU_NO,
	USER_FRIENDLY_NAMES_ON = YNU_YES,
};

enum retain_hwhandler_states {
	RETAIN_HWHANDLER_UNDEF = YNU_UNDEF,
	RETAIN_HWHANDLER_OFF = YNU_NO,
	RETAIN_HWHANDLER_ON = YNU_YES,
};

enum detect_prio_states {
	DETECT_PRIO_UNDEF = YNU_UNDEF,
	DETECT_PRIO_OFF = YNU_NO,
	DETECT_PRIO_ON = YNU_YES,
};

enum detect_checker_states {
	DETECT_CHECKER_UNDEF = YNU_UNDEF,
	DETECT_CHECKER_OFF = YNU_NO,
	DETECT_CHECKER_ON = YNU_YES,
};

enum deferred_remove_states {
	DEFERRED_REMOVE_UNDEF = YNU_UNDEF,
	DEFERRED_REMOVE_OFF = YNU_NO,
	DEFERRED_REMOVE_ON = YNU_YES,
	DEFERRED_REMOVE_IN_PROGRESS,
};

enum skip_kpartx_states {
	SKIP_KPARTX_UNDEF = YNU_UNDEF,
	SKIP_KPARTX_OFF = YNU_NO,
	SKIP_KPARTX_ON = YNU_YES,
};

enum max_sectors_kb_states {
	MAX_SECTORS_KB_UNDEF = 0,
	MAX_SECTORS_KB_MIN = 4,  /* can't be smaller than page size */
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

enum no_undef_states {
	NU_NO = -1,
	NU_UNDEF = 0,
};

enum ghost_delay_states {
	GHOST_DELAY_OFF = NU_NO,
	GHOST_DELAY_UNDEF = NU_UNDEF,
};

enum initialized_states {
	INIT_NEW,
	INIT_FAILED,
	INIT_MISSING_UDEV,
	INIT_REQUESTED_UDEV,
	INIT_OK,
};

enum prkey_sources {
	PRKEY_SOURCE_NONE,
	PRKEY_SOURCE_CONF,
	PRKEY_SOURCE_FILE,
};

enum all_tg_pt_states {
	ALL_TG_PT_UNDEF = YNU_UNDEF,
	ALL_TG_PT_OFF = YNU_NO,
	ALL_TG_PT_ON = YNU_YES,
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
	char product_id[PATH_PRODUCT_SIZE];
	char rev[PATH_REV_SIZE];
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
	int detect_checker;
	int tpgs;
	char * uid_attribute;
	char * getuid;
	struct prio prio;
	char * prio_args;
	struct checker checker;
	struct multipath * mpp;
	int fd;
	int initialized;
	int retriggers;
	unsigned int path_failures;
	time_t dis_reinstate_time;
	int disable_reinstate;
	int san_path_err_forget_rate;
	time_t io_err_dis_reinstate_time;
	int io_err_disable_reinstate;
	int io_err_pathfail_cnt;
	int io_err_pathfail_starttime;
	int find_multipaths_timeout;
	int marginal;
	/* configlet pointers */
	vector hwe;
	struct gen_path generic_path;
};

typedef int (pgpolicyfn) (struct multipath *, vector);

struct multipath {
	char wwid[WWID_SIZE];
	char alias_old[WWID_SIZE];
	int pgpolicy;
	pgpolicyfn *pgpolicyfn;
	int nextpg;
	int bestpg;
	int queuedio;
	int action;
	int wait_for_udev;
	int uev_wait_tick;
	int pgfailback;
	int failback_tick;
	int rr_weight;
	int nr_active;     /* current available(= not known as failed) paths */
	int no_path_retry; /* number of retries after all paths are down */
	int retry_tick;    /* remaining times for retries */
	int disable_queueing;
	int minio;
	int flush_on_last_del;
	int attribute_flags;
	int fast_io_fail;
	int retain_hwhandler;
	int deferred_remove;
	int san_path_err_threshold;
	int san_path_err_forget_rate;
	int san_path_err_recovery_time;
	int marginal_path_err_sample_time;
	int marginal_path_err_rate_threshold;
	int marginal_path_err_recheck_gap_time;
	int marginal_path_double_failed_time;
	int skip_kpartx;
	int max_sectors_kb;
	int force_readonly;
	int force_udev_reload;
	int needs_paths_uevent;
	int ghost_delay;
	int ghost_delay_tick;
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
	vector hwe;

	/* threads */
	pthread_t waiter;

	/* stats */
	unsigned int stat_switchgroup;
	unsigned int stat_path_failures;
	unsigned int stat_map_loads;
	unsigned int stat_total_queueing_time;
	unsigned int stat_queueing_timeouts;
	unsigned int stat_map_failures;

	/* checkers shared data */
	void * mpcontext;

	/* persistent management data*/
	int prkey_source;
	struct be64 reservation_key;
	uint8_t sa_flags;
	unsigned char prflag;
	int all_tg_pt;
	struct gen_multipath generic_mp;
};

static inline int marginal_path_check_enabled(const struct multipath *mpp)
{
	return mpp->marginal_path_double_failed_time > 0 &&
		mpp->marginal_path_err_sample_time > 0 &&
		mpp->marginal_path_err_recheck_gap_time > 0 &&
		mpp->marginal_path_err_rate_threshold >= 0;
}

static inline int san_path_check_enabled(const struct multipath *mpp)
{
	return mpp->san_path_err_threshold > 0 &&
		mpp->san_path_err_forget_rate > 0 &&
		mpp->san_path_err_recovery_time > 0;
}

struct pathgroup {
	long id;
	int status;
	int priority;
	int enabled_paths;
	int marginal;
	vector paths;
	struct multipath *mpp;
	struct gen_pathgroup generic_pg;
};

struct adapter_group {
	char adapter_name[SLOT_NAME_SIZE];
	struct pathgroup *pgp;
	int num_hosts;
	vector host_groups;
	int next_host_index;
};

struct host_group {
	int host_no;
	int num_paths;
	vector paths;
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

struct adapter_group * alloc_adaptergroup(void);
struct host_group * alloc_hostgroup(void);
void free_adaptergroup(vector adapters);
void free_hostgroup(vector hostgroups);

int store_adaptergroup(vector adapters, struct adapter_group *agp);
int store_hostgroup(vector hostgroupvec, struct host_group *hgp);

int store_path (vector pathvec, struct path * pp);
int add_pathgroup(struct multipath*, struct pathgroup *);

struct multipath * find_mp_by_alias (const struct _vector *mp, const char *alias);
struct multipath * find_mp_by_wwid (const struct _vector *mp, const char *wwid);
struct multipath * find_mp_by_str (const struct _vector *mp, const char *wwid);
struct multipath * find_mp_by_minor (const struct _vector *mp, int minor);

struct path * find_path_by_devt (const struct _vector *pathvec, const char *devt);
struct path * find_path_by_dev (const struct _vector *pathvec, const char *dev);
struct path * first_path (const struct multipath *mpp);

int pathcountgr (const struct pathgroup *, int);
int pathcount (const struct multipath *, int);
int pathcmp (const struct pathgroup *, const struct pathgroup *);
int add_feature (char **, const char *);
int remove_feature (char **, const char *);

extern char sysfs_path[PATH_SIZE];

#endif /* _STRUCTS_H */
