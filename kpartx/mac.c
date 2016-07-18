#include "kpartx.h"
#include "byteorder.h"
#include <stdio.h>
#include <string.h>
#include "mac.h"

int
read_mac_pt(int fd, struct slice all, struct slice *sp, int ns) {
	struct mac_driver_desc *md;
	struct mac_partition *part;
	unsigned secsize;
	char *data;
	int blk, blocks_in_map;
	int n = 0;

	md = (struct mac_driver_desc *) getblock(fd, 0);
	if (md == NULL)
		return -1;

	if (be16_to_cpu(md->signature) != MAC_DRIVER_MAGIC)
		return -1;

	secsize = be16_to_cpu(md->block_size);
	data = getblock(fd, secsize/512);
	if (!data)
		return -1;
	part = (struct mac_partition *) (data + secsize%512);

	if (be16_to_cpu(part->signature) != MAC_PARTITION_MAGIC)
		return -1;

	blocks_in_map = be32_to_cpu(part->map_count);
	for (blk = 1; blk <= blocks_in_map && blk <= ns; ++blk, ++n) {
		int pos = blk * secsize;
		data = getblock(fd, pos/512);
		if (!data)
			return -1;

		part = (struct mac_partition *) (data + pos%512);
		if (be16_to_cpu(part->signature) != MAC_PARTITION_MAGIC)
			break;

		sp[n].start = be32_to_cpu(part->start_block) * (secsize/512);
		sp[n].size = be32_to_cpu(part->block_count) * (secsize/512);
	}
	return n;
}
