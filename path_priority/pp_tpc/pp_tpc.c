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

int sgi_tpc_prio(const char *dev)
{
	unsigned char sense_buffer[256];
	unsigned char sb[128];
	unsigned char inqCmdBlk[INQUIRY_CMDLEN] = {INQUIRY_CMD, 1, 0xC9, 0,
						sizeof(sb), 0};
	struct sg_io_hdr io_hdr;
	int ret = 0;
	int fd;

	fd = open(dev, O_RDWR|O_NONBLOCK);

	if (fd <= 0) {
		fprintf(stderr, "opening of the device failed.\n");
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
		fprintf(stderr, "sending inquiry command failed\n");
		goto out;
	}
	if (io_hdr.info & SG_INFO_OK_MASK) {
		fprintf(stderr, "inquiry command indicates error");
		goto out;
	}

	close(fd);
	
	if (/* Verify the code page - right page & page identifier */
	    sense_buffer[1] != 0xc9 || 
	    sense_buffer[3] != 0x2c ||
	    sense_buffer[4] != 'v' ||
	    sense_buffer[5] != 'a' ||
	    sense_buffer[6] != 'c' ) {
		fprintf(stderr, "Volume access control page in unknown format");
		goto out;
	}
	
	if ( /* Auto-volume Transfer Enabled */
	    	(sense_buffer[8] & 0x80) != 0x80 ) {
		fprintf(stderr, "Auto-volume Transfer not enabled");
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
	
out:
	return(ret);
}

int
main (int argc, char **argv)
{
	int prio;
	if (argc != 2) {
		fprintf(stderr, "Wrong number of arguments.\n");
		fprintf(stderr, "Usage: %s device\n", argv[0]);
		prio = 0;
	} else
		prio = sgi_tpc_prio(argv[1]);

	printf("%d\n", prio);
	exit(0);
}
