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
#include "debug.h"

#include "../libmultipath/sg_include.h"

#define INQUIRY_CMDLEN		6
#define INQUIRY_CMD		0x12
#define MODE_SENSE_CMD		0x5a
#define MODE_SELECT_CMD		0x55
#define MODE_SEN_SEL_CMDLEN	10
#define SENSE_BUFF_LEN		32
#define SCSI_CHECK_CONDITION	0x2
#define SCSI_COMMAND_TERMINATED	0x22
#define SG_ERR_DRIVER_SENSE	0x08
#define RECOVERED_ERROR		0x01
#define ILLEGAL_REQUEST		0x05


#define CURRENT_PAGE_CODE_VALUES	0
#define CHANGEABLE_PAGE_CODE_VALUES	1

#define MSG_RDAC_DOWN  " reports path is down"
#define MSG_RDAC_DOWN_TYPE(STR) MSG_RDAC_DOWN": "STR

#define RTPG_UNAVAILABLE	0x3
#define RTPG_OFFLINE		0xE
#define RTPG_TRANSITIONING	0xF

#define RTPG_UNAVAIL_NON_RESPONSIVE	0x2
#define RTPG_UNAVAIL_IN_RESET		0x3
#define RTPG_UNAVAIL_CFW_DL1		0x4
#define RTPG_UNAVAIL_CFW_DL2		0x5
#define RTPG_UNAVAIL_QUIESCED		0x6
#define RTPG_UNAVAIL_SERVICE_MODE	0x7

struct control_mode_page {
	unsigned char header[8];
	unsigned char page_code;
	unsigned char page_len;
	unsigned char dontcare0[3];
	unsigned char tas_bit;
	unsigned char dontcare1[6];
};

struct rdac_checker_context {
	void * dummy;
};

int libcheck_init (struct checker * c)
{
	unsigned char cmd[MODE_SEN_SEL_CMDLEN];
	unsigned char sense_b[SENSE_BUFF_LEN];
	struct sg_io_hdr io_hdr;
	struct control_mode_page current, changeable;
	int set = 0;

	memset(cmd, 0, MODE_SEN_SEL_CMDLEN);
	cmd[0] = MODE_SENSE_CMD;
	cmd[1] = 0x08; /* DBD bit on */
	cmd[2] = 0xA + (CURRENT_PAGE_CODE_VALUES << 6);
	cmd[8] = (sizeof(struct control_mode_page) &  0xff);

	memset(&io_hdr, 0, sizeof(struct sg_io_hdr));
	memset(sense_b, 0, SENSE_BUFF_LEN);
	memset(&current, 0, sizeof(struct control_mode_page));

	io_hdr.interface_id = 'S';
	io_hdr.cmd_len = MODE_SEN_SEL_CMDLEN;
	io_hdr.mx_sb_len = sizeof(sense_b);
	io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	io_hdr.dxfer_len = (sizeof(struct control_mode_page) &  0xff);
	io_hdr.dxferp = &current;
	io_hdr.cmdp = cmd;
	io_hdr.sbp = sense_b;
	io_hdr.timeout = c->timeout * 1000;

	if (ioctl(c->fd, SG_IO, &io_hdr) < 0)
		goto out;

	/* check the TAS bit to see if it is already set */
	if ((current.tas_bit >> 6) & 0x1) {
		set = 1;
		goto out;
	}

	/* get the changeble values */
	cmd[2] = 0xA + (CHANGEABLE_PAGE_CODE_VALUES << 6);
	io_hdr.dxferp = &changeable;
	memset(&changeable, 0, sizeof(struct control_mode_page));

	if (ioctl(c->fd, SG_IO, &io_hdr) < 0)
		goto out;

	/* if TAS bit is not settable exit */
	if (((changeable.tas_bit >> 6) & 0x1) == 0)
		goto out;

	/* Now go ahead and set it */
	memset(cmd, 0, MODE_SEN_SEL_CMDLEN);
	cmd[0] = MODE_SELECT_CMD;
	cmd[1] = 0x1; /* set SP bit on */
	cmd[8] = (sizeof(struct control_mode_page) &  0xff);

	/* use the same buffer as current, only set the tas bit */
	current.page_code = 0xA;
	current.page_len = 0xA;
	current.tas_bit |= (1 << 6);

	io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
	io_hdr.dxferp = &current;

	if (ioctl(c->fd, SG_IO, &io_hdr) < 0)
		goto out;

	/* Success */
	set = 1;
out:
	if (set == 0)
		condlog(3, "rdac checker failed to set TAS bit");
	return 0;
}

void libcheck_free (struct checker * c)
{
	return;
}

static int
do_inq(int sg_fd, unsigned int pg_op, void *resp, int mx_resp_len,
       unsigned int timeout)
{
	unsigned char inqCmdBlk[INQUIRY_CMDLEN] = { INQUIRY_CMD, 1, 0, 0, 0, 0 };
	unsigned char sense_b[SENSE_BUFF_LEN];
	struct sg_io_hdr io_hdr;
	int retry_rdac = 5;

retry:
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
	io_hdr.timeout = timeout * 1000;

	if (ioctl(sg_fd, SG_IO, &io_hdr) < 0 && errno == ENOTTY)
		return PATH_WILD;

	/* treat SG_ERR here to get rid of sg_err.[ch] */
	io_hdr.status &= 0x7e;
	if ((0 == io_hdr.status) && (0 == io_hdr.host_status) &&
	    (0 == io_hdr.driver_status))
		return PATH_UP;

	/* check if we need to retry this error */
	if (io_hdr.info & SG_INFO_OK_MASK) {
		switch (io_hdr.host_status) {
		case DID_BUS_BUSY:
		case DID_ERROR:
		case DID_SOFT_ERROR:
		case DID_TRANSPORT_DISRUPTED:
			/* Transport error, retry */
			if (--retry_rdac)
				goto retry;
			break;
		default:
			break;
		}
	}

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
				return PATH_UP;
			else if (ILLEGAL_REQUEST == sense_key)
				return PATH_WILD;
			condlog(1, "rdac checker: INQUIRY failed with sense key %02x",
				sense_key);
		}
	}
	return PATH_DOWN;
}

struct volume_access_inq
{
	char PQ_PDT;
	char dontcare0[7];
	char avtcvp;
	char vol_ppp;
	char aas_cur;
	char vendor_specific_cur;
	char aas_alt;
	char vendor_specific_alt;
	char dontcare1[34];
};

enum {
	RDAC_MSGID_NOT_CONN = CHECKER_FIRST_MSGID,
	RDAC_MSGID_IN_STARTUP,
	RDAC_MSGID_NON_RESPONSIVE,
	RDAC_MSGID_IN_RESET,
	RDAC_MSGID_FW_DOWNLOADING,
	RDAC_MSGID_QUIESCED,
	RDAC_MSGID_SERVICE_MODE,
	RDAC_MSGID_UNAVAILABLE,
	RDAC_MSGID_INQUIRY_FAILED,
};

#define _IDX(x) (RDAC_MSGID_##x - CHECKER_FIRST_MSGID)
const char *libcheck_msgtable[] = {
	[_IDX(NOT_CONN)] = MSG_RDAC_DOWN_TYPE("lun not connected"),
	[_IDX(IN_STARTUP)] = MSG_RDAC_DOWN_TYPE("ctlr is in startup sequence"),
	[_IDX(NON_RESPONSIVE)] =
	MSG_RDAC_DOWN_TYPE("non-responsive to queries"),
	[_IDX(IN_RESET)] = MSG_RDAC_DOWN_TYPE("ctlr held in reset"),
	[_IDX(FW_DOWNLOADING)] =
	MSG_RDAC_DOWN_TYPE("ctlr firmware downloading"),
	[_IDX(QUIESCED)] = MSG_RDAC_DOWN_TYPE("ctlr quiesced by admin request"),
	[_IDX(SERVICE_MODE)] = MSG_RDAC_DOWN_TYPE("ctlr is in service mode"),
	[_IDX(UNAVAILABLE)] = MSG_RDAC_DOWN_TYPE("ctlr is unavailable"),
	[_IDX(INQUIRY_FAILED)] = MSG_RDAC_DOWN_TYPE("inquiry failed"),
	NULL,
};

static int
checker_msg_string(const struct volume_access_inq *inq)
{
	/* lun not connected */
	if (((inq->PQ_PDT & 0xE0) == 0x20) || (inq->PQ_PDT & 0x7f))
		return RDAC_MSGID_NOT_CONN;

	/* if no tpg data is available, give the generic path down message */
	if (!(inq->avtcvp & 0x10))
		return CHECKER_MSGID_DOWN;

	/* controller is booting up */
	if (((inq->aas_cur & 0x0F) == RTPG_TRANSITIONING) &&
		(inq->aas_alt & 0x0F) != RTPG_TRANSITIONING)
		return RDAC_MSGID_IN_STARTUP;

	/* if not unavailable, give generic message */
	if ((inq->aas_cur & 0x0F) != RTPG_UNAVAILABLE)
		return CHECKER_MSGID_DOWN;

	/* target port group unavailable */
	switch (inq->vendor_specific_cur) {
	case RTPG_UNAVAIL_NON_RESPONSIVE:
		return RDAC_MSGID_NON_RESPONSIVE;
	case RTPG_UNAVAIL_IN_RESET:
		return RDAC_MSGID_IN_RESET;
	case RTPG_UNAVAIL_CFW_DL1:
	case RTPG_UNAVAIL_CFW_DL2:
		return RDAC_MSGID_FW_DOWNLOADING;
	case RTPG_UNAVAIL_QUIESCED:
		return RDAC_MSGID_QUIESCED;
	case RTPG_UNAVAIL_SERVICE_MODE:
		return RDAC_MSGID_SERVICE_MODE;
	default:
		return RDAC_MSGID_UNAVAILABLE;
	}
}

int libcheck_check(struct checker * c)
{
	struct volume_access_inq inq;
	int ret, inqfail;

	inqfail = 0;
	memset(&inq, 0, sizeof(struct volume_access_inq));
	ret = do_inq(c->fd, 0xC9, &inq, sizeof(struct volume_access_inq),
		     c->timeout);
	if (ret != PATH_UP) {
		inqfail = 1;
		goto done;
	}

	if (((inq.PQ_PDT & 0xE0) == 0x20) || (inq.PQ_PDT & 0x7f)) {
		/* LUN not connected*/
		ret = PATH_DOWN;
		goto done;
	}

	/* If TPGDE bit set, evaluate TPG information */
	if ((inq.avtcvp & 0x10)) {
		switch (inq.aas_cur & 0x0F) {
		/* Never use the path if it reports unavailable */
		case RTPG_UNAVAILABLE:
			ret = PATH_DOWN;
			goto done;
		/*
		 * If both controllers report transitioning, it
		 * means mode select or STPG is being processed.
		 *
		 * If this controller alone is transitioning, it's
		 * booting and we shouldn't use it yet.
		 */
		case RTPG_TRANSITIONING:
			if ((inq.aas_alt & 0xF) != RTPG_TRANSITIONING) {
				ret = PATH_DOWN;
				goto done;
			}
			break;
		}
	}

	/* If owner set or ioship mode is enabled return PATH_UP always */
	if ((inq.avtcvp & 0x1) || ((inq.avtcvp >> 5) & 0x1))
		ret = PATH_UP;
	else
		ret = PATH_GHOST;

done:
	switch (ret) {
	case PATH_WILD:
		c->msgid = CHECKER_MSGID_UNSUPPORTED;
		break;
	case PATH_DOWN:
		c->msgid = (inqfail ? RDAC_MSGID_INQUIRY_FAILED :
			    checker_msg_string(&inq));
		break;
	case PATH_UP:
		c->msgid = CHECKER_MSGID_UP;
		break;
	case PATH_GHOST:
		c->msgid = CHECKER_MSGID_GHOST;
		break;
	}

	return ret;
}
