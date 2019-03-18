#ifndef DISCOVERY_H
#define DISCOVERY_H

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
int path_offline (struct path *);
int get_state (struct path * pp, struct config * conf, int daemon, int state);
int get_vpd_sgio (int fd, int pg, char * str, int maxlen);
int pathinfo (struct path * pp, struct config * conf, int mask);
int alloc_path_with_pathinfo (struct config *conf, struct udev_device *udevice,
			      const char *wwid, int flag, struct path **pp_ptr);
int store_pathinfo (vector pathvec, struct config *conf,
		    struct udev_device *udevice, int flag,
		    struct path **pp_ptr);
int sysfs_set_scsi_tmo (struct multipath *mpp, int checkint);
int sysfs_get_timeout(const struct path *pp, unsigned int *timeout);
int sysfs_get_host_pci_name(const struct path *pp, char *pci_name);
int sysfs_get_iscsi_ip_address(const struct path *pp, char *ip_address);
int sysfs_get_host_adapter_name(const struct path *pp,
				char *adapter_name);
ssize_t sysfs_get_vpd (struct udev_device *udev, unsigned char pg,
		       unsigned char *buff, size_t len);
ssize_t sysfs_get_inquiry(struct udev_device *udev,
			  unsigned char *buff, size_t len);
int sysfs_get_asymmetric_access_state(struct path *pp,
				      char *buff, int buflen);
int get_uid(struct path * pp, int path_state, struct udev_device *udev,
	    int allow_fallback);

/*
 * discovery bitmask
 */
enum discovery_mode {
	__DI_SYSFS,
	__DI_SERIAL,
	__DI_CHECKER,
	__DI_PRIO,
	__DI_WWID,
	__DI_BLACKLIST,
	__DI_NOIO,
};

#define DI_SYSFS	(1 << __DI_SYSFS)
#define DI_SERIAL	(1 << __DI_SERIAL)
#define DI_CHECKER	(1 << __DI_CHECKER)
#define DI_PRIO		(1 << __DI_PRIO)
#define DI_WWID		(1 << __DI_WWID)
#define DI_BLACKLIST	(1 << __DI_BLACKLIST)
#define DI_NOIO		(1 << __DI_NOIO) /* Avoid IO on the device */

#define DI_ALL		(DI_SYSFS  | DI_SERIAL | DI_CHECKER | DI_PRIO | \
			 DI_WWID)

#endif /* DISCOVERY_H */
