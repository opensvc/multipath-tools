#ifndef DISCOVERY_H_INCLUDED
#define DISCOVERY_H_INCLUDED

#define SYSFS_PATH_SIZE 255
#define INQUIRY_CMDLEN  6
#define INQUIRY_CMD     0x12
#define SENSE_BUFF_LEN  32
#define RECOVERED_ERROR 0x01
#define MX_ALLOC_LEN    255
#define TUR_CMD_LEN     6

#ifndef BLKGETSIZE
#define BLKGETSIZE      _IO(0x12,96)
#endif

#ifndef DEF_TIMEOUT
#define DEF_TIMEOUT	30
#endif

/*
 * excerpt from sg_err.h
 */
#define SCSI_CHECK_CONDITION    0x2
#define SCSI_COMMAND_TERMINATED 0x22
#define SG_ERR_DRIVER_SENSE     0x08

#define PATHINFO_OK 0
#define PATHINFO_FAILED 1
#define PATHINFO_SKIPPED 2

struct config;

int path_discovery (vector pathvec, int flag);
int path_get_tpgs(struct path *pp); /* This function never returns TPGS_UNDEF */
int do_tur (char *);
int path_sysfs_state(struct path *);
int start_checker(struct path * pp, struct config * conf, int daemon,
		  int state);
int get_state(struct path * pp);
int get_vpd_sgio (int fd, int pg, int vend_id, char * str, int maxlen);
int pathinfo (struct path * pp, struct config * conf, int mask);
int alloc_path_with_pathinfo (struct config *conf, struct udev_device *udevice,
			      const char *wwid, int flag, struct path **pp_ptr);
int store_pathinfo (vector pathvec, struct config *conf,
		    struct udev_device *udevice, int flag,
		    struct path **pp_ptr);
int sysfs_set_scsi_tmo (struct config *conf, struct multipath *mpp);
int sysfs_get_timeout(const struct path *pp, unsigned int *timeout);
int sysfs_get_iscsi_ip_address(const struct path *pp, char *ip_address);
int sysfs_get_host_adapter_name(const struct path *pp,
				char *adapter_name);
ssize_t sysfs_get_vpd (struct udev_device *udev, unsigned char pg,
		       unsigned char *buff, size_t len);
ssize_t sysfs_get_inquiry(struct udev_device *udev,
			  unsigned char *buff, size_t len);
int sysfs_get_asymmetric_access_state(struct path *pp,
				      char *buff, int buflen);
bool has_uid_fallback(struct path *pp);
int get_uid(struct path * pp, int path_state, struct udev_device *udev,
	    int allow_fallback);
bool is_vpd_page_supported(int fd, int pg);
void cleanup_udev_enumerate_ptr(void *arg);
void cleanup_udev_device_ptr(void *arg);

/*
 * discovery bitmask
 */
enum discovery_mode {
	DI_SYSFS__,
	DI_SERIAL__,
	DI_CHECKER__,
	DI_PRIO__,
	DI_WWID__,
	DI_BLACKLIST__,
	DI_NOIO__,
	DI_NOFALLBACK__,
};

#define DI_SYSFS	(1 << DI_SYSFS__)
#define DI_SERIAL	(1 << DI_SERIAL__)
#define DI_CHECKER	(1 << DI_CHECKER__)
#define DI_PRIO		(1 << DI_PRIO__)
#define DI_WWID		(1 << DI_WWID__)
#define DI_BLACKLIST	(1 << DI_BLACKLIST__)
#define DI_NOIO		(1 << DI_NOIO__) /* Avoid IO on the device */
#define DI_NOFALLBACK	(1 << DI_NOFALLBACK__) /* do not allow wwid fallback */

#define DI_ALL		(DI_SYSFS  | DI_SERIAL | DI_CHECKER | DI_PRIO | \
			 DI_WWID)

#endif /* DISCOVERY_H_INCLUDED */
