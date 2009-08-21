/*
 * Copyright (c) 2005 Christophe Varoqui
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "checkers.h"

#include "../libmultipath/sg_include.h"

#define INQUIRY_CMDLEN		6
#define INQUIRY_CMD		0x12
#define SENSE_BUFF_LEN		32
#define RDAC_DEF_TIMEOUT	60000
#define SCSI_CHECK_CONDITION	0x2
#define SCSI_COMMAND_TERMINATED	0x22
#define SG_ERR_DRIVER_SENSE	0x08
#define RECOVERED_ERROR		0x01

#define MSG_RDAC_UP    "rdac checker reports path is up"
#define MSG_RDAC_DOWN  "rdac checker reports path is down"
#define MSG_RDAC_GHOST "rdac checker reports path is ghost"

struct rdac_checker_context {
	void * dummy;
};

int libcheck_init (struct checker * c)
{
	return 0;
}

void libcheck_free (struct checker * c)
{
	return;
}

static int
do_inq(int sg_fd, unsigned int pg_op, void *resp, int mx_resp_len)
{
	unsigned char inqCmdBlk[INQUIRY_CMDLEN] = { INQUIRY_CMD, 1, 0, 0, 0, 0 };
	unsigned char sense_b[SENSE_BUFF_LEN];
	struct sg_io_hdr io_hdr;

	inqCmdBlk[2] = (unsigned char) pg_op;
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
	io_hdr.timeout = RDAC_DEF_TIMEOUT;

	if (ioctl(sg_fd, SG_IO, &io_hdr) < 0)
		return 1;

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
			if (RECOVERED_ERROR == sense_key)
				return 0;
		}
	}
	return 1;
}

struct volume_access_inq
{
	char dontcare0[8];
	char avtcvp;
	char dontcare1[39];
};

extern int
libcheck_check (struct checker * c)
{
	struct volume_access_inq inq;

	memset(&inq, 0, sizeof(struct volume_access_inq));
	if (0 != do_inq(c->fd, 0xC9, &inq, sizeof(struct volume_access_inq))) {
		MSG(c, MSG_RDAC_DOWN);
		return PATH_DOWN;
	}

	if (inq.avtcvp & 0x1) {
		MSG(c, MSG_RDAC_UP);
		return PATH_UP;
	}
	else {
		MSG(c, MSG_RDAC_GHOST);
		return PATH_GHOST;
	}
}
