#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "../../libmultipath/sg_include.h"

#define INQUIRY_CMD     0x12
#define INQUIRY_CMDLEN  6

int emc_clariion_prio(const char *dev)
{
	unsigned char sense_buffer[256];
	unsigned char sb[128];
	unsigned char inqCmdBlk[INQUIRY_CMDLEN] = {INQUIRY_CMD, 1, 0xC0, 0,
						sizeof(sb), 0};
	struct sg_io_hdr io_hdr;
	int ret = 0;
	int fd;

	fd = open(dev, O_RDWR|O_NONBLOCK);

	if (fd <= 0) {
		fprintf(stderr, "Opening the device failed.\n");
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
		fprintf(stderr, "sending query command failed\n");
		goto out;
	}
	if (io_hdr.info & SG_INFO_OK_MASK) {
		fprintf(stderr, "query command indicates error");
		goto out;
	}

	close(fd);
	
	if (/* Verify the code page - right page & revision */
	    sense_buffer[1] != 0xc0 || sense_buffer[9] != 0x00) {
		fprintf(stderr, "Path unit report page in unknown format");
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
		fprintf(stderr, "Path not correctly configured for failover");
	}

	if ( /* LUN operations should indicate normal operations */
		sense_buffer[48] != 0x00) {
		fprintf(stderr, "Path not available for normal operations");
	}

	/* Is the default owner equal to this path? */
	/* Note this will switch to the default priority group, even if
	 * it is not the currently active one. */
	ret = (sense_buffer[5] == sense_buffer[8]) ? 1 : 0;
	
out:
	return(ret);
}

int
main (int argc, char **argv)
{
	int prio;
	if (argc != 2) {
		fprintf(stderr, "Arguments wrong!\n");
		prio = 0;
	} else
		prio = emc_clariion_prio(argv[1]);

	printf("%d\n", prio);
	exit(0);
}

