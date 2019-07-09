/*
 * dasd.c
 *
 * IBM DASD partition table handling.
 *
 * Mostly taken from drivers/s390/block/dasd.c
 *
 * Copyright (c) 2005, Hannes Reinecke, SUSE Linux Products GmbH
 * Copyright IBM Corporation, 2009
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/ioctl.h>
#include <linux/hdreg.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <libdevmapper.h>
#include "devmapper.h"
#include "kpartx.h"
#include "byteorder.h"
#include "dasd.h"

unsigned long long sectors512(unsigned long long sectors, int blocksize)
{
	return sectors * (blocksize >> 9);
}

/*
 * Magic records per track calculation, copied from fdasd.c
 */
static unsigned int ceil_quot(unsigned int d1, unsigned int d2)
{
	return (d1 + (d2 - 1)) / d2;
}

unsigned int recs_per_track(unsigned int dl)
{
	int dn = ceil_quot(dl + 6, 232) + 1;
	return 1729 / (10 + 9 + ceil_quot(dl + 6 * dn, 34));
}


typedef unsigned int __attribute__((__may_alias__)) label_ints_t;

/*
 */
int
read_dasd_pt(int fd, struct slice all, struct slice *sp, int ns)
{
	int retval = -1;
	int blocksize;
	uint64_t disksize;
	uint64_t offset, size, fmt_size;
	dasd_information_t info;
	struct hd_geometry geo;
	char type[5] = {0,};
	volume_label_t vlabel;
	unsigned char *data = NULL;
	uint64_t blk;
	int fd_dasd = -1;
	struct stat sbuf;
	dev_t dev;
	char *devname;
	char pathname[256];

	if (fd < 0) {
		return -1;
	}

	if (fstat(fd, &sbuf) == -1) {
		return -1;
	}

	devname = dm_mapname(major(sbuf.st_rdev), minor(sbuf.st_rdev));

	if (devname != NULL) {
		/* We were passed a handle to a dm device.
		 * Get the first target and operate on that instead.
		 */
		if (!(dev = dm_get_first_dep(devname))) {
			free(devname);
			return -1;
		}
		free(devname);

		if ((unsigned int)major(dev) != 94) {
			/* Not a DASD */
			return -1;
		}

		/*
		 * Hard to believe, but there's no simple way to translate
		 * major/minor into an openable device file, so we have
		 * to create one for ourselves.
		 */

		sprintf(pathname, "/dev/.kpartx-node-%u-%u",
			(unsigned int)major(dev), (unsigned int)minor(dev));
		if ((fd_dasd = open(pathname, O_RDONLY)) == -1) {
			/* Devicenode does not exist. Try to create one */
			if (mknod(pathname, 0600 | S_IFBLK, dev) == -1) {
				/* Couldn't create a device node */
				return -1;
			}
			fd_dasd = open(pathname, O_RDONLY);
			/*
			 * The file will vanish when the last process (we)
			 * has ceased to access it.
			 */
			unlink(pathname);
		}
		if (fd_dasd < 0) {
			/* Couldn't open the device */
			return -1;
		}
	} else {
		fd_dasd = dup(fd);
		if (fd_dasd < 0)
			return -1;
	}

	if (ioctl(fd_dasd, BIODASDINFO, (unsigned long)&info) != 0) {
		info.label_block = 2;
		info.FBA_layout = 0;
		memcpy(info.type, "ECKD", sizeof(info.type));
	}

	if (ioctl(fd_dasd, BLKSSZGET, &blocksize) != 0)
		goto out;

	if (ioctl(fd_dasd, BLKGETSIZE64, &disksize) != 0)
		goto out;

	if (ioctl(fd_dasd, HDIO_GETGEO, (unsigned long)&geo) != 0) {
		unsigned int cyl;

		geo.heads = 15;
		geo.sectors = recs_per_track(blocksize);
		cyl = disksize / ((uint64_t)blocksize * geo.heads *
				  geo.sectors);
		if (cyl < LV_COMPAT_CYL)
			geo.cylinders = cyl;
		else
			geo.cylinders = LV_COMPAT_CYL;
		geo.start = 0;
	}

	disksize >>= 9;

	if (blocksize < 512 || blocksize > 4096)
		goto out;

	/*
	 * Get volume label, extract name and type.
	 */

	if (!(data = (unsigned char *)malloc(blocksize)))
		goto out;


	if (lseek(fd_dasd, info.label_block * blocksize, SEEK_SET) == -1)
		goto out;
	if (read(fd_dasd, data, blocksize) == -1) {
		perror("read");
		goto out;
	}

	if ((!info.FBA_layout) && (!strcmp(info.type, "ECKD")))
		memcpy (&vlabel, data, sizeof(vlabel));
	else {
		bzero(&vlabel,4);
		memcpy ((char *)&vlabel + 4, data, sizeof(vlabel) - 4);
	}
	vtoc_ebcdic_dec(vlabel.vollbl, type, 4);

	/*
	 * Three different types: CMS1, VOL1 and LNX1/unlabeled
	 */
	if (strncmp(type, "CMS1", 4) == 0) {
		/*
		 * VM style CMS1 labeled disk
		 */
		label_ints_t *label = (label_ints_t *) &vlabel;

		blocksize = label[4];
		if (label[14] != 0) {
			/* disk is reserved minidisk */
			offset = label[14];
			size   = sectors512(label[8] - 1, blocksize);
		} else {
			offset = info.label_block + 1;
			size   = sectors512(label[8], blocksize);
		}
		sp[0].start = sectors512(offset, blocksize);
		sp[0].size  = size - sp[0].start;
		retval = 1;
	} else if ((strncmp(type, "VOL1", 4) == 0) &&
		(!info.FBA_layout) && (!strcmp(info.type, "ECKD"))) {
		/*
		 * New style VOL1 labeled disk
		 */
		int counter;

		/* get block number and read then go through format1 labels */
		blk = cchhb2blk(&vlabel.vtoc, &geo) + 1;
		counter = 0;
		if (lseek(fd_dasd, blk * blocksize, SEEK_SET) == -1)
			goto out;

		while (read(fd_dasd, data, blocksize) != -1) {
			format1_label_t f1;

			memcpy(&f1, data, sizeof(format1_label_t));

			/* skip FMT4 / FMT5 / FMT7 labels */
			if (EBCtoASC[f1.DS1FMTID] == '4'
			    || EBCtoASC[f1.DS1FMTID] == '5'
			    || EBCtoASC[f1.DS1FMTID] == '7'
			    || EBCtoASC[f1.DS1FMTID] == '9') {
				blk++;
				continue;
			}

			/* only FMT1 and FMT8 valid at this point */
			if (EBCtoASC[f1.DS1FMTID] != '1' &&
			    EBCtoASC[f1.DS1FMTID] != '8')
				break;

			/* OK, we got valid partition data */
			offset = cchh2blk(&f1.DS1EXT1.llimit, &geo);
			size  = cchh2blk(&f1.DS1EXT1.ulimit, &geo) -
				offset + geo.sectors;
			sp[counter].start = sectors512(offset, blocksize);
			sp[counter].size  = sectors512(size, blocksize);
			counter++;
			blk++;
		}
		retval = counter;
	} else {
		/*
		 * Old style LNX1 or unlabeled disk
		 */
		if (strncmp(type, "LNX1", 4) == 0) {
			if (vlabel.ldl_version == 0xf2) {
				fmt_size = sectors512(vlabel.formatted_blocks,
						      blocksize);
			} else if (!strcmp(info.type, "ECKD")) {
				/* formatted w/o large volume support */
				fmt_size = geo.cylinders * geo.heads
					* geo.sectors * (blocksize >> 9);
			} else {
				/* old label and no usable disk geometry
				 * (e.g. DIAG) */
				fmt_size = disksize;
			}
			size = disksize;
			if (fmt_size < size)
				size = fmt_size;
		} else if ((unsigned int)major(sbuf.st_rdev) != 94) {
			/* Not a DASD */
			retval = -1;
			goto out;
		} else
			size = disksize;

		sp[0].start = sectors512(info.label_block + 1, blocksize);
		sp[0].size  = size - sp[0].start;
		retval = 1;
	}

out:
	if (data != NULL)
		free(data);
	close(fd_dasd);
	return retval;
}
