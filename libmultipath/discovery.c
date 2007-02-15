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
#include <errno.h>
#include <sysfs/dlist.h>
#include <sysfs/libsysfs.h>

#include <checkers.h>

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
#include "discovery.h"

struct path *
store_pathinfo (vector pathvec, vector hwtable, char * devname, int flag)
{
	struct path * pp;

	pp = alloc_path();

	if (!pp)
		return NULL;

	if(safe_sprintf(pp->dev, "%s", devname)) {
		condlog(0, "pp->dev too small");
		goto out;
	}
	if (pathinfo(pp, hwtable, flag))
		goto out;

	if (store_path(pathvec, pp))
		goto out;

	return pp;
out:
	free_path(pp);
	return NULL;
}

static int
path_discover (vector pathvec, struct config * conf, char * devname, int flag)
{
	char path[FILE_NAME_SIZE];
	struct path * pp;

	if (!devname)
		return 0;

	if (filter_devnode(conf->blist_devnode, conf->elist_devnode,
			   devname) > 0)
		return 0;

	if(safe_sprintf(path, "%s/block/%s/device", sysfs_path,
			devname)) {
		condlog(0, "path too small");
		return 1;
	}
			
	if (!filepresent(path))
		return 0;

	pp = find_path_by_dev(pathvec, devname);

	if (!pp) {
		pp = store_pathinfo(pathvec, conf->hwtable,
				    devname, flag);
		return (pp ? 0 : 1);
	}
	return pathinfo(pp, conf->hwtable, flag);
}

int
path_discovery (vector pathvec, struct config * conf, int flag)
{
	struct dlist * ls;
	struct sysfs_class * class;
	struct sysfs_class_device * dev;
	int r = 1;

	if (!(class = sysfs_open_class("block")))
		return 1;

	if (!(ls = sysfs_get_class_devices(class)))
		goto out;

	r = 0;

	dlist_for_each_data(ls, dev, struct sysfs_class_device)
		r += path_discover(pathvec, conf, dev->name, flag);

out:
	sysfs_close_class(class);
	return r;
}

/*
 * the daemon can race udev upon path add,
 * not multipath(8), ran by udev
 */
#if DAEMON
#define WAIT_MAX_SECONDS 5
#define WAIT_LOOP_PER_SECOND 5

static int
wait_for_file (char * filename)
{
	int loop;
	struct stat stats;
	
	loop = WAIT_MAX_SECONDS * WAIT_LOOP_PER_SECOND;
	
	while (--loop) {
		if (stat(filename, &stats) == 0)
			return 0;

		if (errno != ENOENT)
			return 1;

		usleep(1000 * 1000 / WAIT_LOOP_PER_SECOND);
	}
	return 1;
}
#else
static int
wait_for_file (char * filename)
{
	return 0;
}
#endif

#define declare_sysfs_get_str(fname, fmt) \
extern int \
sysfs_get_##fname (char * sysfs_path, char * dev, char * buff, int len) \
{ \
	struct sysfs_attribute * attr; \
	char attr_path[SYSFS_PATH_SIZE]; \
\
	if (safe_sprintf(attr_path, fmt, sysfs_path, dev)) \
		return 1; \
\
	if (wait_for_file(attr_path)) \
		return 1; \
\
	if (!(attr = sysfs_open_attribute(attr_path))) \
		return 1; \
\
	if (0 > sysfs_read_attribute(attr)) \
		goto out; \
\
	if (attr->len < 2 || attr->len - 1 > len) \
		goto out; \
\
	strncpy(buff, attr->value, attr->len - 1); \
	strchop(buff); \
	sysfs_close_attribute(attr); \
	return 0; \
out: \
	sysfs_close_attribute(attr); \
	return 1; \
}

declare_sysfs_get_str(devtype, "%s/block/%s/device/devtype");
declare_sysfs_get_str(cutype, "%s/block/%s/device/cutype");
declare_sysfs_get_str(vendor, "%s/block/%s/device/vendor");
declare_sysfs_get_str(model, "%s/block/%s/device/model");
declare_sysfs_get_str(rev, "%s/block/%s/device/rev");
declare_sysfs_get_str(dev, "%s/block/%s/dev");

int
sysfs_get_size (char * sysfs_path, char * dev, unsigned long long * size)
{
	struct sysfs_attribute * attr;
	char attr_path[SYSFS_PATH_SIZE];
	int r;

	if (safe_sprintf(attr_path, "%s/block/%s/size", sysfs_path, dev))
		return 1;

	attr = sysfs_open_attribute(attr_path);

	if (!attr)
		return 1;

	if (0 > sysfs_read_attribute(attr))
		goto out;

	r = sscanf(attr->value, "%llu\n", size);
	sysfs_close_attribute(attr);

	if (r != 1)
		return 1;

	return 0;
out:
	sysfs_close_attribute(attr);
	return 1;
}
	
/*
 * udev might be slow creating node files : wait
 */
static int
opennode (char * dev, int mode)
{
	char devpath[FILE_NAME_SIZE];

	if (safe_sprintf(devpath, "%s/%s", conf->udev_dir, dev)) {
		condlog(0, "devpath too small");
		return -1;
	}

	if (wait_for_file(devpath)) {
		condlog(3, "failed to open %s", devpath);
		return -1;
	}

	return open(devpath, mode);
}

extern int
devt2devname (char *devname, char *devt)
{
	struct dlist * ls;
	char attr_path[FILE_NAME_SIZE];
	char block_path[FILE_NAME_SIZE];
	struct sysfs_attribute * attr = NULL;
	struct sysfs_class * class;
	struct sysfs_class_device * dev;

	if(safe_sprintf(block_path, "%s/block", sysfs_path)) {
		condlog(0, "block_path too small");
		return 1;
	}
	if (!(class = sysfs_open_class("block")))
		return 1;

	if (!(ls = sysfs_get_class_devices(class)))
		goto err;

	dlist_for_each_data(ls, dev, struct sysfs_class_device) {
		if(safe_sprintf(attr_path, "%s/%s/dev",
				block_path, dev->name)) {
			condlog(0, "attr_path too small");
			goto err;
		}
		if (!(attr = sysfs_open_attribute(attr_path)))
			goto err;

		if (sysfs_read_attribute(attr))
			goto err1;

		/* discard newline */
		if (attr->len > 1) attr->len--;

		if (strlen(devt) == attr->len &&
		    strncmp(attr->value, devt, attr->len) == 0) {
			if(safe_sprintf(attr_path, "%s/%s",
					block_path, dev->name)) {
				condlog(0, "attr_path too small");
				goto err1;
			}
			sysfs_get_name_from_path(attr_path, devname,
						 FILE_NAME_SIZE);
			sysfs_close_attribute(attr);
			sysfs_close_class(class);
			return 0;
		}
	}
err1:
	sysfs_close_attribute(attr);
err:
	sysfs_close_class(class);
	return 1;
}

static int
do_inq(int sg_fd, int cmddt, int evpd, unsigned int pg_op,
       void *resp, int mx_resp_len, int noisy)
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

	if (0 == do_inq(fd, 0, 1, 0x80, buff, MX_ALLOC_LEN, 0)) {
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
sysfs_get_bus (char * sysfs_path, struct path * pp)
{
	struct sysfs_device *sdev;
	char attr_path[FILE_NAME_SIZE];
	char attr_buff[FILE_NAME_SIZE];

	pp->bus = SYSFS_BUS_UNDEF;

	/*
	 * This is ugly : we should be able to do a simple
	 * get_link("%s/block/%s/device/bus", ...) but it just
	 * won't work
	 */
	if(safe_sprintf(attr_path, "%s/block/%s/device",
			sysfs_path, pp->dev)) {
		condlog(0, "attr_path too small");
		return 1;
	}

	if (0 > sysfs_get_link(attr_path, attr_buff, sizeof(attr_buff)))
		return 1;

#if DAEMON
	int loop = WAIT_MAX_SECONDS * WAIT_LOOP_PER_SECOND;

	while (loop--) {
		sdev = sysfs_open_device_path(attr_buff);

		if (strlen(sdev->bus))
			break;

		sysfs_close_device(sdev);
		usleep(1000 * 1000 / WAIT_LOOP_PER_SECOND);
	}
#else
	sdev = sysfs_open_device_path(attr_buff);
#endif

	if (!strncmp(sdev->bus, "scsi", 4))
		pp->bus = SYSFS_BUS_SCSI;
	else if (!strncmp(sdev->bus, "ide", 3))
		pp->bus = SYSFS_BUS_IDE;
	else if (!strncmp(sdev->bus, "ccw", 3))
		pp->bus = SYSFS_BUS_CCW;
	else
		return 1;

	sysfs_close_device(sdev);

	return 0;
}

static int
scsi_sysfs_pathinfo (struct path * pp)
{
	char attr_path[FILE_NAME_SIZE];
	char attr_buff[FILE_NAME_SIZE];
	struct sysfs_attribute * attr;

	if (sysfs_get_vendor(sysfs_path, pp->dev,
			     pp->vendor_id, SCSI_VENDOR_SIZE))
		return 1;

	condlog(3, "%s: vendor = %s", pp->dev, pp->vendor_id);

	if (sysfs_get_model(sysfs_path, pp->dev,
			    pp->product_id, SCSI_PRODUCT_SIZE))
		return 1;

	condlog(3, "%s: product = %s", pp->dev, pp->product_id);

	if (sysfs_get_rev(sysfs_path, pp->dev,
			  pp->rev, SCSI_REV_SIZE))
		return 1;

	condlog(3, "%s: rev = %s", pp->dev, pp->rev);

	/*
	 * set the hwe configlet pointer
	 */
	pp->hwe = find_hwe(conf->hwtable, pp->vendor_id, pp->product_id, pp->rev);

	/*
	 * host / bus / target / lun
	 */
	if(safe_sprintf(attr_path, "%s/block/%s/device",
			sysfs_path, pp->dev)) {
		condlog(0, "attr_path too small");
		return 1;
	}
	if (0 > sysfs_get_link(attr_path, attr_buff, sizeof(attr_buff)))
		return 1;
	
	basename(attr_buff, attr_path);

	sscanf(attr_path, "%i:%i:%i:%i",
			&pp->sg_id.host_no,
			&pp->sg_id.channel,
			&pp->sg_id.scsi_id,
			&pp->sg_id.lun);
	condlog(3, "%s: h:b:t:l = %i:%i:%i:%i",
			pp->dev,
			pp->sg_id.host_no,
			pp->sg_id.channel,
			pp->sg_id.scsi_id,
			pp->sg_id.lun);

	/*
	 * target node name
	 */
	if(safe_sprintf(attr_path,
			"%s/class/fc_transport/target%i:%i:%i/node_name",
			sysfs_path,
			pp->sg_id.host_no,
			pp->sg_id.channel,
			pp->sg_id.scsi_id)) {
		condlog(0, "attr_path too small");
		return 1;
	}
	if (!(attr = sysfs_open_attribute(attr_path)))
		return 0;

	if (sysfs_read_attribute(attr))
		goto err;

	if (attr->len > 0)
		strncpy(pp->tgt_node_name, attr->value, attr->len - 1);

	condlog(3, "%s: tgt_node_name = %s",
		pp->dev, pp->tgt_node_name);

	return 0;
err:
	sysfs_close_attribute(attr);
	return 1;
}

static int
ccw_sysfs_pathinfo (struct path * pp)
{
	char attr_path[FILE_NAME_SIZE];
	char attr_buff[FILE_NAME_SIZE];

	sprintf(pp->vendor_id, "IBM");

	condlog(3, "%s: vendor = %s", pp->dev, pp->vendor_id);

	if (sysfs_get_devtype(sysfs_path, pp->dev,
			      attr_buff, FILE_NAME_SIZE))
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
	if(safe_sprintf(attr_path, "%s/block/%s/device",
			sysfs_path, pp->dev)) {
		condlog(0, "attr_path too small");
		return 1;
	}
	if (0 > sysfs_get_link(attr_path, attr_buff, sizeof(attr_buff)))
		return 1;
	
	basename(attr_buff, attr_path);
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
common_sysfs_pathinfo (struct path * pp)
{
	if (sysfs_get_bus(sysfs_path, pp))
		return 1;

	condlog(3, "%s: bus = %i", pp->dev, pp->bus);

	if (sysfs_get_dev(sysfs_path, pp->dev,
			  pp->dev_t, BLK_DEV_SIZE))
		return 1;

	condlog(3, "%s: dev_t = %s", pp->dev, pp->dev_t);

	if (sysfs_get_size(sysfs_path, pp->dev, &pp->size))
		return 1;

	condlog(3, "%s: size = %llu", pp->dev, pp->size);

	return 0;
}

extern int
sysfs_pathinfo(struct path * pp)
{
	if (common_sysfs_pathinfo(pp))
		return 1;

	if (pp->bus == SYSFS_BUS_UNDEF)
		return 0;
	else if (pp->bus == SYSFS_BUS_SCSI) {
		if (scsi_sysfs_pathinfo(pp))
			return 1;
	} else if (pp->bus == SYSFS_BUS_CCW) {
		if (ccw_sysfs_pathinfo(pp))
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
get_state (struct path * pp)
{
	struct checker * c = &pp->checker;

	if (!pp->mpp)
		return 0;

	if (!checker_selected(c)) {
		select_checker(pp);
		if (!checker_selected(c))
			return 1;
		checker_set_fd(c, pp->fd);
		if (checker_init(c, &pp->mpp->mpcontext))
			return 1;
	}
	pp->state = checker_check(c);
	condlog(3, "%s: state = %i", pp->dev, pp->state);
	if (pp->state == PATH_DOWN)
		condlog(2, "%s: checker msg is \"%s\"",
			pp->dev, checker_message(c));
	return 0;
}

static int
get_prio (struct path * pp)
{
	char buff[CALLOUT_MAX_SIZE];
	char prio[16];

	if (!pp->getprio_selected) {
		select_getprio(pp);
		pp->getprio_selected = 1;
	}
	if (!pp->getprio) {
		pp->priority = PRIO_DEFAULT;
	} else if (apply_format(pp->getprio, &buff[0], pp)) {
		condlog(0, "error formatting prio callout command");
		pp->priority = PRIO_UNDEF;
		return 1;
	} else if (execute_program(buff, prio, 16)) {
		condlog(0, "error calling out %s", buff);
		pp->priority = PRIO_UNDEF;
		return 1;
	} else
		pp->priority = atoi(prio);

	condlog(3, "%s: prio = %u", pp->dev, pp->priority);
	return 0;
}

static int
get_uid (struct path * pp)
{
	char buff[CALLOUT_MAX_SIZE];

	if (!pp->getuid)
		select_getuid(pp);

	if (apply_format(pp->getuid, &buff[0], pp)) {
		condlog(0, "error formatting uid callout command");
		memset(pp->wwid, 0, WWID_SIZE);
	} else if (execute_program(buff, pp->wwid, WWID_SIZE)) {
		condlog(0, "error calling out %s", buff);
		memset(pp->wwid, 0, WWID_SIZE);
		return 1;
	}
	condlog(3, "%s: uid = %s (callout)", pp->dev ,pp->wwid);
	return 0;
}

extern int
pathinfo (struct path *pp, vector hwtable, int mask)
{
	condlog(3, "%s: mask = 0x%x", pp->dev, mask);

	/*
	 * fetch info available in sysfs
	 */
	if (mask & DI_SYSFS && sysfs_pathinfo(pp))
		return 1;

	/*
	 * fetch info not available through sysfs
	 */
	if (pp->fd < 0)
		pp->fd = opennode(pp->dev, O_RDONLY);

	if (pp->fd < 0)
		goto blank;

	if (pp->bus == SYSFS_BUS_SCSI &&
	    scsi_ioctl_pathinfo(pp, mask))
		goto blank;

	if (mask & DI_CHECKER && get_state(pp))
		goto blank;
	
	 /*
	  * Retrieve path priority for even PATH_DOWN paths if it has never
	  * been successfully obtained before.
	  */
	if (mask & DI_PRIO &&
	    (pp->state != PATH_DOWN || pp->priority == PRIO_UNDEF))
		get_prio(pp);

	if (mask & DI_WWID && !strlen(pp->wwid))
		get_uid(pp);

	return 0;

blank:
	/*
	 * Recoverable error, for example faulty or offline path
	 */
	memset(pp->wwid, 0, WWID_SIZE);
	pp->state = PATH_DOWN;
	return 0;
}
