#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "path_state.h"
#include "checkers.h"

#include "../libmultipath/sg_include.h"

#define INQUIRY_CMD     0x12
#define INQUIRY_CMDLEN  6
#define HEAVY_CHECK_COUNT       10

struct emc_clariion_checker_context {
	int run_count;
	char wwn[16];
	unsigned wwn_set;
};

int emc_clariion(int fd, char *msg, void **context)
{
	unsigned char sense_buffer[256] = { 0, };
	unsigned char sb[128] = { 0, };
	unsigned char inqCmdBlk[INQUIRY_CMDLEN] = {INQUIRY_CMD, 1, 0xC0, 0,
						sizeof(sb), 0};
	struct sg_io_hdr io_hdr;
	struct emc_clariion_checker_context * ctxt = NULL;
	int ret;

	/*
	 * caller passed in a context : use its address
	 */
	if (context)
		ctxt = (struct emc_clariion_checker_context *) (*context);

	/*
	 * passed in context is uninitialized or volatile context :
	 * initialize it
	 */
	if (!ctxt) {
		ctxt = malloc(sizeof(struct emc_clariion_checker_context));
		memset(ctxt, 0, sizeof(struct emc_clariion_checker_context));

		if (!ctxt) {
			MSG("cannot allocate context");
			return -1;
		}
		if (context)
			*context = ctxt;
	}
	ctxt->run_count++;

	if ((ctxt->run_count % HEAVY_CHECK_COUNT) == 0) {
		ctxt->run_count = 0;
		/* do stuff */
	}

	if (fd <= 0) {
		MSG("no usable fd");
		ret = -1;
		goto out;
	}
	memset(&io_hdr, 0, sizeof (struct sg_io_hdr));
	io_hdr.interface_id = 'S';
	io_hdr.cmd_len = sizeof (inqCmdBlk);
	io_hdr.mx_sb_len = sizeof (sb);
	io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	io_hdr.dxfer_len = sizeof (sense_buffer);
	io_hdr.dxferp = sense_buffer;
	io_hdr.cmdp = inqCmdBlk;
	io_hdr.sbp = sb;
	io_hdr.timeout = 60000;
	io_hdr.pack_id = 0;
	if (ioctl(fd, SG_IO, &io_hdr) < 0) {
		MSG("emc_clariion_checker: sending query command failed");
		ret = PATH_DOWN;
		goto out;
	}
	if (io_hdr.info & SG_INFO_OK_MASK) {
		MSG("emc_clariion_checker: query command indicates error");
		ret = PATH_DOWN;
		goto out;
	}
	if (/* Verify the code page - right page & revision */
	    sense_buffer[1] != 0xc0 || sense_buffer[9] != 0x00) {
		MSG("emc_clariion_checker: Path unit report page in unknown format");
		ret = PATH_DOWN;
		goto out;
	}

	if ( /* Effective initiator type */
	    	sense_buffer[27] != 0x03
		/* Failover mode should be set to 1 */        
		|| (sense_buffer[28] & 0x07) != 0x04
		/* Arraycommpath should be set to 1 */
		|| (sense_buffer[30] & 0x04) != 0x04) {
		MSG("emc_clariion_checker: Path not correctly configured for failover");
		ret = PATH_DOWN;
		goto out;
	}

	if ( /* LUN operations should indicate normal operations */
		sense_buffer[48] != 0x00) {
		MSG("emc_clariion_checker: Path not available for normal operations");
		ret = PATH_SHAKY;
		goto out;
	}

#if 0
	/* This is not actually an error as the failover to this group
	 * _would_ bind the path */
	if ( /* LUN should at least be bound somewhere */
		sense_buffer[4] != 0x00) {
		ret = PATH_UP;
		goto out;
	}
#endif	
	
	/*
	 * store the LUN WWN there and compare that it indeed did not
	 * change in between, to protect against the path suddenly
	 * pointing somewhere else.
	 */
	if (context && ctxt->wwn_set) {
		if (memcmp(ctxt->wwn, &sense_buffer[10], 16) != 0) {
			MSG("emc_clariion_checker: Logical Unit WWN has changed!");
			ret = PATH_DOWN;
			goto out;
		}
	} else {
		memcpy(ctxt->wwn, &sense_buffer[10], 16);
		ctxt->wwn_set = 1;
	}
	
	
	MSG("emc_clariion_checker: Path healthy");
        ret = PATH_UP;
out:
	/*
	 * caller told us he doesn't want to keep the context :
	 * free it
	 */
	if (!context)
		free(ctxt);

	return(ret);
}
