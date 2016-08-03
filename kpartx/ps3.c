#include "kpartx.h"
#include "byteorder.h"
#include <sys/types.h>
#include <string.h>

#define SECTOR_SIZE		512
#define MAX_ACL_ENTRIES		8
#define MAX_PARTITIONS		8

#define MAGIC1			0x0FACE0FFULL
#define MAGIC2			0xDEADFACEULL

struct p_acl_entry {
	u_int64_t laid;
	u_int64_t rights;
};

struct d_partition {
	u_int64_t p_start;
	u_int64_t p_size;
	struct p_acl_entry p_acl[MAX_ACL_ENTRIES];
};

struct disklabel {
	u_int8_t d_res1[16];
	u_int64_t d_magic1;
	u_int64_t d_magic2;
	u_int64_t d_res2;
	u_int64_t d_res3;
	struct d_partition d_partitions[MAX_PARTITIONS];
	u_int8_t d_pad[0x600 - MAX_PARTITIONS * sizeof(struct d_partition) - 0x30];
};

static int
read_disklabel(int fd, struct disklabel *label) {
	unsigned char *data;
	int i;

	for (i = 0; i < sizeof(struct disklabel) / SECTOR_SIZE; i++) {
		data = (unsigned char *) getblock(fd, i);
		if (!data)
			return 0;

		memcpy((unsigned char *) label + i * SECTOR_SIZE, data, SECTOR_SIZE);
	}

	return 1;
}

int
read_ps3_pt(int fd, struct slice all, struct slice *sp, int ns) {
	struct disklabel label;
	int n = 0;
	int i;

	if (!read_disklabel(fd, &label))
		return -1;

	if ((be64_to_cpu(label.d_magic1) != MAGIC1) ||
	    (be64_to_cpu(label.d_magic2) != MAGIC2))
		return -1;

	for (i = 0; i < MAX_PARTITIONS; i++) {
		if (label.d_partitions[i].p_start && label.d_partitions[i].p_size) {
			sp[n].start =  be64_to_cpu(label.d_partitions[i].p_start);
			sp[n].size =  be64_to_cpu(label.d_partitions[i].p_size);
			n++;
		}
	}

	return n;
}
