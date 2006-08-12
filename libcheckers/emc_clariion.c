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

#include "checkers.h"

#include "../libmultipath/sg_include.h"

#define INQUIRY_CMD     0x12
#define INQUIRY_CMDLEN  6
#define HEAVY_CHECK_COUNT       10

struct emc_clariion_checker_context {
	char wwn[16];
	unsigned wwn_set;
};

int emc_clariion_init (struct checker * c)
{
	c->context = malloc(sizeof(struct emc_clariion_checker_context));
	if (!c->context)
		return 1;
	((struct emc_clariion_checker_context *)c->context)->wwn_set = 0;
	return 0;
}

void emc_clariion_free (struct checker * c)
{
	free(c->context);
}

int emc_clariion(struct checker * c)
{
	unsigned char sense_buffer[256] = { 0, };
	unsigned char sb[128] = { 0, };
	unsigned char inqCmdBlk[INQUIRY_CMDLEN] = {INQUIRY_CMD, 1, 0xC0, 0,
						sizeof(sb), 0};
	struct sg_io_hdr io_hdr;
	struct emc_clariion_checker_context * ct =
		(struct emc_clariion_checker_context *)c->context;

	memset(&io_hdr, 0, sizeof (struct sg_io_hdr));
	io_hdr.interface_id = 'S';
	io_hdr.cmd_len = sizeof (inqCmdBlk);
	io_hdr.mx_sb_len = sizeof (sb);
	io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	io_hdr.dxfer_len = sizeof (sense_buffer);
	io_hdr.dxferp = sense_buffer;
	io_hdr.cmdp = inqCmdBlk;
	io_hdr.sbp = sb;
	io_hdr.timeout = DEF_TIMEOUT;
	io_hdr.pack_id = 0;
	if (ioctl(c->fd, SG_IO, &io_hdr) < 0) {
		MSG(c, "emc_clariion_checker: sending query command failed");
		return PATH_DOWN;
	}
	if (io_hdr.info & SG_INFO_OK_MASK) {
		MSG(c, "emc_clariion_checker: query command indicates error");
		return PATH_DOWN;
	}
	if (/* Verify the code page - right page & revision */
	    sense_buffer[1] != 0xc0 || sense_buffer[9] != 0x00) {
		MSG(c, "emc_clariion_checker: Path unit report page in unknown format");
		return PATH_DOWN;
	}

	if ( /* Effective initiator type */
	    	sense_buffer[27] != 0x03
		/* Failover mode should be set to 1 */        
		|| (sense_buffer[28] & 0x07) != 0x04
		/* Arraycommpath should be set to 1 */
		|| (sense_buffer[30] & 0x04) != 0x04) {
		MSG(c, "emc_clariion_checker: Path not correctly configured for failover");
		return PATH_DOWN;
	}

	if ( /* LUN operations should indicate normal operations */
		sense_buffer[48] != 0x00) {
		MSG(c, "emc_clariion_checker: Path not available for normal operations");
		return PATH_SHAKY;
	}

	if ( /* LUN should at least be bound somewhere and not be LUNZ */
		sense_buffer[4] == 0x00) {
		MSG(c, "emc_clariion_checker: Logical Unit is unbound or LUNZ");
		return PATH_DOWN;
	}
	
	/*
	 * store the LUN WWN there and compare that it indeed did not
	 * change in between, to protect against the path suddenly
	 * pointing somewhere else.
	 */
	if (ct->wwn_set) {
		if (memcmp(ct->wwn, &sense_buffer[10], 16) != 0) {
			MSG(c, "emc_clariion_checker: Logical Unit WWN has changed!");
			return PATH_DOWN;
		}
	} else {
		memcpy(ct->wwn, &sense_buffer[10], 16);
		ct->wwn_set = 1;
	}
	
	
	MSG(c, "emc_clariion_checker: Path healthy");
        return PATH_UP;
}
