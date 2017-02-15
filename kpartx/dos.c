/*
 * Source: copy of util-linux' partx dos.c
 *
 * Copyrights of the original file apply
 * Copyright (c) 2005 Bastian Blank
 */
#include "kpartx.h"
#include "byteorder.h"
#include <stdio.h>
#include <string.h>
#include "dos.h"

static int
is_extended(int type) {
	return (type == 5 || type == 0xf || type == 0x85);
}

static int
read_extended_partition(int fd, struct partition *ep, int en,
			struct slice *sp, int ns)
{
	struct partition p;
	unsigned long start, here, next;
	unsigned char *bp;
	int loopct = 0;
	int moretodo = 1;
	int i, n=0;

	int sector_size_mul = get_sector_size(fd)/512;

	next = start = sector_size_mul * le32_to_cpu(ep->start_sect);

	while (moretodo) {
		here = next;
		moretodo = 0;
		if (++loopct > 100)
			return n;

		bp = (unsigned char *)getblock(fd, here);
		if (bp == NULL)
			return n;

		if (bp[510] != 0x55 || bp[511] != 0xaa)
			return n;

		for (i=0; i<2; i++) {
			memcpy(&p, bp + 0x1be + i * sizeof (p), sizeof (p));
			if (is_extended(p.sys_type)) {
				if (p.start_sect && p.nr_sects && !moretodo) {
					next = start + sector_size_mul * le32_to_cpu(p.start_sect);
					moretodo = 1;
				}
				continue;
			}
			if (n < ns) {
				sp[n].start = here + sector_size_mul * le32_to_cpu(p.start_sect);
				sp[n].size = sector_size_mul * le32_to_cpu(p.nr_sects);
				sp[n].container = en + 1;
				n++;
			} else {
				fprintf(stderr,
				    "dos_extd_partition: too many slices\n");
				return n;
			}
			loopct = 0;
		}
	}
	return n;
}

static int
is_gpt(int type) {
	return (type == 0xEE);
}

int
read_dos_pt(int fd, struct slice all, struct slice *sp, int ns) {
	struct partition p;
	unsigned long offset = all.start;
	int i, n=4;
	unsigned char *bp;
	uint64_t  sector_size_mul = get_sector_size(fd)/512;

	bp = (unsigned char *)getblock(fd, offset);
	if (bp == NULL)
		return -1;

	if (bp[510] != 0x55 || bp[511] != 0xaa)
		return -1;

	for (i=0; i<4; i++) {
		memcpy(&p, bp + 0x1be + i * sizeof (p), sizeof (p));
		if (is_gpt(p.sys_type))
			return 0;
		if (i < ns) {
			sp[i].start =  sector_size_mul * le32_to_cpu(p.start_sect);
			sp[i].size = sector_size_mul * le32_to_cpu(p.nr_sects);
		} else {
			fprintf(stderr,
				"dos_partition: too many slices\n");
			break;
		}
		if (is_extended(p.sys_type)) {
			/* extended partitions only get one or
			   two sectors mapped for LILO to install,
			   whichever is needed to have 1kb of space */
			if (sector_size_mul == 1)
				sp[i].size = 2;
			else sp[i].size = sector_size_mul;
			n += read_extended_partition(fd, &p, i, sp+n, ns-n);
		}
	}
	return n;
}
