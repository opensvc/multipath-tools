/*
 * Copyright 2005 Network Appliance, Inc., All Rights Reserved
 * Author:  David Wysochanski available at davidw@netapp.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License v2 for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "sg_include.h"
#include "debug.h"
#include "prio.h"
#include "structs.h"
#include "unaligned.h"

#define INQUIRY_CMD	0x12
#define INQUIRY_CMDLEN	6
#define DEFAULT_PRIOVAL	10
#define RESULTS_MAX	256
#define SG_TIMEOUT	60000

#define pp_ontap_log(prio, fmt, args...) \
	condlog(prio, "%s: ontap prio: " fmt, dev, ##args)

static void dump_cdb(unsigned char *cdb, int size)
{
	int i;
	char buf[10*5+1];
	char * p = &buf[0];

	condlog(0, "- SCSI CDB: ");
	for (i=0; i<size; i++) {
		p += snprintf(p, 10*(size-i), "0x%02x ", cdb[i]);
	}
	condlog(0, "%s", buf);
}

static void process_sg_error(struct sg_io_hdr *io_hdr)
{
	int i;
	char buf[128*5+1];
	char * p = &buf[0];

	condlog(0, "- masked_status=0x%02x, host_status=0x%02x, "
		"driver_status=0x%02x", io_hdr->masked_status,
		io_hdr->host_status, io_hdr->driver_status);
	if (io_hdr->sb_len_wr > 0) {
		condlog(0, "- SCSI sense data: ");
		for (i=0; i<io_hdr->sb_len_wr; i++) {
			p += snprintf(p, 128*(io_hdr->sb_len_wr-i), "0x%02x ",
				      io_hdr->sbp[i]);
		}
		condlog(0, "%s", buf);
	}
}

/*
 * Returns:
 * -1: error, errno set
 *  0: success
 */
static int send_gva(const char *dev, int fd, unsigned char pg,
		    unsigned char *results, int *results_size,
		    unsigned int timeout)
{
	unsigned char sb[128];
	unsigned char cdb[10] = {0xc0, 0, 0x1, 0xa, 0x98, 0xa,
				 pg, sizeof(sb), 0, 0};
	struct sg_io_hdr io_hdr;
	int ret = -1;

	memset(&io_hdr, 0, sizeof (struct sg_io_hdr));
	memset(results, 0, *results_size);
	io_hdr.interface_id = 'S';
	io_hdr.cmd_len = sizeof (cdb);
	io_hdr.mx_sb_len = sizeof (sb);
	io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	io_hdr.dxfer_len = *results_size;
	io_hdr.dxferp = results;
	io_hdr.cmdp = cdb;
	io_hdr.sbp = sb;
	io_hdr.timeout = get_prio_timeout(timeout, SG_TIMEOUT);
	io_hdr.pack_id = 0;
	if (ioctl(fd, SG_IO, &io_hdr) < 0) {
		pp_ontap_log(0, "SG_IO ioctl failed, errno=%d", errno);
		dump_cdb(cdb, sizeof(cdb));
		goto out;
	}
	if (io_hdr.info & SG_INFO_OK_MASK) {
		pp_ontap_log(0, "SCSI error");
		dump_cdb(cdb, sizeof(cdb));
		process_sg_error(&io_hdr);
		goto out;
	}

	if (results[4] != 0x0a || results[5] != 0x98 ||
	    results[6] != 0x0a ||results[7] != 0x01) {
		dump_cdb(cdb, sizeof(cdb));
		pp_ontap_log(0, "GVA return wrong format ");
		pp_ontap_log(0, "results[4-7] = 0x%02x 0x%02x 0x%02x 0x%02x",
			results[4], results[5], results[6], results[7]);
		goto out;
	}
	ret = 0;
out:
	return(ret);
}

/*
 * Returns:
 * -1: Unable to obtain proxy info
 *  0: Device _not_ proxy path
 *  1: Device _is_ proxy path
 */
static int get_proxy(const char *dev, int fd, unsigned int timeout)
{
	unsigned char results[256];
	unsigned char sb[128];
	unsigned char cdb[INQUIRY_CMDLEN] = {INQUIRY_CMD, 1, 0xc1, 0,
						   sizeof(sb), 0};
	struct sg_io_hdr io_hdr;
	int ret = -1;

	memset(&results, 0, sizeof (results));
	memset(&io_hdr, 0, sizeof (struct sg_io_hdr));
	io_hdr.interface_id = 'S';
	io_hdr.cmd_len = sizeof (cdb);
	io_hdr.mx_sb_len = sizeof (sb);
	io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	io_hdr.dxfer_len = sizeof (results);
	io_hdr.dxferp = results;
	io_hdr.cmdp = cdb;
	io_hdr.sbp = sb;
	io_hdr.timeout = get_prio_timeout(timeout, SG_TIMEOUT);
	io_hdr.pack_id = 0;
	if (ioctl(fd, SG_IO, &io_hdr) < 0) {
		pp_ontap_log(0, "ioctl sending inquiry command failed, "
			"errno=%d", errno);
		dump_cdb(cdb, sizeof(cdb));
		goto out;
	}
	if (io_hdr.info & SG_INFO_OK_MASK) {
		pp_ontap_log(0, "SCSI error");
		dump_cdb(cdb, sizeof(cdb));
		process_sg_error(&io_hdr);
		goto out;
	}

	if (results[1] != 0xc1 || results[8] != 0x0a ||
	    results[9] != 0x98 || results[10] != 0x0a ||
	    results[11] != 0x0 || results[12] != 0xc1 ||
	    results[13] != 0x0) {
		pp_ontap_log(0,"proxy info page in unknown format - ");
		pp_ontap_log(0,"results[8-13]=0x%02x 0x%02x 0x%02x 0x%02x "
			"0x%02x 0x%02x",
			results[8], results[9], results[10],
			results[11], results[12], results[13]);
		dump_cdb(cdb, sizeof(cdb));
		goto out;
	}
	ret = (results[19] & 0x02) >> 1;

out:
	return(ret);
}

/*
 * Returns priority of device based on device info.
 *
 * 4: FCP non-proxy, FCP proxy unknown, or unable to determine protocol
 * 3: iSCSI HBA
 * 2: iSCSI software
 * 1: FCP proxy
 */
static int ontap_prio(const char *dev, int fd, unsigned int timeout)
{
	unsigned char results[RESULTS_MAX];
	int results_size=RESULTS_MAX;
	int rc;
	int is_proxy;
	int is_iscsi_software;
	int is_iscsi_hardware;
	int tot_len;

	is_iscsi_software = is_iscsi_hardware = is_proxy = 0;

	memset(&results, 0, sizeof (results));
	rc = send_gva(dev, fd, 0x41, results, &results_size, timeout);
	if (rc >= 0) {
		tot_len = get_unaligned_be32(&results[0]);
		if (tot_len <= 8) {
			goto try_fcp_proxy;
		}
		if (results[8] != 0x41) {
			pp_ontap_log(0, "GVA page 0x41 error - "
				"results[8] = 0x%x", results[8]);
			goto try_fcp_proxy;
		}
		if ((strncmp((char *)&results[12], "ism_sw", 6) == 0) ||
		    (strncmp((char *)&results[12], "iswt", 4) == 0)) {
			is_iscsi_software = 1;
			goto prio_select;
		}
		else if (strncmp((char *)&results[12], "ism_sn", 6) == 0) {
			is_iscsi_hardware = 1;
			goto prio_select;
		}
	} else {
		return 0;
	}

try_fcp_proxy:
	rc = get_proxy(dev, fd, timeout);
	if (rc >= 0) {
		is_proxy = rc;
	}

prio_select:
	if (is_iscsi_hardware) {
		return 3;
	} else if (is_iscsi_software) {
		return 2;
	} else {
		if (is_proxy) {
			return 1;
		} else {
			/* Either non-proxy, or couldn't get proxy info */
			return 4;
		}
	}
}

int getprio (struct path * pp, char * args, unsigned int timeout)
{
	return ontap_prio(pp->dev, pp->fd, timeout);
}
