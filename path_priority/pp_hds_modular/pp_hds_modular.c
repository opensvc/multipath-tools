/*
 * (C) Copyright HDS GmbH 2006. All Rights Reserved.
 *
 * pp_hds_modular.c
 * Version 1.12
 *
 * Prioritizer for multipath tools device mapper and HDS Storage
 *
 * Hitachis Modular Storage contains two controllers for redundancy. The
 * Storage internal LUN (LDEV) will normally allocated via two pathes to the
 * server (one path per controller). For performance reasons should the server
 * access to a LDEV only via one controller. The other path to the other
 * controller is stand-by. It is also possible to allocate more as one path
 * for a LDEV per controller. Here is active/active access allowed. The other
 * pathes via the other controller are stand-by.
 * 
 * This prioritizer checks with inquiry commands the represented LDEV and
 * Controller number and gives back a priority followed by this scheme :
 *
 * CONTROLLER ODD  and LDEV  ODD: PRIORITY 1
 * CONTROLLER ODD  and LDEV EVEN: PRIORITY 0
 * CONTROLLER EVEN and LDEV  ODD: PRIORITY 0
 * CONTROLLER EVEN and LDEV EVEN: PRIORITY 1
 *
 * In the storage you can define for each LDEV a owner controller. If the
 * server makes IOs via the other controller the storage will switch the
 * ownership automatically. In this case you can see in the storage that the
 * current controller is different from the default controller, but this is
 * absolutely no problem.
 *
 * With this prioritizer it is possible to establish a static load balancing.
 * Half of the LUNs are accessed via one HBA/storage controller and the other
 * half via the other HBA/storage controller.
 *
 * In cluster environmemnts (RAC) it also guarantees that all cluster nodes
 * have access to the LDEVs via the same controller.
 * 
 * You can run the prioritizer manually in verbose mode :
 * # pp_hds_modular -v 8:224
 * VENDOR:  HITACHI
 * PRODUCT: DF600F-CM
 * SERIAL:  0x0105
 * LDEV:    0x00C6
 * CTRL:    1
 * PORT:    B
 * CTRL ODD, LDEV EVEN, PRIO 0
 *
 * The items VENDOR and PRODUCT helps you to make the correct entries in file
 * /etc/multipath.conf :
 * # cat /etc/multipath.conf
 * ...
 * devices {
 *        device {
 *                vendor                  "HITACHI"
 *                product                 "DF600F"
 *                path_grouping_policy    group_by_prio
 *                prio_callout            "/sbin/pp_hds_modular %d"
 *                path_checker            readsector0
 *                getuid_callout          "/sbin/scsi_id -g -u -s /block/%n"
 *                failback                immediate
 *        }
 *        device {
 *                vendor                  "HITACHI"
 *                product                 "DF600F-CM"
 *                path_grouping_policy    group_by_prio
 *                prio_callout            "/sbin/pp_hds_modular %d"
 *                path_checker            readsector0
 *                getuid_callout          "/sbin/scsi_id -g -u -s /block/%n"
 *                failback                immediate
 *
 *
 * Author: Matthias Rudolph <matthias.rudolph@hds.com>
 *
 * This file is released under the GPL.
 *
 */

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <libdevmapper.h>
#include <memory.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <scsi/sg.h> /* take care: fetches glibc's /usr/include/scsi/sg.h */

#define INQ_REPLY_LEN 255
#define INQ_CMD_CODE 0x12
#define INQ_CMD_LEN 6
#define FILE_NAME_SIZE 255
#define safe_sprintf(var, format, args...)	\
	snprintf(var, sizeof(var), format, ##args) >= sizeof(var)
#define safe_snprintf(var, size, format, args...)      \
	snprintf(var, size, format, ##args) >= size

int verbose;

int hds_modular_prio(char * major_minor)
{
	int sg_fd, k, i;
	char vendor[32];
	char product[32];
	char serial[32];
	char ldev[32];
	char ctrl[32];
	char port[32];
	char devpath[FILE_NAME_SIZE];
	unsigned int major;
	unsigned int minor;
	unsigned char inqCmdBlk[INQ_CMD_LEN] = 
		{INQ_CMD_CODE, 0, 0, 0, INQ_REPLY_LEN, 0};
	unsigned char inqBuff[INQ_REPLY_LEN];
	unsigned char sense_buffer[32];
	sg_io_hdr_t io_hdr;

	sscanf(major_minor, "%u:%u", &major, &minor);
	memset(devpath, 0, FILE_NAME_SIZE);

	if (safe_sprintf(devpath, "/tmp/.pp_balance.%u.%u.devnode",
			 major, minor))
		exit(1);

	unlink (devpath);
	mknod(devpath, S_IFBLK|S_IRUSR|S_IWUSR, makedev(major, minor));

	if ((sg_fd = open(devpath, O_RDONLY)) < 0) exit(1);
	if ((ioctl(sg_fd, SG_GET_VERSION_NUM, &k) < 0) || (k < 30000))
		exit(1);

	memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
	io_hdr.interface_id = 'S';
	io_hdr.cmd_len = sizeof(inqCmdBlk);
	io_hdr.mx_sb_len = sizeof(sense_buffer);
	io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	io_hdr.dxfer_len = INQ_REPLY_LEN;
	io_hdr.dxferp = inqBuff;
	io_hdr.cmdp = inqCmdBlk;
	io_hdr.sbp = sense_buffer;
	io_hdr.timeout = 2000;     /* TimeOut = 2 seconds */

        if (ioctl(sg_fd, SG_IO, &io_hdr) < 0) exit(1);
	if ((io_hdr.info & SG_INFO_OK_MASK) != SG_INFO_OK) exit(1);

	for (i = 0; i <  8 ; i++) vendor[i]  = inqBuff[i+8];
	vendor[8] = 0;
	for (i = 0; i < 16 ; i++) product[i] = inqBuff[i+16];
	product[16] = 0;
	for (i = 0; i <  4 ; i++) serial[i]  = inqBuff[i+40];
	serial[4] = 0;
	for (i = 0; i <  4 ; i++) ldev[i]    = inqBuff[i+44];
	ldev[4] = 0;
	ctrl[0] = inqBuff[49];
	ctrl[1] = 0;
	port[0] = inqBuff[50];
	port[1] = 0;

	close(sg_fd);

	if (1 == verbose) {
		printf("VENDOR:  %s\n", vendor);
		printf("PRODUCT: %s\n", product);
		printf("SERIAL:  0x%s\n", serial);
		printf("LDEV:    0x%s\n", ldev);
		printf("CTRL:    %s\n", ctrl);
		printf("PORT:    %s\n", port);
	}
	switch( ctrl[0] ) {
		case '0': case '2': case '4': case '6': case '8':
			switch( ldev[3] ) {
				case '0': case '2': case '4': case '6':
				case '8': case 'A': case 'C': case 'E':
					if (1 == verbose)
						printf("CTRL EVEN, LDEV EVEN, "
						       "PRIO 1\n");
					return 1;
					break;
				case '1': case '3': case '5': case '7':
				case '9': case 'B': case 'D': case 'F':
					if (1 == verbose)
						printf("CTRL EVEN, LDEV ODD, "
						       "PRIO 0\n");
					return 0;
					break;

			}
		case '1': case '3': case '5': case '7': case '9':
			switch( ldev[3] ) {
				case '0': case '2': case '4': case '6':
				case '8': case 'A': case 'C': case 'E':
					if (1 == verbose)
						printf("CTRL ODD, LDEV EVEN, "
						       "PRIO 0\n");
					return 0;
					break;
				case '1': case '3': case '5': case '7':
				case '9': case 'B': case 'D': case 'F':
					if (1 == verbose)
						printf("CTRL ODD, LDEV ODD, "
						       "PRIO 1\n");
					return 1;
					break;
			}
	}
	exit(1);
}

void print_help(void)
{
	printf("Usage:       "
			"pp_hds_modular [-v] <device_major:device_minor>\n");
	printf("Option:      "
			"-v verbose mode\n");
	printf("Description: "
			"Prioritizer for Multipath Tools and HDS Storage\n");
	printf("Version:     "
			"1.12\n");
	printf("Author:      "
			"Matthias Rudolph <matthias.rudolph@hds.com>\n");
	return;
}

int main(int argc, char * argv[])
{
	int prio;

	if (2 == argc) {
		if (0 == strcmp(argv[1], "-h")) {
			print_help();
			exit(0);
		}
		else {
			verbose = 0;
			prio = hds_modular_prio(argv[1]);
			printf("%d\n", prio);
			exit(0);
		}
	}

	if ((3 == argc) && (0 == strcmp(argv[1], "-v"))) {
		verbose = 1;
		prio = hds_modular_prio(argv[2]);
		printf("%d\n", prio);
		exit(0);
	}
	print_help();
	exit(1);
}

