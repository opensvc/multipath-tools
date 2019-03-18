/*
 * (C) Copyright IBM Corp. 2004, 2005   All Rights Reserved.
 *
 * rtpg.c
 *
 * Tool to make use of a SCSI-feature called Asymmetric Logical Unit Access.
 * It determines the ALUA state of a device and prints a priority value to
 * stdout.
 *
 * Author(s): Jan Kunigk
 *            S. Bader <shbader@de.ibm.com>
 *
 * This file is released under the GPL.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <inttypes.h>
#include <libudev.h>
#include <errno.h>

#define __user
#include <scsi/sg.h>

#include "../structs.h"
#include "../prio.h"
#include "../discovery.h"
#include "../unaligned.h"
#include "../debug.h"
#include "alua_rtpg.h"

#define SENSE_BUFF_LEN  32
#define SGIO_TIMEOUT     60000

#define PRINT_DEBUG(f, a...) \
	condlog(4, "alua: " f, ##a)

/*
 * Optionally print the commands sent and the data received a hex dump.
 */
#if DEBUG > 0
#if DEBUG_DUMPHEX > 0
#define PRINT_HEX(p, l)	print_hex(p, l)
void
print_hex(unsigned char *p, unsigned long len)
{
	int	i;

	for(i = 0; i < len; i++) {
		if (i % 16 == 0)
			printf("%04x: ", i);
		printf("%02x%s", p[i], (((i + 1) % 16) == 0) ? "\n" : " ");
	}
	printf("\n");
}
#else
#define PRINT_HEX(p, l)
#endif
#else
#define PRINT_HEX(p, l)
#endif

/*
 * Returns 0 if the SCSI command either was successful or if the an error was
 * recovered, otherwise 1. (definitions taken from sg_err.h)
 */
#define SCSI_CHECK_CONDITION    0x2
#define SCSI_COMMAND_TERMINATED 0x22
#define SG_ERR_DRIVER_SENSE     0x08
#define RECOVERED_ERROR 0x01
#define NOT_READY 0x2
#define UNIT_ATTENTION 0x6

enum scsi_disposition {
	SCSI_GOOD = 0,
	SCSI_ERROR,
	SCSI_RETRY,
};

static int
scsi_error(struct sg_io_hdr *hdr, int opcode)
{
	int sense_key, asc, ascq;

	/* Treat SG_ERR here to get rid of sg_err.[ch] */
	hdr->status &= 0x7e;

	if (
		(hdr->status == 0)        &&
		(hdr->host_status == 0)   &&
		(hdr->driver_status == 0)
	) {
		return SCSI_GOOD;
	}

	sense_key = asc = ascq = -1;
	if (
		(hdr->status == SCSI_CHECK_CONDITION)    ||
		(hdr->status == SCSI_COMMAND_TERMINATED) ||
		((hdr->driver_status & 0xf) == SG_ERR_DRIVER_SENSE)
	) {
		if (hdr->sbp && (hdr->sb_len_wr > 2)) {
			unsigned char *	sense_buffer = hdr->sbp;

			if (sense_buffer[0] & 0x2) {
				sense_key = sense_buffer[1] & 0xf;
				if (hdr->sb_len_wr > 3)
					asc = sense_buffer[2];
				if (hdr->sb_len_wr > 4)
					ascq = sense_buffer[3];
			} else {
				sense_key = sense_buffer[2] & 0xf;
				if (hdr->sb_len_wr > 13)
					asc = sense_buffer[12];
				if (hdr->sb_len_wr > 14)
					ascq = sense_buffer[13];
			}

			if (sense_key == RECOVERED_ERROR)
				return SCSI_GOOD;
		}
	}

	PRINT_DEBUG("alua: SCSI error for command %02x: status %02x, sense %02x/%02x/%02x",
		    opcode, hdr->status, sense_key, asc, ascq);

	if (sense_key == UNIT_ATTENTION || sense_key == NOT_READY)
		return SCSI_RETRY;
	else
		return SCSI_ERROR;
}

/*
 * Helper function to setup and run a SCSI inquiry command.
 */
static int
do_inquiry_sg(int fd, int evpd, unsigned int codepage,
	      void *resp, int resplen, unsigned int timeout)
{
	struct inquiry_command	cmd;
	struct sg_io_hdr	hdr;
	unsigned char		sense[SENSE_BUFF_LEN];
	int rc, retry_count = 3;

retry:
	memset(&cmd, 0, sizeof(cmd));
	cmd.op = OPERATION_CODE_INQUIRY;
	if (evpd) {
		inquiry_command_set_evpd(&cmd);
		cmd.page = codepage;
	}
	put_unaligned_be16(resplen, cmd.length);
	PRINT_HEX((unsigned char *) &cmd, sizeof(cmd));

	memset(&hdr, 0, sizeof(hdr));
	hdr.interface_id	= 'S';
	hdr.cmdp		= (unsigned char *) &cmd;
	hdr.cmd_len		= sizeof(cmd);
	hdr.dxfer_direction	= SG_DXFER_FROM_DEV;
	hdr.dxferp		= resp;
	hdr.dxfer_len		= resplen;
	hdr.sbp			= sense;
	hdr.mx_sb_len		= sizeof(sense);
	hdr.timeout		= get_prio_timeout(timeout, SGIO_TIMEOUT);

	if (ioctl(fd, SG_IO, &hdr) < 0) {
		PRINT_DEBUG("do_inquiry: IOCTL failed!");
		return -RTPG_INQUIRY_FAILED;
	}

	rc = scsi_error(&hdr, OPERATION_CODE_INQUIRY);
	if (rc == SCSI_ERROR) {
		PRINT_DEBUG("do_inquiry: SCSI error!");
		return -RTPG_INQUIRY_FAILED;
	} else if (rc == SCSI_RETRY) {
		if (--retry_count >= 0)
			goto retry;
		PRINT_DEBUG("do_inquiry: retries exhausted!");
		return -RTPG_INQUIRY_FAILED;
	}
	PRINT_HEX((unsigned char *) resp, resplen);

	return 0;
}

int do_inquiry(const struct path *pp, int evpd, unsigned int codepage,
	       void *resp, int resplen, unsigned int timeout)
{
	struct udev_device *ud;

	ud = udev_device_get_parent_with_subsystem_devtype(pp->udev, "scsi",
							   "scsi_device");
	if (ud != NULL) {
		int rc;

		if (!evpd)
			rc = sysfs_get_inquiry(ud, resp, resplen);
		else
			rc = sysfs_get_vpd(ud, codepage, resp, resplen);

		if (rc >= 0) {
			PRINT_HEX((unsigned char *) resp, resplen);
			return 0;
		}
	}
	return do_inquiry_sg(pp->fd, evpd, codepage, resp, resplen, timeout);
}

/*
 * This function returns the support for target port groups by evaluating the
 * data returned by the standard inquiry command.
 */
int
get_target_port_group_support(const struct path *pp, unsigned int timeout)
{
	struct inquiry_data	inq;
	int			rc;

	memset((unsigned char *)&inq, 0, sizeof(inq));
	rc = do_inquiry(pp, 0, 0x00, &inq, sizeof(inq), timeout);
	if (!rc) {
		rc = inquiry_data_get_tpgs(&inq);
	}

	return rc;
}

static int
get_sysfs_pg83(const struct path *pp, unsigned char *buff, int buflen)
{
	struct udev_device *parent = pp->udev;

	while (parent) {
		const char *subsys = udev_device_get_subsystem(parent);
		if (subsys && !strncmp(subsys, "scsi", 4))
			break;
		parent = udev_device_get_parent(parent);
	}

	if (!parent || sysfs_get_vpd(parent, 0x83, buff, buflen) <= 0) {
		PRINT_DEBUG("failed to read sysfs vpd pg83");
		return -1;
	}
	return 0;
}

int
get_target_port_group(const struct path * pp, unsigned int timeout)
{
	unsigned char		*buf;
	struct vpd83_data *	vpd83;
	struct vpd83_dscr *	dscr;
	int			rc;
	int			buflen, scsi_buflen;

	buflen = 4096;
	buf = (unsigned char *)malloc(buflen);
	if (!buf) {
		PRINT_DEBUG("malloc failed: could not allocate"
			     "%u bytes", buflen);
		return -RTPG_RTPG_FAILED;
	}

	memset(buf, 0, buflen);

	rc = get_sysfs_pg83(pp, buf, buflen);

	if (rc < 0) {
		rc = do_inquiry(pp, 1, 0x83, buf, buflen, timeout);
		if (rc < 0)
			goto out;

		scsi_buflen = get_unaligned_be16(&buf[2]) + 4;
		/* Paranoia */
		if (scsi_buflen >= USHRT_MAX)
			scsi_buflen = USHRT_MAX;
		if (buflen < scsi_buflen) {
			free(buf);
			buf = (unsigned char *)malloc(scsi_buflen);
			if (!buf) {
				PRINT_DEBUG("malloc failed: could not allocate"
					    "%u bytes", scsi_buflen);
				return -RTPG_RTPG_FAILED;
			}
			buflen = scsi_buflen;
			memset(buf, 0, buflen);
			rc = do_inquiry(pp, 1, 0x83, buf, buflen, timeout);
			if (rc < 0)
				goto out;
		}
	}

	vpd83 = (struct vpd83_data *) buf;
	rc = -RTPG_NO_TPG_IDENTIFIER;
	FOR_EACH_VPD83_DSCR(vpd83, dscr) {
		if (vpd83_dscr_istype(dscr, IDTYPE_TARGET_PORT_GROUP)) {
			struct vpd83_tpg_dscr *p;
			if (rc != -RTPG_NO_TPG_IDENTIFIER) {
				PRINT_DEBUG("get_target_port_group: more "
					    "than one TPG identifier found!");
				continue;
			}
			p  = (struct vpd83_tpg_dscr *)dscr->data;
			rc = get_unaligned_be16(p->tpg);
		}
	}

	if (rc == -RTPG_NO_TPG_IDENTIFIER) {
		PRINT_DEBUG("get_target_port_group: "
			    "no TPG identifier found!");
	}
out:
	free(buf);
	return rc;
}

int
do_rtpg(int fd, void* resp, long resplen, unsigned int timeout)
{
	struct rtpg_command	cmd;
	struct sg_io_hdr	hdr;
	unsigned char		sense[SENSE_BUFF_LEN];
	int retry_count = 3, rc;

retry:
	memset(&cmd, 0, sizeof(cmd));
	cmd.op			= OPERATION_CODE_RTPG;
	rtpg_command_set_service_action(&cmd);
	put_unaligned_be32(resplen, cmd.length);
	PRINT_HEX((unsigned char *) &cmd, sizeof(cmd));

	memset(&hdr, 0, sizeof(hdr));
	hdr.interface_id	= 'S';
	hdr.cmdp		= (unsigned char *) &cmd;
	hdr.cmd_len		= sizeof(cmd);
	hdr.dxfer_direction	= SG_DXFER_FROM_DEV;
	hdr.dxferp		= resp;
	hdr.dxfer_len		= resplen;
	hdr.mx_sb_len		= sizeof(sense);
	hdr.sbp			= sense;
	hdr.timeout		= get_prio_timeout(timeout, SGIO_TIMEOUT);

	if (ioctl(fd, SG_IO, &hdr) < 0) {
		condlog(2, "%s: sg ioctl failed: %s",
			__func__, strerror(errno));
		return -RTPG_RTPG_FAILED;
	}

	rc = scsi_error(&hdr, OPERATION_CODE_RTPG);
	if (rc == SCSI_ERROR) {
		PRINT_DEBUG("do_rtpg: SCSI error!");
		return -RTPG_RTPG_FAILED;
	} else if (rc == SCSI_RETRY) {
		if (--retry_count >= 0)
			goto retry;
		PRINT_DEBUG("do_rtpg: retries exhausted!");
		return -RTPG_RTPG_FAILED;
	}
	PRINT_HEX(resp, resplen);

	return 0;
}

int
get_asymmetric_access_state(const struct path *pp, unsigned int tpg,
			    unsigned int timeout)
{
	unsigned char		*buf;
	struct rtpg_data *	tpgd;
	struct rtpg_tpg_dscr *	dscr;
	int			rc;
	int			buflen;
	uint64_t		scsi_buflen;
	int fd = pp->fd;

	buflen = 4096;
	buf = (unsigned char *)malloc(buflen);
	if (!buf) {
		PRINT_DEBUG ("malloc failed: could not allocate"
			"%u bytes", buflen);
		return -RTPG_RTPG_FAILED;
	}
	memset(buf, 0, buflen);
	rc = do_rtpg(fd, buf, buflen, timeout);
	if (rc < 0) {
		PRINT_DEBUG("%s: do_rtpg returned %d", __func__, rc);
		goto out;
	}
	scsi_buflen = get_unaligned_be32(&buf[0]) + 4;
	if (scsi_buflen > UINT_MAX)
		scsi_buflen = UINT_MAX;
	if (buflen < scsi_buflen) {
		free(buf);
		buf = (unsigned char *)malloc(scsi_buflen);
		if (!buf) {
			PRINT_DEBUG("malloc failed: could not allocate %"
				    PRIu64 " bytes", scsi_buflen);
			return -RTPG_RTPG_FAILED;
		}
		buflen = scsi_buflen;
		memset(buf, 0, buflen);
		rc = do_rtpg(fd, buf, buflen, timeout);
		if (rc < 0)
			goto out;
	}

	tpgd = (struct rtpg_data *) buf;
	rc   = -RTPG_TPG_NOT_FOUND;
	RTPG_FOR_EACH_PORT_GROUP(tpgd, dscr) {
		if (get_unaligned_be16(dscr->tpg) == tpg) {
			if (rc != -RTPG_TPG_NOT_FOUND) {
				PRINT_DEBUG("get_asymmetric_access_state: "
					"more than one entry with same port "
					"group.");
			} else {
				condlog(5, "pref=%i", dscr->b0);
				rc = rtpg_tpg_dscr_get_aas(dscr);
			}
		}
	}
	if (rc == -RTPG_TPG_NOT_FOUND)
		condlog(2, "%s: port group %d not found", __func__, tpg);
out:
	free(buf);
	return rc;
}
