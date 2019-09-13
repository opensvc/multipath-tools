/*
 * Copyright (c) 2004, 2005 Lars Marowsky-Bree
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

#include "../libmultipath/sg_include.h"
#include "libsg.h"
#include "checkers.h"
#include "debug.h"
#include "memory.h"

#define INQUIRY_CMD     0x12
#define INQUIRY_CMDLEN  6
#define HEAVY_CHECK_COUNT       10
#define SCSI_COMMAND_TERMINATED	0x22
#define SCSI_CHECK_CONDITION	0x2
#define RECOVERED_ERROR		0x01
#define ILLEGAL_REQUEST		0x05
#define SG_ERR_DRIVER_SENSE	0x08

/*
 * Mechanism to track CLARiiON inactive snapshot LUs.
 * This is done so that we can fail passive paths
 * to an inactive snapshot LU even though since a
 * simple read test would return 02/04/03 instead
 * of 05/25/01 sensekey/ASC/ASCQ data.
 */
#define	IS_INACTIVE_SNAP(c)   (c->mpcontext ?				   \
			       ((struct emc_clariion_checker_LU_context *) \
					(*c->mpcontext))->inactive_snap	   \
					    : 0)

#define	SET_INACTIVE_SNAP(c)  if (c->mpcontext)				   \
				((struct emc_clariion_checker_LU_context *)\
					(*c->mpcontext))->inactive_snap = 1

#define	CLR_INACTIVE_SNAP(c)  if (c->mpcontext)				   \
				((struct emc_clariion_checker_LU_context *)\
					(*c->mpcontext))->inactive_snap = 0

enum {
	MSG_CLARIION_QUERY_FAILED = CHECKER_FIRST_MSGID,
	MSG_CLARIION_QUERY_ERROR,
	MSG_CLARIION_PATH_CONFIG,
	MSG_CLARIION_UNIT_REPORT,
	MSG_CLARIION_PATH_NOT_AVAIL,
	MSG_CLARIION_LUN_UNBOUND,
	MSG_CLARIION_WWN_CHANGED,
	MSG_CLARIION_READ_ERROR,
	MSG_CLARIION_PASSIVE_GOOD,
};

#define _IDX(x) (MSG_CLARIION_ ## x - CHECKER_FIRST_MSGID)
const char *libcheck_msgtable[] = {
	[_IDX(QUERY_FAILED)] = ": sending query command failed",
	[_IDX(QUERY_ERROR)] = ": query command indicates error",
	[_IDX(PATH_CONFIG)] =
	": Path not correctly configured for failover",
	[_IDX(UNIT_REPORT)] =
	": Path unit report page in unknown format",
	[_IDX(PATH_NOT_AVAIL)] =
	": Path not available for normal operations",
	[_IDX(LUN_UNBOUND)] = ": Logical Unit is unbound or LUNZ",
	[_IDX(WWN_CHANGED)] = ": Logical Unit WWN has changed",
	[_IDX(READ_ERROR)] = ": Read error",
	[_IDX(PASSIVE_GOOD)] = ": Active path is healthy",
	NULL,
};

struct emc_clariion_checker_path_context {
	char wwn[16];
	unsigned wwn_set;
};

struct emc_clariion_checker_LU_context {
	int inactive_snap;
};

void hexadecimal_to_ascii(char * wwn, char *wwnstr)
{
	int i,j, nbl;

	for (i=0,j=0;i<16;i++) {
		wwnstr[j++] = ((nbl = ((wwn[i]&0xf0) >> 4)) <= 9) ?
					'0' + nbl : 'a' + (nbl - 10);
		wwnstr[j++] = ((nbl = (wwn[i]&0x0f)) <= 9) ?
					'0' + nbl : 'a' + (nbl - 10);
	}
	wwnstr[32]=0;
}

int libcheck_init (struct checker * c)
{
	/*
	 * Allocate and initialize the path specific context.
	 */
	c->context = MALLOC(sizeof(struct emc_clariion_checker_path_context));
	if (!c->context)
		return 1;
	((struct emc_clariion_checker_path_context *)c->context)->wwn_set = 0;

	return 0;
}

int libcheck_mp_init (struct checker * c)
{
	/*
	 * Allocate and initialize the multi-path global context.
	 */
	if (c->mpcontext && *c->mpcontext == NULL) {
		void * mpctxt = malloc(sizeof(int));
		if (!mpctxt)
			return 1;
		*c->mpcontext = mpctxt;
		CLR_INACTIVE_SNAP(c);
	}

	return 0;
}

void libcheck_free (struct checker * c)
{
	free(c->context);
}

int libcheck_check (struct checker * c)
{
	unsigned char sense_buffer[128] = { 0, };
	unsigned char sb[SENSE_BUFF_LEN] = { 0, }, *sbb;
	unsigned char inqCmdBlk[INQUIRY_CMDLEN] = {INQUIRY_CMD, 1, 0xC0, 0,
						sizeof(sense_buffer), 0};
	struct sg_io_hdr io_hdr;
	struct emc_clariion_checker_path_context * ct =
		(struct emc_clariion_checker_path_context *)c->context;
	char wwnstr[33];
	int ret;
	int retry_emc = 5;

retry:
	memset(&io_hdr, 0, sizeof (struct sg_io_hdr));
	memset(sense_buffer, 0, 128);
	memset(sb, 0, SENSE_BUFF_LEN);
	io_hdr.interface_id = 'S';
	io_hdr.cmd_len = sizeof (inqCmdBlk);
	io_hdr.mx_sb_len = sizeof (sb);
	io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	io_hdr.dxfer_len = sizeof (sense_buffer);
	io_hdr.dxferp = sense_buffer;
	io_hdr.cmdp = inqCmdBlk;
	io_hdr.sbp = sb;
	io_hdr.timeout = c->timeout * 1000;
	io_hdr.pack_id = 0;
	if (ioctl(c->fd, SG_IO, &io_hdr) < 0) {
		if (errno == ENOTTY) {
			c->msgid = CHECKER_MSGID_UNSUPPORTED;
			return PATH_WILD;
		}
		c->msgid = MSG_CLARIION_QUERY_FAILED;
		return PATH_DOWN;
	}

	if (io_hdr.info & SG_INFO_OK_MASK) {
		switch (io_hdr.host_status) {
		case DID_BUS_BUSY:
		case DID_ERROR:
		case DID_SOFT_ERROR:
		case DID_TRANSPORT_DISRUPTED:
			/* Transport error, retry */
			if (--retry_emc)
				goto retry;
			break;
		default:
			break;
		}
	}

	if (SCSI_CHECK_CONDITION == io_hdr.status ||
	    SCSI_COMMAND_TERMINATED == io_hdr.status ||
	    SG_ERR_DRIVER_SENSE == (0xf & io_hdr.driver_status)) {
		if (io_hdr.sbp && (io_hdr.sb_len_wr > 2)) {
			unsigned char *sbp = io_hdr.sbp;
			int sense_key;

			if (sbp[0] & 0x2)
				sense_key = sbp[1] & 0xf;
			else
				sense_key = sbp[2] & 0xf;

			if (sense_key == ILLEGAL_REQUEST) {
				c->msgid = CHECKER_MSGID_UNSUPPORTED;
				return PATH_WILD;
			} else if (sense_key != RECOVERED_ERROR) {
				condlog(1, "emc_clariion_checker: INQUIRY failed with sense key %02x",
					sense_key);
				c->msgid = MSG_CLARIION_QUERY_ERROR;
				return PATH_DOWN;
			}
		}
	}

	if (io_hdr.info & SG_INFO_OK_MASK) {
		condlog(1, "emc_clariion_checker: INQUIRY failed without sense, status %02x",
			io_hdr.status);
		c->msgid = MSG_CLARIION_QUERY_ERROR;
		return PATH_DOWN;
	}

	if (/* Verify the code page - right page & revision */
	    sense_buffer[1] != 0xc0 || sense_buffer[9] != 0x00) {
		c->msgid = MSG_CLARIION_UNIT_REPORT;
		return PATH_DOWN;
	}

	if ( /* Effective initiator type */
		sense_buffer[27] != 0x03
		/*
		 * Failover mode should be set to 1 (PNR failover mode)
		 * or 4 (ALUA failover mode).
		 */
		|| (((sense_buffer[28] & 0x07) != 0x04) &&
		    ((sense_buffer[28] & 0x07) != 0x06))
		/* Arraycommpath should be set to 1 */
		|| (sense_buffer[30] & 0x04) != 0x04) {
		c->msgid = MSG_CLARIION_PATH_CONFIG;
		return PATH_DOWN;
	}

	if ( /* LUN operations should indicate normal operations */
		sense_buffer[48] != 0x00) {
		c->msgid = MSG_CLARIION_PATH_NOT_AVAIL;
		return PATH_SHAKY;
	}

	if ( /* LUN should at least be bound somewhere and not be LUNZ */
		sense_buffer[4] == 0x00) {
		c->msgid = MSG_CLARIION_LUN_UNBOUND;
		return PATH_DOWN;
	}

	/*
	 * store the LUN WWN there and compare that it indeed did not
	 * change in between, to protect against the path suddenly
	 * pointing somewhere else.
	 */
	if (ct->wwn_set) {
		if (memcmp(ct->wwn, &sense_buffer[10], 16) != 0) {
			c->msgid = MSG_CLARIION_WWN_CHANGED;
			return PATH_DOWN;
		}
	} else {
		memcpy(ct->wwn, &sense_buffer[10], 16);
		ct->wwn_set = 1;
	}

	/*
	 * Issue read on active path to determine if inactive snapshot.
	 */
	if (sense_buffer[4] == 2) {/* if active path */
		unsigned char buf[4096];

		memset(buf, 0, 4096);
		ret = sg_read(c->fd, &buf[0], 4096,
			      sbb = &sb[0], SENSE_BUFF_LEN, c->timeout);
		if (ret == PATH_DOWN) {
			hexadecimal_to_ascii(ct->wwn, wwnstr);

			/*
			 * Check for inactive snapshot LU this way.  Must
			 * fail these.
			 */
			if (((sbb[2]&0xf) == 5) && (sbb[12] == 0x25) &&
			    (sbb[13]==1)) {
				/*
				 * Do this so that we can fail even the
				 * passive paths which will return
				 * 02/04/03 not 05/25/01 on read.
				 */
				SET_INACTIVE_SNAP(c);
				condlog(3, "emc_clariion_checker: Active "
					"path to inactive snapshot WWN %s.",
					wwnstr);
			} else {
				condlog(3, "emc_clariion_checker: Read "
					"error for WWN %s.  Sense data are "
					"0x%x/0x%x/0x%x.", wwnstr,
					sbb[2]&0xf, sbb[12], sbb[13]);
				c->msgid = MSG_CLARIION_READ_ERROR;
			}
		} else {
			c->msgid = MSG_CLARIION_PASSIVE_GOOD;
			/*
			 * Remove the path from the set of paths to inactive
			 * snapshot LUs if it was in this list since the
			 * snapshot is no longer inactive.
			 */
			CLR_INACTIVE_SNAP(c);
		}
	} else {
		if (IS_INACTIVE_SNAP(c)) {
			hexadecimal_to_ascii(ct->wwn, wwnstr);
			condlog(3, "emc_clariion_checker: Passive "
				"path to inactive snapshot WWN %s.",
				wwnstr);
			ret = PATH_DOWN;
		} else {
			c->msgid = MSG_CLARIION_PASSIVE_GOOD;
			ret = PATH_UP;	/* not ghost */
		}
	}

	return ret;
}
