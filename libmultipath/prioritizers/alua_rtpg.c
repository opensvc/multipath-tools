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
#include <sys/ioctl.h>
#include <inttypes.h>

#define __user
#include <scsi/sg.h>

#include "alua_rtpg.h"

#define SENSE_BUFF_LEN  32
#define DEF_TIMEOUT     60000

/*
 * Macro used to print debug messaged.
 */
#if DEBUG > 0
#define PRINT_DEBUG(f, a...) \
		fprintf(stderr, "DEBUG: " f, ##a)
#else
#define PRINT_DEBUG(f, a...)
#endif

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

static int
scsi_error(struct sg_io_hdr *hdr)
{
	/* Treat SG_ERR here to get rid of sg_err.[ch] */
	hdr->status &= 0x7e;

	if (
		(hdr->status == 0)        &&
		(hdr->host_status == 0)   &&
		(hdr->driver_status == 0)
	) {
		return 0;
	}

	if (
		(hdr->status == SCSI_CHECK_CONDITION)    ||
		(hdr->status == SCSI_COMMAND_TERMINATED) ||
		((hdr->driver_status & 0xf) == SG_ERR_DRIVER_SENSE)
	) {
		if (hdr->sbp && (hdr->sb_len_wr > 2)) {
			int		sense_key;
			unsigned char *	sense_buffer = hdr->sbp;

			if (sense_buffer[0] & 0x2)
				sense_key = sense_buffer[1] & 0xf;
			else
				sense_key = sense_buffer[2] & 0xf;

			if (sense_key == RECOVERED_ERROR)
				return 0;
		}
	}

	return 1;
}

/*
 * Helper function to setup and run a SCSI inquiry command.
 */
int
do_inquiry(int fd, int evpd, unsigned int codepage, void *resp, int resplen)
{
	struct inquiry_command	cmd;
	struct sg_io_hdr	hdr;
	unsigned char		sense[SENSE_BUFF_LEN];

	memset(&cmd, 0, sizeof(cmd));
	cmd.op = OPERATION_CODE_INQUIRY;
	if (evpd) {
		inquiry_command_set_evpd(&cmd);
		cmd.page = codepage;
	}
	set_uint16(cmd.length, resplen);
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
	hdr.timeout		= DEF_TIMEOUT;

	if (ioctl(fd, SG_IO, &hdr) < 0) {
		PRINT_DEBUG("do_inquiry: IOCTL failed!\n");
		return -RTPG_INQUIRY_FAILED;
	}

	if (scsi_error(&hdr)) {
		PRINT_DEBUG("do_inquiry: SCSI error!\n");
		return -RTPG_INQUIRY_FAILED;
	}
	PRINT_HEX((unsigned char *) resp, resplen);

	return 0;
}

/*
 * This function returns the support for target port groups by evaluating the
 * data returned by the standard inquiry command.
 */
int
get_target_port_group_support(int fd)
{
	struct inquiry_data	inq;
	int			rc;

	memset((unsigned char *)&inq, 0, sizeof(inq));
	rc = do_inquiry(fd, 0, 0x00, &inq, sizeof(inq));
	if (!rc) {
		rc = inquiry_data_get_tpgs(&inq);
	}

	return rc;
}

int
get_target_port_group(int fd)
{
	unsigned char		*buf;
	struct vpd83_data *	vpd83;
	struct vpd83_dscr *	dscr;
	int			rc;
	int			buflen, scsi_buflen;

	buflen = 128; /* Lets start from 128 */
	buf = (unsigned char *)malloc(buflen);
	if (!buf) {
		PRINT_DEBUG("malloc failed: could not allocate"
			     "%u bytes\n", buflen);
		return -RTPG_RTPG_FAILED;
	}

	memset(buf, 0, buflen);
	rc = do_inquiry(fd, 1, 0x83, buf, buflen);
	if (rc < 0)
		goto out;

	scsi_buflen = (buf[2] << 8 | buf[3]) + 4;
	if (buflen < scsi_buflen) {
		free(buf);
		buf = (unsigned char *)malloc(scsi_buflen);
		if (!buf) {
			PRINT_DEBUG("malloc failed: could not allocate"
				     "%u bytes\n", scsi_buflen);
			return -RTPG_RTPG_FAILED;
		}
		buflen = scsi_buflen;
		memset(buf, 0, buflen);
		rc = do_inquiry(fd, 1, 0x83, buf, buflen);
		if (rc < 0)
			goto out;
	}

	vpd83 = (struct vpd83_data *) buf;
	rc = -RTPG_NO_TPG_IDENTIFIER;
	FOR_EACH_VPD83_DSCR(vpd83, dscr) {
		if (vpd83_dscr_istype(dscr, IDTYPE_TARGET_PORT_GROUP)) {
			struct vpd83_tpg_dscr *p;
			if (rc != -RTPG_NO_TPG_IDENTIFIER) {
				PRINT_DEBUG("get_target_port_group: more "
					    "than one TPG identifier found!\n");
				continue;
			}
			p  = (struct vpd83_tpg_dscr *)dscr->data;
			rc = get_uint16(p->tpg);
		}
	}

	if (rc == -RTPG_NO_TPG_IDENTIFIER) {
		PRINT_DEBUG("get_target_port_group: "
			    "no TPG identifier found!\n");
	}
out:
	free(buf);
	return rc;
}

int
do_rtpg(int fd, void* resp, long resplen)
{
	struct rtpg_command	cmd;
	struct sg_io_hdr	hdr;
	unsigned char		sense[SENSE_BUFF_LEN];

	memset(&cmd, 0, sizeof(cmd));
	cmd.op			= OPERATION_CODE_RTPG;
	rtpg_command_set_service_action(&cmd);
	set_uint32(cmd.length, resplen);
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
	hdr.timeout		= DEF_TIMEOUT;

	if (ioctl(fd, SG_IO, &hdr) < 0)
		return -RTPG_RTPG_FAILED;

	if (scsi_error(&hdr)) {
		PRINT_DEBUG("do_rtpg: SCSI error!\n");
		return -RTPG_RTPG_FAILED;
	}
	PRINT_HEX(resp, resplen);

	return 0;
}

int
get_asymmetric_access_state(int fd, unsigned int tpg)
{
	unsigned char		*buf;
	struct rtpg_data *	tpgd;
	struct rtpg_tpg_dscr *	dscr;
	int			rc;
	int			buflen;
	uint32_t		scsi_buflen;

	buflen = 128; /* Initial value from old code */
	buf = (unsigned char *)malloc(buflen);
	if (!buf) {
		PRINT_DEBUG ("malloc failed: could not allocate"
			"%u bytes\n", buflen);
		return -RTPG_RTPG_FAILED;
	}
	memset(buf, 0, buflen);
	rc = do_rtpg(fd, buf, buflen);
	if (rc < 0)
		goto out;
	scsi_buflen = (buf[0] << 24 | buf[1] << 16 | buf[2] << 8 | buf[3]) + 4;
	if (buflen < scsi_buflen) {
		free(buf);
		buf = (unsigned char *)malloc(scsi_buflen);
		if (!buf) {
			PRINT_DEBUG ("malloc failed: could not allocate"
				"%u bytes\n", scsi_buflen);
			return -RTPG_RTPG_FAILED;
		}
		buflen = scsi_buflen;
		memset(buf, 0, buflen);
		rc = do_rtpg(fd, buf, buflen);
		if (rc < 0)
			goto out;
	}

	tpgd = (struct rtpg_data *) buf;
	rc   = -RTPG_TPG_NOT_FOUND;
	RTPG_FOR_EACH_PORT_GROUP(tpgd, dscr) {
		if (get_uint16(dscr->tpg) == tpg) {
			if (rc != -RTPG_TPG_NOT_FOUND) {
				PRINT_DEBUG("get_asymmetric_access_state: "
					"more than one entry with same port "
					"group.\n");
			} else {
				PRINT_DEBUG("pref=%i\n", dscr->b0);
				rc = rtpg_tpg_dscr_get_aas(dscr);
			}
		}
	}
out:
	free(buf);
	return rc;
}

