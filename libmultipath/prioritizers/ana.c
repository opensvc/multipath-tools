/*
 * (C) Copyright HUAWEI Technology Corp. 2017   All Rights Reserved.
 *
 * ana.c
 * Version 1.00
 *
 * Tool to make use of a NVMe-feature called  Asymmetric Namespace Access.
 * It determines the ANA state of a device and prints a priority value to stdout.
 *
 * Author(s): Cheng Jike <chengjike.cheng@huawei.com>
 *            Li Jie <lijie34@huawei.com>
 *
 * This file is released under the GPL version 2, or any later version.
 */
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdbool.h>

#include "debug.h"
#include "prio.h"
#include "structs.h"
#include "ana.h"

enum {
	ANA_PRIO_OPTIMIZED		= 50,
	ANA_PRIO_NONOPTIMIZED		= 10,
	ANA_PRIO_INACCESSIBLE		= 5,
	ANA_PRIO_PERSISTENT_LOSS	= 1,
	ANA_PRIO_CHANGE			= 0,
	ANA_PRIO_RESERVED		= 0,
	ANA_PRIO_GETCTRL_FAILED		= -1,
	ANA_PRIO_NOT_SUPPORTED		= -2,
	ANA_PRIO_GETANAS_FAILED		= -3,
	ANA_PRIO_GETANALOG_FAILED	= -4,
	ANA_PRIO_GETNSID_FAILED		= -5,
	ANA_PRIO_GETNS_FAILED		= -6,
	ANA_PRIO_NO_MEMORY		= -7,
	ANA_PRIO_NO_INFORMATION		= -8,
};

static const char * anas_string[] = {
	[NVME_ANA_OPTIMIZED]			= "ANA Optimized State",
	[NVME_ANA_NONOPTIMIZED]			= "ANA Non-Optimized State",
	[NVME_ANA_INACCESSIBLE]			= "ANA Inaccessible State",
	[NVME_ANA_PERSISTENT_LOSS]		= "ANA Persistent Loss State",
	[NVME_ANA_CHANGE]			= "ANA Change state",
	[NVME_ANA_RESERVED]			= "Invalid namespace group state!",
};

static const char *aas_print_string(int rc)
{
	rc &= 0xff;

	switch(rc) {
	case NVME_ANA_OPTIMIZED:
	case NVME_ANA_NONOPTIMIZED:
	case NVME_ANA_INACCESSIBLE:
	case NVME_ANA_PERSISTENT_LOSS:
	case NVME_ANA_CHANGE:
		return anas_string[rc];
	default:
		return anas_string[NVME_ANA_RESERVED];
	}

	return anas_string[NVME_ANA_RESERVED];
}

static int nvme_get_nsid(int fd, unsigned *nsid)
{
	static struct stat nvme_stat;
	int err = fstat(fd, &nvme_stat);
	if (err < 0)
		return 1;

	if (!S_ISBLK(nvme_stat.st_mode)) {
		condlog(0, "Error: requesting namespace-id from non-block device\n");
		return 1;
	}

	*nsid = ioctl(fd, NVME_IOCTL_ID);
	return 0;
}

static int nvme_submit_admin_passthru(int fd, struct nvme_passthru_cmd *cmd)
{
	return ioctl(fd, NVME_IOCTL_ADMIN_CMD, cmd);
}

int nvme_get_log13(int fd, __u32 nsid, __u8 log_id, __u8 lsp, __u64 lpo,
                 __u16 lsi, bool rae, __u32 data_len, void *data)
{
	struct nvme_admin_cmd cmd = {
		.opcode		= nvme_admin_get_log_page,
		.nsid		= nsid,
		.addr		= (__u64)(uintptr_t) data,
		.data_len	= data_len,
	};
	__u32 numd = (data_len >> 2) - 1;
	__u16 numdu = numd >> 16, numdl = numd & 0xffff;

	cmd.cdw10 = log_id | (numdl << 16) | (rae ? 1 << 15 : 0);
	if (lsp)
		cmd.cdw10 |= lsp << 8;

	cmd.cdw11 = numdu | (lsi << 16);
	cmd.cdw12 = lpo;
	cmd.cdw13 = (lpo >> 32);

	return nvme_submit_admin_passthru(fd, &cmd);

}

int nvme_identify13(int fd, __u32 nsid, __u32 cdw10, __u32 cdw11, void *data)
{
	struct nvme_admin_cmd cmd = {
		.opcode		= nvme_admin_identify,
		.nsid		= nsid,
		.addr		= (__u64)(uintptr_t) data,
		.data_len	= NVME_IDENTIFY_DATA_SIZE,
		.cdw10		= cdw10,
		.cdw11		= cdw11,
	};

	return nvme_submit_admin_passthru(fd, &cmd);
}

int nvme_identify(int fd, __u32 nsid, __u32 cdw10, void *data)
{
	return nvme_identify13(fd, nsid, cdw10, 0, data);
}

int nvme_identify_ctrl(int fd, void *data)
{
	return nvme_identify(fd, 0, NVME_ID_CNS_CTRL, data);
}

int nvme_identify_ns(int fd, __u32 nsid, void *data)
{
	return nvme_identify(fd, nsid, NVME_ID_CNS_NS, data);
}

int nvme_ana_log(int fd, void *ana_log, size_t ana_log_len, int rgo)
{
	__u64 lpo = 0;

	return nvme_get_log13(fd, NVME_NSID_ALL, NVME_LOG_ANA, rgo, lpo, 0,
			true, ana_log_len, ana_log);
}

static int get_ana_state(__u32 nsid, __u32 anagrpid, void *ana_log)
{
	int	rc = ANA_PRIO_GETANAS_FAILED;
	void *base = ana_log;
	struct nvme_ana_rsp_hdr *hdr = base;
	struct nvme_ana_group_desc *ana_desc;
	int offset = sizeof(struct nvme_ana_rsp_hdr);
	__u32 nr_nsids;
	size_t nsid_buf_size;
	int i, j;

	for (i = 0; i < le16_to_cpu(hdr->ngrps); i++) {
		ana_desc = base + offset;
		nr_nsids = le32_to_cpu(ana_desc->nnsids);
		nsid_buf_size = nr_nsids * sizeof(__le32);

		offset += sizeof(*ana_desc);

		for (j = 0; j < nr_nsids; j++) {
			if (nsid == le32_to_cpu(ana_desc->nsids[j]))
				return ana_desc->state;
		}

		if (anagrpid != 0 && anagrpid == le32_to_cpu(ana_desc->grpid))
			rc = ana_desc->state;

		offset += nsid_buf_size;
	}

	return rc;
}

int get_ana_info(struct path * pp, unsigned int timeout)
{
	int	rc;
	__u32 nsid;
	struct nvme_id_ctrl ctrl;
	struct nvme_id_ns ns;
	void *ana_log;
	size_t ana_log_len;

	rc = nvme_identify_ctrl(pp->fd, &ctrl);
	if (rc)
		return ANA_PRIO_GETCTRL_FAILED;

	if(!(ctrl.cmic & (1 << 3)))
		return ANA_PRIO_NOT_SUPPORTED;

	rc = nvme_get_nsid(pp->fd, &nsid);
	if (rc)
		return ANA_PRIO_GETNSID_FAILED;

	rc = nvme_identify_ns(pp->fd, nsid, &ns);
	if (rc)
		return ANA_PRIO_GETNS_FAILED;

	ana_log_len = sizeof(struct nvme_ana_rsp_hdr) +
		le32_to_cpu(ctrl.nanagrpid) * sizeof(struct nvme_ana_group_desc);
	if (!(ctrl.anacap & (1 << 6)))
		ana_log_len += le32_to_cpu(ctrl.mnan) * sizeof(__le32);

	ana_log = malloc(ana_log_len);
	if (!ana_log)
		return ANA_PRIO_NO_MEMORY;

	rc = nvme_ana_log(pp->fd, ana_log, ana_log_len,
		(ctrl.anacap & (1 << 6)) ? NVME_ANA_LOG_RGO : 0);
	if (rc) {
		free(ana_log);
		return ANA_PRIO_GETANALOG_FAILED;
	}

	rc = get_ana_state(nsid, le32_to_cpu(ns.anagrpid), ana_log);
	if (rc < 0){
		free(ana_log);
		return ANA_PRIO_GETANAS_FAILED;
	}

	free(ana_log);
	condlog(3, "%s: ana state = %02x [%s]", pp->dev, rc, aas_print_string(rc));

	return rc;
}

int getprio(struct path * pp, char * args, unsigned int timeout)
{
	int rc;

	if (pp->fd < 0)
		return ANA_PRIO_NO_INFORMATION;

	rc = get_ana_info(pp, timeout);
	if (rc >= 0) {
		rc &= 0x0f;
		switch(rc) {
		case NVME_ANA_OPTIMIZED:
			rc = ANA_PRIO_OPTIMIZED;
			break;
		case NVME_ANA_NONOPTIMIZED:
			rc = ANA_PRIO_NONOPTIMIZED;
			break;
		case NVME_ANA_INACCESSIBLE:
			rc = ANA_PRIO_INACCESSIBLE;
			break;
		case NVME_ANA_PERSISTENT_LOSS:
			rc = ANA_PRIO_PERSISTENT_LOSS;
			break;
		case NVME_ANA_CHANGE:
			rc = ANA_PRIO_CHANGE;
			break;
		default:
			rc = ANA_PRIO_RESERVED;
		}
	} else {
		switch(rc) {
		case ANA_PRIO_GETCTRL_FAILED:
			condlog(0, "%s: couldn't get ctrl info", pp->dev);
			break;
		case ANA_PRIO_NOT_SUPPORTED:
			condlog(0, "%s: ana not supported", pp->dev);
			break;
		case ANA_PRIO_GETANAS_FAILED:
			condlog(0, "%s: couldn't get ana state", pp->dev);
			break;
		case ANA_PRIO_GETANALOG_FAILED:
			condlog(0, "%s: couldn't get ana log", pp->dev);
			break;
		case ANA_PRIO_GETNS_FAILED:
			condlog(0, "%s: couldn't get namespace", pp->dev);
			break;
		case ANA_PRIO_GETNSID_FAILED:
			condlog(0, "%s: couldn't get namespace id", pp->dev);
			break;
		case ANA_PRIO_NO_MEMORY:
			condlog(0, "%s: couldn't alloc memory", pp->dev);
			break;
		}
	}
	return rc;
}

