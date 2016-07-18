#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

#include "sg_include.h"
#include "debug.h"
#include "prio.h"
#include "structs.h"

#define INQUIRY_CMD     0x12
#define INQUIRY_CMDLEN  6

#define pp_emc_log(prio, msg) condlog(prio, "%s: emc prio: " msg, dev)

int emc_clariion_prio(const char *dev, int fd, unsigned int timeout)
{
	unsigned char sense_buffer[128];
	unsigned char sb[128];
	unsigned char inqCmdBlk[INQUIRY_CMDLEN] = {INQUIRY_CMD, 1, 0xC0, 0,
						sizeof(sense_buffer), 0};
	struct sg_io_hdr io_hdr;
	int ret = PRIO_UNDEF;

	memset(&io_hdr, 0, sizeof (struct sg_io_hdr));
	memset(&sense_buffer, 0, 128);
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
		pp_emc_log(0, "sending query command failed");
		goto out;
	}
	if (io_hdr.info & SG_INFO_OK_MASK) {
		pp_emc_log(0, "query command indicates error");
		goto out;
	}

	if (/* Verify the code page - right page & revision */
	    sense_buffer[1] != 0xc0 || sense_buffer[9] != 0x00) {
		pp_emc_log(0, "path unit report page in unknown format");
		goto out;
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
		pp_emc_log(0, "path not correctly configured for failover");
		goto out;
	}

	if ( /* LUN operations should indicate normal operations */
		sense_buffer[48] != 0x00) {
		pp_emc_log(0, "path not available for normal operations");
		goto out;
	}

	/* LUN state: unbound, bound, or owned */
	ret = sense_buffer[4];

	/* Is the default owner equal to this path? */
	/* Note this will switch to the default priority group, even if
	 * it is not the currently active one. */
	if (sense_buffer[5] == sense_buffer[8])
		ret+=2;

out:
	return(ret);
}

int getprio (struct path * pp, char * args, unsigned int timeout)
{
	return emc_clariion_prio(pp->dev, pp->fd, timeout);
}
