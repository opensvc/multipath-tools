/*
 * Definitions for the NVM Express interface
 * Copyright (c) 2011-2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef NVME_NVME_H_INCLUDED
#define NVME_NVME_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <endian.h>
#include "plugin.h"
#include "json.h"

#define unlikely(x) x

#ifdef LIBUUID
#include <uuid/uuid.h>
#else
typedef struct {
	uint8_t b[16];
} uuid_t;
#endif

#include "linux/nvme.h"

struct nvme_effects_log_page {
	__le32 acs[256];
	__le32 iocs[256];
	__u8   resv[2048];
};

struct nvme_error_log_page {
	__le64	error_count;
	__le16	sqid;
	__le16	cmdid;
	__le16	status_field;
	__le16	parm_error_location;
	__le64	lba;
	__le32	nsid;
	__u8	vs;
	__u8	resv[3];
	__le64	cs;
	__u8	resv2[24];
};

struct nvme_firmware_log_page {
	__u8	afi;
	__u8	resv[7];
	__u64	frs[7];
	__u8	resv2[448];
};

/* idle and active power scales occupy the last 2 bits of the field */
#define POWER_SCALE(s) ((s) >> 6)

struct nvme_host_mem_buffer {
	__u32			hsize;
	__u32			hmdlal;
	__u32			hmdlau;
	__u32			hmdlec;
	__u8			rsvd16[4080];
};

struct nvme_auto_pst {
	__u32	data;
	__u32	rsvd32;
};

struct nvme_timestamp {
	__u8 timestamp[6];
	__u8 attr;
	__u8 rsvd;
};

struct nvme_controller_list {
	__le16 num;
	__le16 identifier[];
};

struct nvme_secondary_controller_entry {
	__le16 scid;	/* Secondary Controller Identifier */
	__le16 pcid;	/* Primary Controller Identifier */
	__u8   scs;	/* Secondary Controller State */
	__u8   rsvd5[3];
	__le16 vfn;	/* Virtual Function Number */
	__le16 nvq;	/* Number of VQ Flexible Resources Assigned */
	__le16 nvi;	/* Number of VI Flexible Resources Assigned */
	__u8   rsvd14[18];
};

struct nvme_secondary_controllers_list {
	__u8   num;
	__u8   rsvd[31];
	struct nvme_secondary_controller_entry sc_entry[127];
};

struct nvme_bar_cap {
	__u16	mqes;
	__u8	ams_cqr;
	__u8	to;
	__u16	bps_css_nssrs_dstrd;
	__u8	mpsmax_mpsmin;
	__u8	rsvd_pmrs;
};

#ifdef __CHECKER__
#define __force       __attribute__((force))
#else
#define __force
#endif

static inline __le16 cpu_to_le16(uint16_t x)
{
	return (__force __le16)htole16(x);
}
static inline __le32 cpu_to_le32(uint32_t x)
{
	return (__force __le32)htole32(x);
}
static inline __le64 cpu_to_le64(uint64_t x)
{
	return (__force __le64)htole64(x);
}

static inline uint16_t le16_to_cpu(__le16 x)
{
	return le16toh((__force __u16)x);
}
static inline uint32_t le32_to_cpu(__le32 x)
{
	return le32toh((__force __u32)x);
}
static inline uint64_t le64_to_cpu(__le64 x)
{
	return le64toh((__force __u64)x);
}

#define MAX_LIST_ITEMS 256
struct list_item {
	char                node[1024];
	struct nvme_id_ctrl ctrl;
	int                 nsid;
	struct nvme_id_ns   ns;
	unsigned            block;
};

struct ctrl_list_item {
	char *name;
	char *address;
	char *transport;
	char *state;
	char *ana_state;
	char *subsysnqn;
	char *traddr;
	char *trsvcid;
	char *host_traddr;
};

struct subsys_list_item {
	char *name;
	char *subsysnqn;
	int nctrls;
	struct ctrl_list_item *ctrls;
};

enum {
	NORMAL,
	JSON,
	BINARY,
};

struct connect_args {
	char *subsysnqn;
	char *transport;
	char *traddr;
	char *trsvcid;
	char *host_traddr;
};

#define SYS_NVME		"/sys/class/nvme"

bool ctrl_matches_connectargs(char *name, struct connect_args *args);
char *find_ctrl_with_connectargs(struct connect_args *args);
char *__parse_connect_arg(char *conargs, const char delim, const char *fieldnm);

extern const char *conarg_nqn;
extern const char *conarg_transport;
extern const char *conarg_traddr;
extern const char *conarg_trsvcid;
extern const char *conarg_host_traddr;

void register_extension(struct plugin *plugin);

#include "argconfig.h"
int parse_and_open(int argc, char **argv, const char *desc,
	const struct argconfig_commandline_options *clo, void *cfg, size_t size);

extern const char *devicename;

int __id_ctrl(int argc, char **argv, struct command *cmd, struct plugin *plugin, void (*vs)(__u8 *vs, struct json_object *root));
int	validate_output_format(char *format);

struct subsys_list_item *get_subsys_list(int *subcnt, char *subsysnqn, __u32 nsid);
void free_subsys_list(struct subsys_list_item *slist, int n);
char *nvme_char_from_block(char *block);

/*
 * is_64bit_reg - It checks whether given offset of the controller register is
 *                64bit or not.
 * @offset: offset of controller register field in bytes
 *
 * It gives true if given offset is 64bit register, otherwise it returns false.
 *
 * Notes:  This function does not care about transport so that the offset is
 * not going to be checked inside of this function for the unsupported fields
 * in a specific transport.  For example, BPMBL(Boot Partition Memory Buffer
 * Location) register is not supported by fabrics, but it can be checked here.
 */
static inline bool is_64bit_reg(__u32 offset)
{
	if (offset == NVME_REG_CAP ||
			offset == NVME_REG_ASQ ||
			offset == NVME_REG_ACQ ||
			offset == NVME_REG_BPMBL)
		return true;

	return false;
}

#endif /* NVME_NVME_H_INCLUDED */
