#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <errno.h>

#include <sysfs/dlist.h>
#include <sysfs/libsysfs.h>

#include "vector.h"
#include "memory.h"
#include "blacklist.h"
#include "util.h"
#include "structs.h"
#include "callout.h"
#include "config.h"
#include "debug.h"
#include "propsel.h"
#include "sg_include.h"
#include "discovery.h"

#include "../libcheckers/path_state.h"

#define readattr(a,b) \
	sysfs_read_attribute_value(a, b, sizeof(b))

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

int
path_discovery (vector pathvec, struct config * conf, int flag)
{
	struct sysfs_directory * sdir;
	struct sysfs_directory * devp;
	char path[FILE_NAME_SIZE];
	struct path * pp;
	int r = 0;

	if(safe_sprintf(path, "%s/block", sysfs_path)) {
		condlog(0, "path too small");
		return 1;
	}
	sdir = sysfs_open_directory(path);
	sysfs_read_directory(sdir);

	dlist_for_each_data(sdir->subdirs, devp, struct sysfs_directory) {
		if (blacklist(conf->blist, devp->name))
			continue;

		if(safe_sprintf(path, "%s/block/%s/device", sysfs_path,
				devp->name)) {
			condlog(0, "path too small");
			sysfs_close_directory(sdir);
			return 1;
		}
				
		if (!filepresent(path))
			continue;

		pp = find_path_by_dev(pathvec, devp->name);

		if (!pp) {
			/*
			 * new path : alloc, store and fetch info
			 * the caller wants
			 */
			if (!store_pathinfo(pathvec, conf->hwtable,
					   devp->name, flag)) {
				r++;
				continue;
			}
		} else {
			/*
			 * path already known :
			 * refresh only what the caller wants
			 */
			if (pathinfo(pp, conf->hwtable, flag)) {
				r++;
				continue;
			}
		}
	}
	sysfs_close_directory(sdir);
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
	char attr_path[SYSFS_PATH_SIZE]; \
	char attr_buff[SYSFS_PATH_SIZE]; \
	int attr_len; \
\
	if (safe_sprintf(attr_path, fmt, sysfs_path, dev)) \
		return 1; \
\
	if (wait_for_file(attr_path)) \
		return 1; \
\
	if (0 > sysfs_read_attribute_value(attr_path, attr_buff, sizeof(attr_buff))) \
		return 1; \
\
	attr_len = strlen(attr_buff); \
	if (attr_len < 2 || attr_len - 1 > len) \
		return 1; \
\
	strncpy(buff, attr_buff, attr_len - 1); \
	buff[attr_len - 1] = '\0'; \
	return 0; \
}

declare_sysfs_get_str(vendor, "%s/block/%s/device/vendor");
declare_sysfs_get_str(model, "%s/block/%s/device/model");
declare_sysfs_get_str(rev, "%s/block/%s/device/rev");
declare_sysfs_get_str(dev, "%s/block/%s/dev");

#define declare_sysfs_get_val(fname, fmt) \
extern unsigned long long  \
sysfs_get_##fname (char * sysfs_path, char * dev) \
{ \
	char attr_path[SYSFS_PATH_SIZE]; \
	char attr_buff[SYSFS_PATH_SIZE]; \
	int r; \
	unsigned long long val; \
\
	if (safe_sprintf(attr_path, fmt, sysfs_path, dev)) \
		return 0; \
\
	if (wait_for_file(attr_path)) \
		return 0; \
\
	if (0 > sysfs_read_attribute_value(attr_path, attr_buff, sizeof(attr_buff))) \
		return 0; \
\
	r = sscanf(attr_buff, "%llu\n", &val); \
\
	if (r != 1) \
		return 0; \
	else \
		return (val); \
}

declare_sysfs_get_val(size, "%s/block/%s/size");

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

int
get_claimed(char * devname)
{
	int fd = opennode(devname, O_EXCL);

	if (fd < 0 && errno == EBUSY)
		return 1;

	if (fd >= 0)
		close(fd);

	return 0;
}	

extern int
devt2devname (char *devname, char *devt)
{
	struct sysfs_directory * sdir;
	struct sysfs_directory * devp;
	char block_path[FILE_NAME_SIZE];
	char attr_path[FILE_NAME_SIZE];
	char attr_value[16];
	int len;

	if(safe_sprintf(block_path, "%s/block", sysfs_path)) {
		condlog(0, "block_path too small");
		exit(1);
	}
	sdir = sysfs_open_directory(block_path);
	sysfs_read_directory(sdir);

	dlist_for_each_data (sdir->subdirs, devp, struct sysfs_directory) {
		if(safe_sprintf(attr_path, "%s/%s/dev",
				block_path, devp->name)) {
			condlog(0, "attr_path too small");
			exit(1);
		}
		sysfs_read_attribute_value(attr_path, attr_value,
					   sizeof(attr_value));

		len = strlen(attr_value);

		/* discard newline */
		if (len > 1) len--;

		if (strlen(devt) == len &&
		    strncmp(attr_value, devt, len) == 0) {
			if(safe_sprintf(attr_path, "%s/%s",
					block_path, devp->name)) {
				condlog(0, "attr_path too small");
				exit(1);
			}
			sysfs_get_name_from_path(attr_path, devname,
						 FILE_NAME_SIZE);
			sysfs_close_directory(sdir);
			return 0;
		}
	}
	sysfs_close_directory(sdir);
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

int
get_serial (char * str, int fd)
{
        int len;
        char buff[MX_ALLOC_LEN + 1];

	if (fd < 0)
                return 0;

	if (0 == do_inq(fd, 0, 1, 0x80, buff, MX_ALLOC_LEN, 0)) {
		len = buff[3];
		if (len > 0) {
			memcpy(str, buff + 4, len);
			str[len] = '\0';
		}
		return 1;
	}
        return 0;
}

static void
sysfs_get_bus (char * sysfs_path, struct path * curpath)
{
	struct sysfs_device *sdev;
	char attr_path[FILE_NAME_SIZE];
	char attr_buff[FILE_NAME_SIZE];

	curpath->bus = SYSFS_BUS_UNDEF;

	/*
	 * This is ugly : we should be able to do a simple
	 * get_link("%s/block/%s/device/bus", ...) but it just
	 * won't work
	 */
	if(safe_sprintf(attr_path, "%s/block/%s/device",
			sysfs_path, curpath->dev)) {
		condlog(0, "attr_path too small");
		return;
	}

	if (0 > sysfs_get_link(attr_path, attr_buff, sizeof(attr_buff)))
		return;

	sdev = sysfs_open_device_path(attr_buff);

	if (!strncmp(sdev->bus, "scsi", 4))
		curpath->bus = SYSFS_BUS_SCSI;
	else if (!strncmp(sdev->bus, "ide", 3))
		curpath->bus = SYSFS_BUS_IDE;

	return;
}

static int
scsi_sysfs_pathinfo (struct path * curpath)
{
	char attr_path[FILE_NAME_SIZE];
	char attr_buff[FILE_NAME_SIZE];

	if (sysfs_get_vendor(sysfs_path, curpath->dev,
			     curpath->vendor_id, SCSI_VENDOR_SIZE))
		return 1;

	condlog(3, "vendor = %s", curpath->vendor_id);

	if (sysfs_get_model(sysfs_path, curpath->dev,
			    curpath->product_id, SCSI_PRODUCT_SIZE))
		return 1;

	condlog(3, "product = %s", curpath->product_id);

	if (sysfs_get_rev(sysfs_path, curpath->dev,
			  curpath->rev, SCSI_REV_SIZE))
		return 1;

	condlog(3, "rev = %s", curpath->rev);

	/*
	 * host / bus / target / lun
	 */
	if(safe_sprintf(attr_path, "%s/block/%s/device",
			sysfs_path, curpath->dev)) {
		condlog(0, "attr_path too small");
		return 1;
	}
	if (0 > sysfs_get_link(attr_path, attr_buff, sizeof(attr_buff)))
		return 1;
	
	basename(attr_buff, attr_path);

	sscanf(attr_path, "%i:%i:%i:%i",
			&curpath->sg_id.host_no,
			&curpath->sg_id.channel,
			&curpath->sg_id.scsi_id,
			&curpath->sg_id.lun);
	condlog(3, "h:b:t:l = %i:%i:%i:%i",
			curpath->sg_id.host_no,
			curpath->sg_id.channel,
			curpath->sg_id.scsi_id,
			curpath->sg_id.lun);

	/*
	 * target node name
	 */
	if(safe_sprintf(attr_path,
			"%s/class/fc_transport/target%i:%i:%i/node_name",
			sysfs_path,
			curpath->sg_id.host_no,
			curpath->sg_id.channel,
			curpath->sg_id.scsi_id)) {
		condlog(0, "attr_path too small");
		return 1;
	}
	if (0 <= readattr(attr_path, attr_buff) && strlen(attr_buff) > 0)
		strncpy(curpath->tgt_node_name, attr_buff,
			strlen(attr_buff) - 1);
	condlog(3, "tgt_node_name = %s", curpath->tgt_node_name);

	return 0;
}

static int
common_sysfs_pathinfo (struct path * curpath)
{
	sysfs_get_bus(sysfs_path, curpath);
	condlog(3, "bus = %i", curpath->bus);

	if (sysfs_get_dev(sysfs_path, curpath->dev,
			  curpath->dev_t, BLK_DEV_SIZE))
		return 1;

	condlog(3, "dev_t = %s", curpath->dev_t);

	curpath->size = sysfs_get_size(sysfs_path, curpath->dev);

	if (curpath->size == 0)
		return 1;

	condlog(3, "size = %llu", curpath->size);

	return 0;
}

extern int
sysfs_pathinfo(struct path * curpath)
{
	if (common_sysfs_pathinfo(curpath))
		return 1;

	if (curpath->bus == SYSFS_BUS_UNDEF)
		return 0;
	else if (curpath->bus == SYSFS_BUS_SCSI)
		if (scsi_sysfs_pathinfo(curpath))
			return 1;

	return 0;
}

static int
apply_format (char * string, char * cmd, struct path * pp)
{
	char * pos;
	char * dst;
	char * p;
	int len;
	int myfree;

	if (!string)
		return 1;

	if (!cmd)
		return 1;

	dst = cmd;

	if (!dst)
		return 1;

	p = dst;
	pos = strchr(string, '%');
	myfree = CALLOUT_MAX_SIZE;

	if (!pos) {
		strcpy(dst, string);
		return 0;
	}

	len = (int) (pos - string) + 1;
	myfree -= len;

	if (myfree < 2)
		return 1;

	snprintf(p, len, "%s", string);
	p += len - 1;
	pos++;

	switch (*pos) {
	case 'n':
		len = strlen(pp->dev) + 1;
		myfree -= len;

		if (myfree < 2)
			return 1;

		snprintf(p, len, "%s", pp->dev);
		p += len - 1;
		break;
	case 'd':
		len = strlen(pp->dev_t) + 1;
		myfree -= len;

		if (myfree < 2)
			return 1;

		snprintf(p, len, "%s", pp->dev_t);
		p += len - 1;
		break;
	default:
		break;
	}
	pos++;

	if (!*pos)
		return 0;

	len = strlen(pos) + 1;
	myfree -= len;

	if (myfree < 2)
		return 1;

	snprintf(p, len, "%s", pos);
	condlog(3, "reformated callout = %s", dst);
	return 0;
}

static int
scsi_ioctl_pathinfo (struct path * pp, int mask)
{
	if (mask & DI_SERIAL) {
		get_serial(pp->serial, pp->fd);
		condlog(3, "serial = %s", pp->serial);
	}

	return 0;
}

extern int
pathinfo (struct path *pp, vector hwtable, int mask)
{
	char buff[CALLOUT_MAX_SIZE];
	char prio[16];

	condlog(3, "===== path info %s (mask 0x%x) =====", pp->dev, mask);

	/*
	 * fetch info available in sysfs
	 */
	if (mask & DI_SYSFS && sysfs_pathinfo(pp))
		return 1;

	/*
	 * fetch info not available through sysfs
	 */
	if (mask & DI_CLAIMED) {
		pp->claimed = get_claimed(pp->dev);
		condlog(3, "claimed = %i", pp->claimed);
	}
	if (pp->fd < 0)
		pp->fd = opennode(pp->dev, O_RDONLY);

	if (pp->fd < 0)
		goto out;

	if (pp->bus == SYSFS_BUS_SCSI &&
	    scsi_ioctl_pathinfo(pp, mask))
		goto out;

	/* get and store hwe pointer */
	pp->hwe = find_hwe(hwtable, pp->vendor_id, pp->product_id);

	/*
	 * get path state, no message collection, no context
	 */
	if (mask & DI_CHECKER) {
		if (!pp->checkfn)
			select_checkfn(pp);

		pp->state = pp->checkfn(pp->fd, NULL, NULL);
		condlog(3, "state = %i", pp->state);
	}
	
	/*
	 * get path prio
	 */
	if (mask & DI_PRIO) {
		if (!pp->getprio)
			select_getprio(pp);

		if (!pp->getprio) {
			pp->priority = 1;
		} else if (apply_format(pp->getprio, &buff[0], pp)) {
			condlog(0, "error formatting prio callout command");
			pp->priority = -1;
		} else if (execute_program(buff, prio, 16)) {
			condlog(0, "error calling out %s", buff);
			pp->priority = -1;
		} else
			pp->priority = atoi(prio);

		condlog(3, "prio = %u", pp->priority);
	}

	/*
	 * get path uid
	 */
	if (mask & DI_WWID && !strlen(pp->wwid)) {
		if (!pp->getuid)
			select_getuid(pp);

		if (apply_format(pp->getuid, &buff[0], pp)) {
			condlog(0, "error formatting uid callout command");
			memset(pp->wwid, 0, WWID_SIZE);
		} else if (execute_program(buff, pp->wwid, WWID_SIZE)) {
			condlog(0, "error calling out %s", buff);
			memset(pp->wwid, 0, WWID_SIZE);
		}
		condlog(3, "uid = %s (callout)", pp->wwid);
	}
	else if (strlen(pp->wwid))
		condlog(3, "uid = %s (cache)", pp->wwid);

	return 0;

out:
	/*
	 * Recoverable error, for example faulty or offline path
	 * Set up safe defaults, don't trust the cache
	 */
	memset(pp->wwid, 0, WWID_SIZE);
	pp->state = PATH_DOWN;
	return 0;
}
