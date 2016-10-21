#ifndef _KPARTX_H
#define _KPARTX_H

#include <stdint.h>
#include <sys/ioctl.h>

/*
 * For each partition type there is a routine that takes
 * a block device and a range, and returns the list of
 * slices found there in the supplied array SP that can
 * hold NS entries. The return value is the number of
 * entries stored, or -1 if the appropriate type is not
 * present.
 */

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

#define safe_sprintf(var, format, args...)	\
	snprintf(var, sizeof(var), format, ##args) >= sizeof(var)

#ifndef BLKSSZGET
#define BLKSSZGET  _IO(0x12,104)	/* get block device sector size */
#endif

int
get_sector_size(int filedes);

/*
 * units: 512 byte sectors
 */
struct slice {
	uint64_t start;
	uint64_t size;
	int container;
	int major;
	int minor;
};

typedef int (ptreader)(int fd, struct slice all, struct slice *sp, int ns);

extern int force_gpt;

extern ptreader read_dos_pt;
extern ptreader read_bsd_pt;
extern ptreader read_solaris_pt;
extern ptreader read_unixware_pt;
extern ptreader read_gpt_pt;
extern ptreader read_dasd_pt;
extern ptreader read_mac_pt;
extern ptreader read_sun_pt;
extern ptreader read_ps3_pt;

char *getblock(int fd, unsigned int secnr);

static inline int
four2int(unsigned char *p) {
	return p[0] + (p[1]<<8) + (p[2]<<16) + (p[3]<<24);
}

#endif /* _KPARTX_H */
