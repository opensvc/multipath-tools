#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

#include "sg_include.h"
#include "debug.h"
#include "prio.h"
#include "structs.h"

#define INQUIRY_CMD     0x12
#define INQUIRY_CMDLEN  6

#define pp_rdac_log(prio, msg) condlog(prio, "%s: rdac prio: " msg, dev)

int rdac_prio(const char *dev, int fd, unsigned int timeout)
{
	unsigned char sense_buffer[128];
	unsigned char sb[128];
	unsigned char inqCmdBlk[INQUIRY_CMDLEN] = {INQUIRY_CMD, 1, 0xC9, 0,
						sizeof(sense_buffer), 0};
	struct sg_io_hdr io_hdr;
	int ret = 0;

	memset(&io_hdr, 0, sizeof (struct sg_io_hdr));
	memset(sense_buffer, 0, 128);
	io_hdr.interface_id = 'S';
	io_hdr.cmd_len = sizeof (inqCmdBlk);
	io_hdr.mx_sb_len = sizeof (sb);
	io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	io_hdr.dxfer_len = sizeof (sense_buffer);
	io_hdr.dxferp = sense_buffer;
	io_hdr.cmdp = inqCmdBlk;
	io_hdr.sbp = sb;
	io_hdr.timeout = get_prio_timeout(timeout, 60000);
	io_hdr.pack_id = 0;
	if (ioctl(fd, SG_IO, &io_hdr) < 0) {
		pp_rdac_log(0, "sending inquiry command failed");
		goto out;
	}
	if (io_hdr.info & SG_INFO_OK_MASK) {
		pp_rdac_log(0, "inquiry command indicates error");
		goto out;
	}

	if (/* Verify the code page - right page & page identifier */
	    sense_buffer[1] != 0xc9 ||
	    sense_buffer[3] != 0x2c ||
	    sense_buffer[4] != 'v' ||
	    sense_buffer[5] != 'a' ||
	    sense_buffer[6] != 'c' ) {
		pp_rdac_log(0, "volume access control page in unknown format");
		goto out;
	}

	if ( /* Current Volume Path Bit */
		( sense_buffer[8] & 0x01) == 0x01 ) {
		/*
		 * This volume was owned by the controller receiving
		 * the inquiry command.
		 */
		ret |= 0x02;
	}

	/* Volume Preferred Path Priority */
	switch ( sense_buffer[9] & 0x0F ) {
	case 0x01:
		/*
		 * Access to this volume is most preferred through
		 * this path and other paths with this value.
		 */
		ret |= 0x04;
		break;
	case 0x02:
		/*
		 * Access to this volume through this path is to be used
		 * as a secondary path. Typically this path would be used
		 * for fail-over situations.
		 */
		ret |= 0x01;
		break;
	default:
		/* Reserved values */
		break;
	}

	/* For ioship mode set the bit 3 (00001000) */
	if ((sense_buffer[8] >> 5) & 0x01)
		ret |= 0x08;

out:
	return(ret);
}

int getprio (struct path * pp, char * args, unsigned int timeout)
{
	return rdac_prio(pp->dev, pp->fd, timeout);
}
