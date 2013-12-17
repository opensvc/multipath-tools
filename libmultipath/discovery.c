/*
 * Copyright (c) 2004, 2005, 2006 Christophe Varoqui
 * Copyright (c) 2005 Stefan Bader, IBM
 * Copyright (c) 2005 Mike Anderson
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <libudev.h>

#include "checkers.h"
#include "vector.h"
#include "memory.h"
#include "util.h"
#include "structs.h"
#include "config.h"
#include "blacklist.h"
#include "callout.h"
#include "debug.h"
#include "propsel.h"
#include "sg_include.h"
#include "sysfs.h"
#include "discovery.h"
#include "prio.h"
#include "defaults.h"

int
store_pathinfo (vector pathvec, vector hwtable, struct udev_device *udevice,
		int flag, struct path **pp_ptr)
{
	int err = 1;
	struct path * pp;
	const char * devname;

	if (pp_ptr)
		*pp_ptr = NULL;

	devname = udev_device_get_sysname(udevice);
	if (!devname)
		return 1;

	pp = alloc_path();

	if (!pp)
		return 1;

	if(safe_sprintf(pp->dev, "%s", devname)) {
		condlog(0, "pp->dev too small");
		goto out;
	}
	pp->udev = udev_device_ref(udevice);
	err = pathinfo(pp, hwtable,
		       (conf->dry_run == 3)? flag : (flag | DI_BLACKLIST));
	if (err)
		goto out;

	err = store_path(pathvec, pp);
	if (err)
		goto out;

out:
	if (err)
		free_path(pp);
	else if (pp_ptr)
		*pp_ptr = pp;
	return err;
}

static int
path_discover (vector pathvec, struct config * conf,
	       struct udev_device *udevice, int flag)
{
	struct path * pp;
	const char * devname;

	devname = udev_device_get_sysname(udevice);
	if (!devname)
		return 0;

	if (filter_property(conf, udevice) > 0)
		return 0;

	if (filter_devnode(conf->blist_devnode, conf->elist_devnode,
			   (char *)devname) > 0)
		return 0;

	pp = find_path_by_dev(pathvec, (char *)devname);
	if (!pp) {
		if (store_pathinfo(pathvec, conf->hwtable,
				   udevice, flag, NULL) != 1)
			return 0;
		else
			return 1;
	}
	return pathinfo(pp, conf->hwtable, flag);
}

int
path_discovery (vector pathvec, struct config * conf, int flag)
{
	struct udev_enumerate *udev_iter;
	struct udev_list_entry *entry;
	struct udev_device *udevice;
	const char *devpath;
	int r = 0;

	udev_iter = udev_enumerate_new(conf->udev);
	if (!udev_iter)
		return 1;

	udev_enumerate_add_match_subsystem(udev_iter, "block");
	udev_enumerate_scan_devices(udev_iter);

	udev_list_entry_foreach(entry,
				udev_enumerate_get_list_entry(udev_iter)) {
		const char *devtype;
		devpath = udev_list_entry_get_name(entry);
		condlog(4, "Discover device %s", devpath);
		udevice = udev_device_new_from_syspath(conf->udev, devpath);
		if (!udevice) {
			condlog(4, "%s: no udev information", devpath);
			r++;
			continue;
		}
		devtype = udev_device_get_devtype(udevice);
		if(devtype && !strncmp(devtype, "disk", 4)) {
			r += path_discover(pathvec, conf,
						   udevice, flag);
		}
		udev_device_unref(udevice);
	}
	udev_enumerate_unref(udev_iter);
	condlog(4, "Discovery status %d", r);
	return r;
}

#define declare_sysfs_get_str(fname)					\
extern ssize_t								\
sysfs_get_##fname (struct udev_device * udev, char * buff, size_t len)	\
{									\
	const char * attr;						\
	const char * devname;						\
									\
	if (!udev)							\
		return -ENOSYS;						\
									\
	devname = udev_device_get_sysname(udev);			\
									\
	attr = udev_device_get_sysattr_value(udev, #fname);		\
	if (!attr) {							\
		condlog(3, "%s: attribute %s not found in sysfs",	\
			devname, #fname);				\
		return -ENXIO;						\
	}								\
	if (strchop(attr) > len) {					\
		condlog(3, "%s: overflow in attribute %s",		\
			devname, #fname);				\
		return -EINVAL;						\
	}								\
	return strlcpy(buff, attr, len);				\
}

declare_sysfs_get_str(devtype);
declare_sysfs_get_str(vendor);
declare_sysfs_get_str(model);
declare_sysfs_get_str(rev);
declare_sysfs_get_str(dev);

int
sysfs_get_timeout(struct path *pp, unsigned int *timeout)
{
	const char *attr = NULL;
	const char *subsys;
	struct udev_device *parent;
	int r;
	unsigned int t;

	if (!pp->udev || pp->bus != SYSFS_BUS_SCSI)
		return -ENOSYS;

	parent = pp->udev;
	while (parent) {
		subsys = udev_device_get_subsystem(parent);
		attr = udev_device_get_sysattr_value(parent, "timeout");
		if (subsys && attr)
			break;
		parent = udev_device_get_parent(parent);
	}
	if (!attr) {
		condlog(3, "%s: No timeout value in sysfs", pp->dev);
		return -ENXIO;
	}

	r = sscanf(attr, "%u\n", &t);

	if (r != 1) {
		condlog(3, "%s: Cannot parse timeout attribute '%s'",
			pp->dev, attr);
		return -EINVAL;
	}

	*timeout = t;

	return 0;
}

int
sysfs_get_tgt_nodename (struct path *pp, char * node)
{
	const char *tgtname, *value;
	struct udev_device *parent, *tgtdev;
	int host, channel, tgtid = -1;

	parent = udev_device_get_parent_with_subsystem_devtype(pp->udev, "scsi", "scsi_device");
	if (!parent)
		return 1;
	/* Check for SAS */
	value = udev_device_get_sysattr_value(parent, "sas_address");
	if (value) {
		tgtdev = udev_device_get_parent(parent);
		while (tgtdev) {
			tgtname = udev_device_get_sysname(tgtdev);
			if (sscanf(tgtname, "end_device-%d:%d",
				   &host, &tgtid) == 2)
				break;
			tgtdev = udev_device_get_parent(tgtdev);
			tgtid = -1;
		}
		if (tgtid >= 0) {
			pp->sg_id.proto_id = SCSI_PROTOCOL_SAS;
			pp->sg_id.transport_id = tgtid;
			strncpy(node, value, NODE_NAME_SIZE);
			return 0;
		}
	}

	parent = udev_device_get_parent_with_subsystem_devtype(pp->udev, "scsi", "scsi_target");
	if (!parent)
		return 1;
	/* Check for FibreChannel */
	tgtdev = udev_device_get_parent(parent);
	value = udev_device_get_sysname(tgtdev);
	if (sscanf(value, "rport-%d:%d-%d",
		   &host, &channel, &tgtid) == 3) {
		tgtdev = udev_device_new_from_subsystem_sysname(conf->udev,
				"fc_remote_ports", value);
		if (tgtdev) {
			condlog(3, "SCSI target %d:%d:%d -> "
				"FC rport %d:%d-%d",
				pp->sg_id.host_no, pp->sg_id.channel,
				pp->sg_id.scsi_id, host, channel,
				tgtid);
			value = udev_device_get_sysattr_value(tgtdev,
							      "node_name");
			if (value) {
				pp->sg_id.proto_id = SCSI_PROTOCOL_FCP;
				pp->sg_id.transport_id = tgtid;
				strncpy(node, value, NODE_NAME_SIZE);
				udev_device_unref(tgtdev);
				return 0;
			} else
				udev_device_unref(tgtdev);
		}
	}

	/* Check for iSCSI */
	parent = pp->udev;
	tgtname = NULL;
	while (parent) {
		tgtname = udev_device_get_sysname(parent);
		if (tgtname && sscanf(tgtname , "session%d", &tgtid) == 1)
			break;
		parent = udev_device_get_parent(parent);
		tgtname = NULL;
		tgtid = -1;
	}
	if (parent && tgtname) {
		tgtdev = udev_device_new_from_subsystem_sysname(conf->udev,
				"iscsi_session", tgtname);
		if (tgtdev) {
			const char *value;

			value = udev_device_get_sysattr_value(tgtdev, "tgtname");
			if (value) {
				pp->sg_id.proto_id = SCSI_PROTOCOL_ISCSI;
				pp->sg_id.transport_id = tgtid;
				strncpy(node, value, NODE_NAME_SIZE);
				udev_device_unref(tgtdev);
				return 0;
			}
			else
				udev_device_unref(tgtdev);
		}
	}
	/* Check for libata */
	parent = pp->udev;
	tgtname = NULL;
	while (parent) {
		tgtname = udev_device_get_sysname(parent);
		if (tgtname && sscanf(tgtname, "ata%d", &tgtid) == 1)
			break;
		parent = udev_device_get_parent(parent);
		tgtname = NULL;
	}
	if (tgtname) {
		pp->sg_id.proto_id = SCSI_PROTOCOL_ATA;
		pp->sg_id.transport_id = tgtid;
		snprintf(node, NODE_NAME_SIZE, "ata-%d.00", tgtid);
		return 0;
	}
	pp->sg_id.proto_id = SCSI_PROTOCOL_UNSPEC;
	return 1;
}

static void
sysfs_set_rport_tmo(struct multipath *mpp, struct path *pp)
{
	struct udev_device *rport_dev = NULL;
	char value[16];
	char rport_id[32];
	unsigned long long tmo = 0;
	int ret;

	sprintf(rport_id, "rport-%d:%d-%d",
		pp->sg_id.host_no, pp->sg_id.channel, pp->sg_id.transport_id);
	rport_dev = udev_device_new_from_subsystem_sysname(conf->udev,
				"fc_remote_ports", rport_id);
	if (!rport_dev) {
		condlog(1, "%s: No fc_remote_port device for '%s'", pp->dev,
			rport_id);
		return;
	}
	condlog(4, "target%d:%d:%d -> %s", pp->sg_id.host_no,
		pp->sg_id.channel, pp->sg_id.scsi_id, rport_id);

	/*
	 * This is tricky.
	 * dev_loss_tmo will be limited to 600 if fast_io_fail
	 * is _not_ set.
	 * fast_io_fail will be limited by the current dev_loss_tmo
	 * setting.
	 * So to get everything right we first need to increase
	 * dev_loss_tmo to the fast_io_fail setting (if present),
	 * then set fast_io_fail, and _then_ set dev_loss_tmo
	 * to the correct value.
	 */
	memset(value, 0, 16);
	if (mpp->fast_io_fail != MP_FAST_IO_FAIL_UNSET &&
	    mpp->fast_io_fail != MP_FAST_IO_FAIL_ZERO &&
	    mpp->fast_io_fail != MP_FAST_IO_FAIL_OFF) {
		/* Check if we need to temporarily increase dev_loss_tmo */
		ret = sysfs_attr_get_value(rport_dev, "dev_loss_tmo",
					   value, 16);
		if (ret <= 0) {
			condlog(0, "%s: failed to read dev_loss_tmo value, "
				"error %d", rport_id, -ret);
			goto out;
		}
		if (sscanf(value, "%llu\n", &tmo) != 1) {
			condlog(0, "%s: Cannot parse dev_loss_tmo "
				"attribute '%s'", rport_id, value);
			goto out;
		}
		if (mpp->fast_io_fail >= tmo) {
			snprintf(value, 16, "%u", mpp->fast_io_fail + 1);
		}
	} else if (mpp->dev_loss > 600) {
		condlog(3, "%s: limiting dev_loss_tmo to 600, since "
			"fast_io_fail is not set", rport_id);
		snprintf(value, 16, "%u", 600);
	} else {
		snprintf(value, 16, "%u", mpp->dev_loss);
	}
	if (strlen(value)) {
		ret = sysfs_attr_set_value(rport_dev, "dev_loss_tmo",
					   value, strlen(value));
		if (ret <= 0) {
			if (ret == -EBUSY)
				condlog(3, "%s: rport blocked", rport_id);
			else
				condlog(0, "%s: failed to set dev_loss_tmo to %s, error %d",
					rport_id, value, -ret);
			goto out;
		}
	}
	if (mpp->fast_io_fail != MP_FAST_IO_FAIL_UNSET) {
		if (mpp->fast_io_fail == MP_FAST_IO_FAIL_OFF)
			sprintf(value, "off");
		else if (mpp->fast_io_fail == MP_FAST_IO_FAIL_ZERO)
			sprintf(value, "0");
		else
			snprintf(value, 16, "%u", mpp->fast_io_fail);
		ret = sysfs_attr_set_value(rport_dev, "fast_io_fail_tmo",
					   value, strlen(value));
		if (ret <= 0) {
			if (ret == -EBUSY)
				condlog(3, "%s: rport blocked", rport_id);
			else
				condlog(0, "%s: failed to set fast_io_fail_tmo to %s, error %d",
					rport_id, value, -ret);
		}
	}
	if (tmo > 0) {
		snprintf(value, 16, "%u", mpp->dev_loss);
		ret = sysfs_attr_set_value(rport_dev, "dev_loss_tmo",
					   value, strlen(value));
		if (ret <= 0) {
			if (ret == -EBUSY)
				condlog(3, "%s: rport blocked", rport_id);
			else
				condlog(0, "%s: failed to set dev_loss_tmo to %s, error %d",
					rport_id, value, -ret);
		}
	}
out:
	udev_device_unref(rport_dev);
}

static void
sysfs_set_session_tmo(struct multipath *mpp, struct path *pp)
{
	struct udev_device *session_dev = NULL;
	char session_id[64];
	char value[11];

	sprintf(session_id, "session%d", pp->sg_id.transport_id);
	session_dev = udev_device_new_from_subsystem_sysname(conf->udev,
				"iscsi_session", session_id);
	if (!session_dev) {
		condlog(1, "%s: No iscsi session for '%s'", pp->dev,
			session_id);
		return;
	}
	condlog(4, "target%d:%d:%d -> %s", pp->sg_id.host_no,
		pp->sg_id.channel, pp->sg_id.scsi_id, session_id);

	if (mpp->dev_loss) {
		condlog(3, "%s: ignoring dev_loss_tmo on iSCSI", pp->dev);
	}
	if (mpp->fast_io_fail != MP_FAST_IO_FAIL_UNSET) {
		if (mpp->fast_io_fail == MP_FAST_IO_FAIL_OFF) {
			condlog(3, "%s: can't switch off fast_io_fail_tmo "
				"on iSCSI", pp->dev);
		} else if (mpp->fast_io_fail == MP_FAST_IO_FAIL_ZERO) {
			condlog(3, "%s: can't set fast_io_fail_tmo to '0'"
				"on iSCSI", pp->dev);
		} else {
			snprintf(value, 11, "%u", mpp->fast_io_fail);
			if (sysfs_attr_set_value(session_dev, "recovery_tmo",
						 value, 11) <= 0) {
				condlog(3, "%s: Failed to set recovery_tmo, "
					" error %d", pp->dev, errno);
			}
		}
	}
	udev_device_unref(session_dev);
	return;
}

static void
sysfs_set_nexus_loss_tmo(struct multipath *mpp, struct path *pp)
{
	struct udev_device *sas_dev = NULL;
	char end_dev_id[64];
	char value[11];

	sprintf(end_dev_id, "end_device-%d:%d",
		pp->sg_id.host_no, pp->sg_id.transport_id);
	sas_dev = udev_device_new_from_subsystem_sysname(conf->udev,
				"sas_end_device", end_dev_id);
	if (!sas_dev) {
		condlog(1, "%s: No SAS end device for '%s'", pp->dev,
			end_dev_id);
		return;
	}
	condlog(4, "target%d:%d:%d -> %s", pp->sg_id.host_no,
		pp->sg_id.channel, pp->sg_id.scsi_id, end_dev_id);

	if (mpp->dev_loss) {
		snprintf(value, 11, "%u", mpp->dev_loss);
		if (sysfs_attr_set_value(sas_dev, "I_T_nexus_loss_timeout",
					 value, 11) <= 0)
			condlog(3, "%s: failed to update "
				"I_T Nexus loss timeout, error %d",
				pp->dev, errno);
	}
	udev_device_unref(sas_dev);
	return;
}

int
sysfs_set_scsi_tmo (struct multipath *mpp)
{
	struct path *pp;
	int i;
	int dev_loss_tmo = mpp->dev_loss;

	if (mpp->no_path_retry > 0) {
		int no_path_retry_tmo = mpp->no_path_retry * conf->checkint;

		if (no_path_retry_tmo > MAX_DEV_LOSS_TMO)
			no_path_retry_tmo = MAX_DEV_LOSS_TMO;
		if (no_path_retry_tmo > dev_loss_tmo)
			dev_loss_tmo = no_path_retry_tmo;
		condlog(3, "%s: update dev_loss_tmo to %d",
			mpp->alias, dev_loss_tmo);
	} else if (mpp->no_path_retry == NO_PATH_RETRY_QUEUE) {
		dev_loss_tmo = MAX_DEV_LOSS_TMO;
		condlog(3, "%s: update dev_loss_tmo to %d",
			mpp->alias, dev_loss_tmo);
	}
	mpp->dev_loss = dev_loss_tmo;
	if (mpp->dev_loss && mpp->fast_io_fail >= (int)mpp->dev_loss) {
		condlog(3, "%s: turning off fast_io_fail (%d is not smaller than dev_loss_tmo)",
			mpp->alias, mpp->fast_io_fail);
		mpp->fast_io_fail = MP_FAST_IO_FAIL_OFF;
	}
	if (!mpp->dev_loss && mpp->fast_io_fail == MP_FAST_IO_FAIL_UNSET)
		return 0;

	vector_foreach_slot(mpp->paths, pp, i) {
		if (pp->sg_id.proto_id == SCSI_PROTOCOL_FCP)
			sysfs_set_rport_tmo(mpp, pp);
		if (pp->sg_id.proto_id == SCSI_PROTOCOL_ISCSI)
			sysfs_set_session_tmo(mpp, pp);
		if (pp->sg_id.proto_id == SCSI_PROTOCOL_SAS)
			sysfs_set_nexus_loss_tmo(mpp, pp);
	}
	return 0;
}

int
do_inq(int sg_fd, int cmddt, int evpd, unsigned int pg_op,
       void *resp, int mx_resp_len)
{
	unsigned char inqCmdBlk[INQUIRY_CMDLEN] =
		{ INQUIRY_CMD, 0, 0, 0, 0, 0 };
	unsigned char sense_b[SENSE_BUFF_LEN];
	struct sg_io_hdr io_hdr;

	if (cmddt)
		inqCmdBlk[1] |= 2;
	if (evpd)
		inqCmdBlk[1] |= 1;
	inqCmdBlk[2] = (unsigned char) pg_op;
	inqCmdBlk[3] = (unsigned char)((mx_resp_len >> 8) & 0xff);
	inqCmdBlk[4] = (unsigned char) (mx_resp_len & 0xff);
	memset(&io_hdr, 0, sizeof (struct sg_io_hdr));
	memset(sense_b, 0, SENSE_BUFF_LEN);
	io_hdr.interface_id = 'S';
	io_hdr.cmd_len = sizeof (inqCmdBlk);
	io_hdr.mx_sb_len = sizeof (sense_b);
	io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	io_hdr.dxfer_len = mx_resp_len;
	io_hdr.dxferp = resp;
	io_hdr.cmdp = inqCmdBlk;
	io_hdr.sbp = sense_b;
	io_hdr.timeout = DEF_TIMEOUT;

	if (ioctl(sg_fd, SG_IO, &io_hdr) < 0)
		return -1;

	/* treat SG_ERR here to get rid of sg_err.[ch] */
	io_hdr.status &= 0x7e;
	if ((0 == io_hdr.status) && (0 == io_hdr.host_status) &&
	    (0 == io_hdr.driver_status))
		return 0;
	if ((SCSI_CHECK_CONDITION == io_hdr.status) ||
	    (SCSI_COMMAND_TERMINATED == io_hdr.status) ||
	    (SG_ERR_DRIVER_SENSE == (0xf & io_hdr.driver_status))) {
		if (io_hdr.sbp && (io_hdr.sb_len_wr > 2)) {
			int sense_key;
			unsigned char * sense_buffer = io_hdr.sbp;
			if (sense_buffer[0] & 0x2)
				sense_key = sense_buffer[1] & 0xf;
			else
				sense_key = sense_buffer[2] & 0xf;
			if(RECOVERED_ERROR == sense_key)
				return 0;
		}
	}
	return -1;
}

static int
get_serial (char * str, int maxlen, int fd)
{
	int len = 0;
	char buff[MX_ALLOC_LEN + 1] = {0};

	if (fd < 0)
		return 1;

	if (0 == do_inq(fd, 0, 1, 0x80, buff, MX_ALLOC_LEN)) {
		len = buff[3];
		if (len >= maxlen)
			return 1;
		if (len > 0) {
			memcpy(str, buff + 4, len);
			str[len] = '\0';
		}
		return 0;
	}
	return 1;
}

static int
get_geometry(struct path *pp)
{
	if (pp->fd < 0)
		return 1;

	if (ioctl(pp->fd, HDIO_GETGEO, &pp->geom)) {
		condlog(2, "%s: HDIO_GETGEO failed with %d", pp->dev, errno);
		memset(&pp->geom, 0, sizeof(pp->geom));
		return 1;
	}
	condlog(3, "%s: %u cyl, %u heads, %u sectors/track, start at %lu",
		pp->dev, pp->geom.cylinders, pp->geom.heads,
		pp->geom.sectors, pp->geom.start);
	return 0;
}

static int
scsi_sysfs_pathinfo (struct path * pp)
{
	struct udev_device *parent;
	const char *attr_path = NULL;

	parent = pp->udev;
	while (parent) {
		const char *subsys = udev_device_get_subsystem(parent);
		if (subsys && !strncmp(subsys, "scsi", 4)) {
			attr_path = udev_device_get_sysname(parent);
			if (!attr_path)
				break;
			if (sscanf(attr_path, "%i:%i:%i:%i",
				   &pp->sg_id.host_no,
				   &pp->sg_id.channel,
				   &pp->sg_id.scsi_id,
				   &pp->sg_id.lun) == 4)
				break;
		}
		parent = udev_device_get_parent(parent);
	}
	if (!attr_path || pp->sg_id.host_no == -1)
		return 1;

	if (sysfs_get_vendor(parent, pp->vendor_id, SCSI_VENDOR_SIZE) <= 0)
		return 1;

	condlog(3, "%s: vendor = %s", pp->dev, pp->vendor_id);

	if (sysfs_get_model(parent, pp->product_id, SCSI_PRODUCT_SIZE) <= 0)
		return 1;

	condlog(3, "%s: product = %s", pp->dev, pp->product_id);

	if (sysfs_get_rev(parent, pp->rev, SCSI_REV_SIZE) <= 0)
		return 1;

	condlog(3, "%s: rev = %s", pp->dev, pp->rev);

	/*
	 * set the hwe configlet pointer
	 */
	pp->hwe = find_hwe(conf->hwtable, pp->vendor_id, pp->product_id, pp->rev);

	/*
	 * host / bus / target / lun
	 */
	condlog(3, "%s: h:b:t:l = %i:%i:%i:%i",
			pp->dev,
			pp->sg_id.host_no,
			pp->sg_id.channel,
			pp->sg_id.scsi_id,
			pp->sg_id.lun);

	/*
	 * target node name
	 */
	if(!sysfs_get_tgt_nodename(pp, pp->tgt_node_name)) {
		condlog(3, "%s: tgt_node_name = %s",
			pp->dev, pp->tgt_node_name);
	}

	return 0;
}

static int
ccw_sysfs_pathinfo (struct path * pp)
{
	struct udev_device *parent;
	char attr_buff[NAME_SIZE];
	const char *attr_path;

	parent = pp->udev;
	while (parent) {
		const char *subsys = udev_device_get_subsystem(parent);
		if (subsys && !strncmp(subsys, "ccw", 3))
			break;
		parent = udev_device_get_parent(parent);
	}
	if (!parent)
		return 1;

	sprintf(pp->vendor_id, "IBM");

	condlog(3, "%s: vendor = %s", pp->dev, pp->vendor_id);

	if (sysfs_get_devtype(parent, attr_buff, FILE_NAME_SIZE) <= 0)
		return 1;

	if (!strncmp(attr_buff, "3370", 4)) {
		sprintf(pp->product_id,"S/390 DASD FBA");
	} else if (!strncmp(attr_buff, "9336", 4)) {
		sprintf(pp->product_id,"S/390 DASD FBA");
	} else {
		sprintf(pp->product_id,"S/390 DASD ECKD");
	}

	condlog(3, "%s: product = %s", pp->dev, pp->product_id);

	/*
	 * set the hwe configlet pointer
	 */
	pp->hwe = find_hwe(conf->hwtable, pp->vendor_id, pp->product_id, NULL);

	/*
	 * host / bus / target / lun
	 */
	attr_path = udev_device_get_sysname(parent);
	pp->sg_id.lun = 0;
	sscanf(attr_path, "%i.%i.%x",
			&pp->sg_id.host_no,
			&pp->sg_id.channel,
			&pp->sg_id.scsi_id);
	condlog(3, "%s: h:b:t:l = %i:%i:%i:%i",
			pp->dev,
			pp->sg_id.host_no,
			pp->sg_id.channel,
			pp->sg_id.scsi_id,
			pp->sg_id.lun);

	return 0;
}

static int
cciss_sysfs_pathinfo (struct path * pp)
{
	const char * attr_path = NULL;
	struct udev_device *parent;

	parent = pp->udev;
	while (parent) {
		const char *subsys = udev_device_get_subsystem(parent);
		if (subsys && !strncmp(subsys, "cciss", 5)) {
			attr_path = udev_device_get_sysname(parent);
			if (!attr_path)
				break;
			if (sscanf(attr_path, "c%id%i",
				   &pp->sg_id.host_no,
				   &pp->sg_id.scsi_id) == 2)
				break;
		}
		parent = udev_device_get_parent(parent);
	}
	if (!attr_path || pp->sg_id.host_no == -1)
		return 1;

	if (sysfs_get_vendor(parent, pp->vendor_id, SCSI_VENDOR_SIZE) <= 0)
		return 1;

	condlog(3, "%s: vendor = %s", pp->dev, pp->vendor_id);

	if (sysfs_get_model(parent, pp->product_id, SCSI_PRODUCT_SIZE) <= 0)
		return 1;

	condlog(3, "%s: product = %s", pp->dev, pp->product_id);

	if (sysfs_get_rev(parent, pp->rev, SCSI_REV_SIZE) <= 0)
		return 1;

	condlog(3, "%s: rev = %s", pp->dev, pp->rev);

	/*
	 * set the hwe configlet pointer
	 */
	pp->hwe = find_hwe(conf->hwtable, pp->vendor_id, pp->product_id, pp->rev);

	/*
	 * host / bus / target / lun
	 */
	pp->sg_id.lun = 0;
	pp->sg_id.channel = 0;
	condlog(3, "%s: h:b:t:l = %i:%i:%i:%i",
		pp->dev,
		pp->sg_id.host_no,
		pp->sg_id.channel,
		pp->sg_id.scsi_id,
		pp->sg_id.lun);
	return 0;
}

static int
common_sysfs_pathinfo (struct path * pp)
{
	if (!pp)
		return 1;

	if (!pp->udev) {
		condlog(4, "%s: udev not initialised", pp->dev);
		return 1;
	}
	if (sysfs_get_dev(pp->udev, pp->dev_t, BLK_DEV_SIZE) <= 0) {
		condlog(3, "%s: no 'dev' attribute in sysfs", pp->dev);
		return 1;
	}

	condlog(3, "%s: dev_t = %s", pp->dev, pp->dev_t);

	if (sysfs_get_size(pp, &pp->size))
		return 1;

	condlog(3, "%s: size = %llu", pp->dev, pp->size);

	return 0;
}

int
path_offline (struct path * pp)
{
	struct udev_device * parent;
	char buff[SCSI_STATE_SIZE];
	int err;

	if (pp->bus != SYSFS_BUS_SCSI)
		return PATH_UP;

	parent = pp->udev;
	while (parent) {
		const char *subsys = udev_device_get_subsystem(parent);
		if (subsys && !strncmp(subsys, "scsi", 4))
			break;
		parent = udev_device_get_parent(parent);
	}

	if (!parent) {
		condlog(1, "%s: failed to get sysfs information", pp->dev);
		return PATH_REMOVED;
	}

	memset(buff, 0x0, SCSI_STATE_SIZE);
	err = sysfs_attr_get_value(parent, "state", buff, SCSI_STATE_SIZE);
	if (err <= 0) {
		if (err == -ENXIO)
			return PATH_REMOVED;
		else
			return PATH_DOWN;
	}


	condlog(3, "%s: path state = %s", pp->dev, buff);

	if (!strncmp(buff, "offline", 7) ||
	    !strncmp(buff, "quiesce", 7) ||
	    !strncmp(buff, "transport-offline", 17)) {
		pp->offline = 1;
		return PATH_DOWN;
	}
	pp->offline = 0;
	if (!strncmp(buff, "blocked", 7) || !strncmp(buff, "quiesce", 7))
		return PATH_PENDING;
	else if (!strncmp(buff, "running", 7))
		return PATH_UP;

	return PATH_DOWN;
}

int
sysfs_pathinfo(struct path * pp)
{
	if (common_sysfs_pathinfo(pp))
		return 1;

	pp->bus = SYSFS_BUS_UNDEF;
	if (!strncmp(pp->dev,"cciss",5))
		pp->bus = SYSFS_BUS_CCISS;
	if (!strncmp(pp->dev,"dasd", 4))
		pp->bus = SYSFS_BUS_CCW;
	if (!strncmp(pp->dev,"sd", 2))
		pp->bus = SYSFS_BUS_SCSI;

	if (pp->bus == SYSFS_BUS_UNDEF)
		return 0;
	else if (pp->bus == SYSFS_BUS_SCSI) {
		if (scsi_sysfs_pathinfo(pp))
			return 1;
	} else if (pp->bus == SYSFS_BUS_CCW) {
		if (ccw_sysfs_pathinfo(pp))
			return 1;
	} else if (pp->bus == SYSFS_BUS_CCISS) {
		if (cciss_sysfs_pathinfo(pp))
			return 1;
	}
	return 0;
}

static int
scsi_ioctl_pathinfo (struct path * pp, int mask)
{
	if (mask & DI_SERIAL) {
		get_serial(pp->serial, SERIAL_SIZE, pp->fd);
		condlog(3, "%s: serial = %s", pp->dev, pp->serial);
	}

	return 0;
}

static int
cciss_ioctl_pathinfo (struct path * pp, int mask)
{
	if (mask & DI_SERIAL) {
		get_serial(pp->serial, SERIAL_SIZE, pp->fd);
		condlog(3, "%s: serial = %s", pp->dev, pp->serial);
	}
	return 0;
}

int
get_state (struct path * pp, int daemon)
{
	struct checker * c = &pp->checker;
	int state;

	condlog(3, "%s: get_state", pp->dev);

	if (!checker_selected(c)) {
		if (daemon) {
			if (pathinfo(pp, conf->hwtable, DI_SYSFS) != 0) {
				condlog(3, "%s: couldn't get sysfs pathinfo",
					pp->dev);
				return PATH_UNCHECKED;
			}
		}
		select_checker(pp);
		if (!checker_selected(c)) {
			condlog(3, "%s: No checker selected", pp->dev);
			return PATH_UNCHECKED;
		}
		checker_set_fd(c, pp->fd);
		if (checker_init(c, pp->mpp?&pp->mpp->mpcontext:NULL)) {
			memset(c, 0x0, sizeof(struct checker));
			condlog(3, "%s: checker init failed", pp->dev);
			return PATH_UNCHECKED;
		}
	}
	checker_clear_message(c);
	if (daemon)
		checker_set_async(c);
	if (!conf->checker_timeout &&
	    sysfs_get_timeout(pp, &(c->timeout)) <= 0)
		c->timeout = DEF_TIMEOUT;
	state = checker_check(c);
	condlog(3, "%s: state = %s", pp->dev, checker_state_name(state));
	if (state != PATH_UP && state != PATH_GHOST &&
	    strlen(checker_message(c)))
		condlog(3, "%s: checker msg is \"%s\"",
			pp->dev, checker_message(c));
	return state;
}

static int
get_prio (struct path * pp)
{
	if (!pp)
		return 0;

	struct prio * p = &pp->prio;

	if (!prio_selected(p)) {
		select_detect_prio(pp);
		select_prio(pp);
		if (!prio_selected(p)) {
			condlog(3, "%s: no prio selected", pp->dev);
			pp->priority = PRIO_UNDEF;
			return 1;
		}
	}
	pp->priority = prio_getprio(p, pp);
	if (pp->priority < 0) {
		condlog(3, "%s: %s prio error", pp->dev, prio_name(p));
		pp->priority = PRIO_UNDEF;
		return 1;
	}
	condlog(3, "%s: %s prio = %u",
		pp->dev, prio_name(p), pp->priority);
	return 0;
}

static int
get_uid (struct path * pp)
{
	char *c;
	const char *origin;

	if (!pp->uid_attribute && !pp->getuid)
		select_getuid(pp);

	if (!pp->udev) {
		condlog(1, "%s: no udev information", pp->dev);
		return 1;
	}

	memset(pp->wwid, 0, WWID_SIZE);
	if (pp->getuid) {
		char buff[CALLOUT_MAX_SIZE];

		/* Use 'getuid' callout, deprecated */
		condlog(1, "%s: using deprecated getuid callout", pp->dev);
		if (apply_format(pp->getuid, &buff[0], pp)) {
			condlog(0, "error formatting uid callout command");
			memset(pp->wwid, 0, WWID_SIZE);
		} else if (execute_program(buff, pp->wwid, WWID_SIZE)) {
			condlog(3, "error calling out %s", buff);
			memset(pp->wwid, 0, WWID_SIZE);
		}
		origin = "callout";
	} else {
		const char *value;

		value = udev_device_get_property_value(pp->udev,
						       pp->uid_attribute);
		if ((!value || strlen(value) == 0) && conf->dry_run == 2)
			value = getenv(pp->uid_attribute);
		if (value && strlen(value)) {
			size_t len = WWID_SIZE;

			if (strlen(value) + 1 > WWID_SIZE) {
				condlog(0, "%s: wwid overflow", pp->dev);
			} else {
				len = strlen(value);
			}
			strncpy(pp->wwid, value, len);
		} else {
			condlog(3, "%s: no %s attribute", pp->dev,
				pp->uid_attribute);
		}
		origin = "udev";
	}
	/* Strip any trailing blanks */
	c = strchr(pp->wwid, '\0');
	c--;
	while (c && c >= pp->wwid && *c == ' ') {
		*c = '\0';
		c--;
	}
	condlog(3, "%s: uid = %s (%s)", pp->dev,
		*pp->wwid == '\0' ? "<empty>" : pp->wwid, origin);
	return 0;
}

extern int
pathinfo (struct path *pp, vector hwtable, int mask)
{
	int path_state;

	if (!pp)
		return 1;

	condlog(3, "%s: mask = 0x%x", pp->dev, mask);

	/*
	 * fetch info available in sysfs
	 */
	if (mask & DI_SYSFS && sysfs_pathinfo(pp))
		return 1;

	if (mask & DI_BLACKLIST && mask & DI_SYSFS) {
		if (filter_device(conf->blist_device, conf->elist_device,
				  pp->vendor_id, pp->product_id) > 0) {
			return 2;
		}
	}

	path_state = path_offline(pp);
	if (path_state == PATH_REMOVED)
		goto blank;

	/*
	 * fetch info not available through sysfs
	 */
	if (pp->fd < 0)
		pp->fd = open(udev_device_get_devnode(pp->udev), O_RDONLY);

	if (pp->fd < 0) {
		condlog(4, "Couldn't open node for %s: %s",
			pp->dev, strerror(errno));
		goto blank;
	}

	if (mask & DI_SERIAL)
		get_geometry(pp);

	if (path_state == PATH_UP && pp->bus == SYSFS_BUS_SCSI &&
	    scsi_ioctl_pathinfo(pp, mask))
		goto blank;

	if (pp->bus == SYSFS_BUS_CCISS &&
	    cciss_ioctl_pathinfo(pp, mask))
		goto blank;

	if (mask & DI_CHECKER) {
		if (path_state == PATH_UP) {
			pp->chkrstate = pp->state = get_state(pp, 0);
			if (pp->state == PATH_UNCHECKED ||
			    pp->state == PATH_WILD)
				goto blank;
			if (pp->state == PATH_TIMEOUT)
				pp->state = PATH_DOWN;
		} else {
			condlog(3, "%s: path inaccessible", pp->dev);
			pp->chkrstate = pp->state = path_state;
			if (path_state == PATH_PENDING ||
			    path_state == PATH_DOWN)
				pp->priority = 0;
		}
	}

	if ((mask & DI_WWID) && !strlen(pp->wwid))
		get_uid(pp);
	if (mask & DI_BLACKLIST && mask & DI_WWID) {
		if (filter_wwid(conf->blist_wwid, conf->elist_wwid,
				pp->wwid) > 0) {
			return 2;
		}
	}

	 /*
	  * Retrieve path priority, even for PATH_DOWN paths if it has never
	  * been successfully obtained before.
	  */
	if ((mask & DI_PRIO) && path_state == PATH_UP) {
		if (pp->state != PATH_DOWN || pp->priority == PRIO_UNDEF) {
			if (!strlen(pp->wwid))
				get_uid(pp);
			get_prio(pp);
		}
	}

	return 0;

blank:
	/*
	 * Recoverable error, for example faulty or offline path
	 */
	memset(pp->wwid, 0, WWID_SIZE);
	pp->chkrstate = pp->state = PATH_DOWN;

	return 0;
}
