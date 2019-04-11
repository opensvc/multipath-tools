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
#include <libudev.h>

#include "debug.h"
#include "nvme-lib.h"
#include "prio.h"
#include "util.h"
#include "structs.h"

enum {
	ANA_ERR_GETCTRL_FAILED		= 1,
	ANA_ERR_NOT_NVME,
	ANA_ERR_NOT_SUPPORTED,
	ANA_ERR_GETANAS_OVERFLOW,
	ANA_ERR_GETANAS_NOTFOUND,
	ANA_ERR_GETANALOG_FAILED,
	ANA_ERR_GETNSID_FAILED,
	ANA_ERR_GETNS_FAILED,
	ANA_ERR_NO_MEMORY,
	ANA_ERR_NO_INFORMATION,
};

static const char *ana_errmsg[] = {
	[ANA_ERR_GETCTRL_FAILED]	= "couldn't get ctrl info",
	[ANA_ERR_NOT_NVME]		= "not an NVMe device",
	[ANA_ERR_NOT_SUPPORTED]		= "ANA not supported",
	[ANA_ERR_GETANAS_OVERFLOW]	= "buffer overflow in ANA log",
	[ANA_ERR_GETANAS_NOTFOUND]	= "NSID or ANAGRPID not found",
	[ANA_ERR_GETANALOG_FAILED]	= "couldn't get ana log",
	[ANA_ERR_GETNSID_FAILED]	= "couldn't get NSID",
	[ANA_ERR_GETNS_FAILED]		= "couldn't get namespace info",
	[ANA_ERR_NO_MEMORY]		= "out of memory",
	[ANA_ERR_NO_INFORMATION]	= "invalid fd",
};

static const char *anas_string[] = {
	[NVME_ANA_OPTIMIZED]			= "ANA Optimized State",
	[NVME_ANA_NONOPTIMIZED]			= "ANA Non-Optimized State",
	[NVME_ANA_INACCESSIBLE]			= "ANA Inaccessible State",
	[NVME_ANA_PERSISTENT_LOSS]		= "ANA Persistent Loss State",
	[NVME_ANA_CHANGE]			= "ANA Change state",
};

static const char *aas_print_string(int rc)
{
	rc &= 0xff;
	if (rc >= 0 && rc < ARRAY_SIZE(anas_string) &&
	    anas_string[rc] != NULL)
		return anas_string[rc];

	return "invalid ANA state";
}

static int get_ana_state(__u32 nsid, __u32 anagrpid, void *ana_log,
			 size_t ana_log_len)
{
	void *base = ana_log;
	struct nvme_ana_rsp_hdr *hdr = base;
	struct nvme_ana_group_desc *ana_desc;
	size_t offset = sizeof(struct nvme_ana_rsp_hdr);
	__u32 nr_nsids;
	size_t nsid_buf_size;
	int i, j;

	for (i = 0; i < le16_to_cpu(hdr->ngrps); i++) {
		ana_desc = base + offset;

		offset += sizeof(*ana_desc);
		if (offset > ana_log_len)
			return -ANA_ERR_GETANAS_OVERFLOW;

		nr_nsids = le32_to_cpu(ana_desc->nnsids);
		nsid_buf_size = nr_nsids * sizeof(__le32);

		offset += nsid_buf_size;
		if (offset > ana_log_len)
			return -ANA_ERR_GETANAS_OVERFLOW;

		for (j = 0; j < nr_nsids; j++) {
			if (nsid == le32_to_cpu(ana_desc->nsids[j]))
				return ana_desc->state;
		}

		if (anagrpid != 0 && anagrpid == le32_to_cpu(ana_desc->grpid))
			return ana_desc->state;

	}
	return -ANA_ERR_GETANAS_NOTFOUND;
}

int get_ana_info(struct path * pp, unsigned int timeout)
{
	int	rc;
	__u32 nsid;
	struct nvme_id_ctrl ctrl;
	struct nvme_id_ns ns;
	void *ana_log;
	size_t ana_log_len;
	bool is_anagrpid_const;

	rc = nvme_id_ctrl_ana(pp->fd, &ctrl);
	if (rc < 0) {
		log_nvme_errcode(rc, pp->dev, "nvme_identify_ctrl");
		return -ANA_ERR_GETCTRL_FAILED;
	} else if (rc == 0)
		return -ANA_ERR_NOT_SUPPORTED;

	nsid = nvme_get_nsid(pp->fd);
	if (nsid <= 0) {
		log_nvme_errcode(rc, pp->dev, "nvme_get_nsid");
		return -ANA_ERR_GETNSID_FAILED;
	}
	is_anagrpid_const = ctrl.anacap & (1 << 6);

	/*
	 * Code copied from nvme-cli/nvme.c. We don't need to allocate an
	 * [nanagrpid*mnan] array of NSIDs because each NSID can occur at most
	 * in one ANA group.
	 */
	ana_log_len = sizeof(struct nvme_ana_rsp_hdr) +
		le32_to_cpu(ctrl.nanagrpid)
		* sizeof(struct nvme_ana_group_desc);

	if (is_anagrpid_const) {
		rc = nvme_identify_ns(pp->fd, nsid, 0, &ns);
		if (rc) {
			log_nvme_errcode(rc, pp->dev, "nvme_identify_ns");
			return -ANA_ERR_GETNS_FAILED;
		}
	} else
		ana_log_len += le32_to_cpu(ctrl.mnan) * sizeof(__le32);

	ana_log = malloc(ana_log_len);
	if (!ana_log)
		return -ANA_ERR_NO_MEMORY;
	pthread_cleanup_push(free, ana_log);
	rc = nvme_ana_log(pp->fd, ana_log, ana_log_len,
			  is_anagrpid_const ? NVME_ANA_LOG_RGO : 0);
	if (rc) {
		log_nvme_errcode(rc, pp->dev, "nvme_ana_log");
		rc = -ANA_ERR_GETANALOG_FAILED;
	} else
		rc = get_ana_state(nsid,
				   is_anagrpid_const ?
				   le32_to_cpu(ns.anagrpid) : 0,
				   ana_log, ana_log_len);
	pthread_cleanup_pop(1);
	if (rc >= 0)
		condlog(4, "%s: ana state = %02x [%s]", pp->dev, rc,
			aas_print_string(rc));
	return rc;
}

/*
 * Priorities modeled roughly after the ALUA model (alua.c/sysfs.c)
 * Reference: ANA Base Protocol (NVMe TP 4004a, 11/13/2018).
 *
 * Differences:
 *
 * - The ANA base spec defines no implicit or explicit (STPG) state management.
 *   If a state is encountered that doesn't allow normal I/O (all except
 *   OPTIMIZED and NON_OPTIMIZED), we can't do anything but either wait for a
 *   Access State Change Notice (can't do that in multipathd as we don't receive
 *   those), or retry commands in regular time intervals until ANATT is expired
 *   (not implemented). Mapping UNAVAILABLE state to ALUA STANDBY is the best we
 *   can currently do.
 *
 *   FIXME: Waiting for ANATT could be implemented with a "delayed failback"
 *   mechanism. The current "failback" method can't be used, as it would
 *   affect failback to every state, and here only failback to UNAVAILABLE
 *   should be delayed.
 *
 * - PERSISTENT_LOSS state is even below ALUA's UNAVAILABLE state.
 *   FIXME: According to the ANA TP, accessing paths in PERSISTENT_LOSS state
 *   in any way makes no sense (e.g. §8.19.6 - paths in this state shouldn't
 *   even be checked under "all paths down" conditions). Device mapper can,
 *   and will, select a PG for IO if it has non-failed paths, even if the
 *   PG has priority 0. We could avoid that only with an "ANA path checker".
 *
 * - ALUA has no CHANGE state. The ANA TP §8.18.3 / §8.19.4 suggests
 *   that CHANGE state should be treated in roughly the same way as
 *   INACCESSIBLE. Therefore we assign the same prio to it.
 *
 * - ALUA's LBA-dependent state has no ANA equivalent.
 */

int getprio(struct path *pp, char *args, unsigned int timeout)
{
	int rc;

	if (pp->fd < 0)
		rc = -ANA_ERR_NO_INFORMATION;
	else
		rc = get_ana_info(pp, timeout);

	switch (rc) {
	case NVME_ANA_OPTIMIZED:
		return 50;
	case NVME_ANA_NONOPTIMIZED:
		return 10;
	case NVME_ANA_INACCESSIBLE:
	case NVME_ANA_CHANGE:
		return 1;
	case NVME_ANA_PERSISTENT_LOSS:
		return 0;
	default:
		break;
	}
	if (rc < 0 && -rc < ARRAY_SIZE(ana_errmsg))
		condlog(2, "%s: ANA error: %s", pp->dev, ana_errmsg[-rc]);
	else
		condlog(1, "%s: invalid ANA rc code %d", pp->dev, rc);
	return -1;
}
