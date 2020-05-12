/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 */
#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/stat.h>

#include "checkers.h"
#include "libsg.h"
#include "../libmultipath/sg_include.h"

int
sg_read (int sg_fd, unsigned char * buff, int buff_len,
	 unsigned char * sense, int sense_len, unsigned int timeout)
{
	/* defaults */
	int blocks;
	long long start_block = 0;
	int bs = 512;
	int cdbsz = 10;

	unsigned char rdCmd[cdbsz];
	unsigned char *sbb = sense;
	struct sg_io_hdr io_hdr;
	int res;
	int rd_opcode[] = {0x8, 0x28, 0xa8, 0x88};
	int sz_ind;
	struct stat filestatus;
	int retry_count = 3;

	if (fstat(sg_fd, &filestatus) != 0)
		return PATH_DOWN;
	bs = (filestatus.st_blksize > 4096)? 4096: filestatus.st_blksize;
	blocks = buff_len / bs;
	memset(rdCmd, 0, cdbsz);
	sz_ind = 1;
	rdCmd[0] = rd_opcode[sz_ind];
	rdCmd[2] = (unsigned char)((start_block >> 24) & 0xff);
	rdCmd[3] = (unsigned char)((start_block >> 16) & 0xff);
	rdCmd[4] = (unsigned char)((start_block >> 8) & 0xff);
	rdCmd[5] = (unsigned char)(start_block & 0xff);
	rdCmd[7] = (unsigned char)((blocks >> 8) & 0xff);
	rdCmd[8] = (unsigned char)(blocks & 0xff);

	memset(&io_hdr, 0, sizeof(struct sg_io_hdr));
	io_hdr.interface_id = 'S';
	io_hdr.cmd_len = cdbsz;
	io_hdr.cmdp = rdCmd;
	io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	io_hdr.dxfer_len = bs * blocks;
	io_hdr.dxferp = buff;
	io_hdr.mx_sb_len = sense_len;
	io_hdr.sbp = sense;
	io_hdr.timeout = timeout * 1000;
	io_hdr.pack_id = (int)start_block;

retry:
	memset(sense, 0, sense_len);
	while (((res = ioctl(sg_fd, SG_IO, &io_hdr)) < 0) && (EINTR == errno));

	if (res < 0) {
		if (ENOMEM == errno) {
			return PATH_UP;
		}
		return PATH_DOWN;
	}

	if ((0 == io_hdr.status) &&
	    (0 == io_hdr.host_status) &&
	    (0 == io_hdr.driver_status)) {
		return PATH_UP;
	} else {
		int key = 0;

		if (io_hdr.sb_len_wr > 3) {
			if (sbb[0] == 0x72 || sbb[0] == 0x73)
				key = sbb[1] & 0x0f;
			else if (io_hdr.sb_len_wr > 13 &&
				 ((sbb[0] & 0x7f) == 0x70 ||
				  (sbb[0] & 0x7f) == 0x71))
				key = sbb[2] & 0x0f;
		}

		/*
		 * Retry if UNIT_ATTENTION check condition.
		 */
		if (key == 0x6) {
			if (--retry_count)
				goto retry;
		}
		return PATH_DOWN;
	}
}
