#ifndef MAC_H
#define MAC_H

#include <stdint.h>

#define MAC_PARTITION_MAGIC     0x504d

/* type field value for A/UX or other Unix partitions */
#define APPLE_AUX_TYPE  "Apple_UNIX_SVR2"

struct mac_partition {
	uint16_t  signature;      /* expected to be MAC_PARTITION_MAGIC */
	uint16_t  res1;
	uint32_t  map_count;      /* # blocks in partition map */
	uint32_t  start_block;    /* absolute starting block # of partition */
	uint32_t  block_count;    /* number of blocks in partition */
	/* there is more stuff after this that we don't need */
};

#define MAC_DRIVER_MAGIC        0x4552

/* Driver descriptor structure, in block 0 */
struct mac_driver_desc {
	uint16_t  signature;      /* expected to be MAC_DRIVER_MAGIC */
	uint16_t  block_size;
	uint32_t  block_count;
	/* ... more stuff */
};

#endif
