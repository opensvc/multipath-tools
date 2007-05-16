/*
 * Path priority checker for HP active/standby controller
 *
 * Check the path state and sort them into groups.
 * There is actually a preferred path in the controller;
 * we should ask HP on how to retrieve that information.
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

#define TUR_CMD_LEN		6
#define SCSI_CHECK_CONDITION	0x2
#define SCSI_COMMAND_TERMINATED	0x22
#define SG_ERR_DRIVER_SENSE	0x08
#define RECOVERED_ERROR		0x01
#define NOT_READY		0x02
#define UNIT_ATTENTION		0x06

#define HP_PATH_ACTIVE		0x04
#define HP_PATH_STANDBY		0x02
#define HP_PATH_FAILED		0x00

#include "../../libmultipath/sg_include.h"

int hp_sw_prio(const char *dev)
{
        unsigned char turCmdBlk[TUR_CMD_LEN] = { 0x00, 0, 0, 0, 0, 0 };
	unsigned char sb[128];
	struct sg_io_hdr io_hdr;
	int ret = HP_PATH_FAILED;
	int fd;

	fd = open(dev, O_RDWR|O_NONBLOCK);

	if (fd <= 0) {
		fprintf(stderr, "Opening the device failed.\n");
		goto out;
	}

	memset(&io_hdr, 0, sizeof (struct sg_io_hdr));
	io_hdr.interface_id = 'S';
	io_hdr.cmd_len = sizeof (turCmdBlk);
	io_hdr.mx_sb_len = sizeof (sb);
	io_hdr.dxfer_direction = SG_DXFER_NONE;
	io_hdr.cmdp = turCmdBlk;
	io_hdr.sbp = sb;
	io_hdr.timeout = 60000;
	io_hdr.pack_id = 0;
 retry:
	if (ioctl(fd, SG_IO, &io_hdr) < 0) {
		fprintf(stderr, "sending tur command failed\n");
		goto out;
	}
        io_hdr.status &= 0x7e;
        if ((0 == io_hdr.status) && (0 == io_hdr.host_status) &&
            (0 == io_hdr.driver_status)) {
		/* Command completed normally, path is active */
                ret = HP_PATH_ACTIVE;
	}

        if ((SCSI_CHECK_CONDITION == io_hdr.status) ||
            (SCSI_COMMAND_TERMINATED == io_hdr.status) ||
            (SG_ERR_DRIVER_SENSE == (0xf & io_hdr.driver_status))) {
                if (io_hdr.sbp && (io_hdr.sb_len_wr > 2)) {
                        int sense_key, asc, asq;
                        unsigned char * sense_buffer = io_hdr.sbp;
                        if (sense_buffer[0] & 0x2) {
                                sense_key = sense_buffer[1] & 0xf;
				asc = sense_buffer[2];
				asq = sense_buffer[3];
			} else {
                                sense_key = sense_buffer[2] & 0xf;
				asc = sense_buffer[12];
				asq = sense_buffer[13];
			}
                        if(RECOVERED_ERROR == sense_key)
                                ret = HP_PATH_ACTIVE;
			if(NOT_READY == sense_key) {
				if (asc == 0x04 && asq == 0x02) {
					/* This is a standby path */
					ret = HP_PATH_STANDBY;
				}
			}
			if(UNIT_ATTENTION == sense_key) {
				if (asc == 0x29) {
					/* Retry for device reset */
					goto retry;
				}
			}
                }
        }

	close(fd);

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
		prio = hp_sw_prio(argv[1]);

	printf("%d\n", prio);
	exit(0);
}

