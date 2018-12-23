#ifndef NVME_LIB_H
#define NVME_LIB_H

#include "nvme.h"

int log_nvme_errcode(int err, const char *dev, const char *msg);
int libmp_nvme_get_nsid(int fd);
int libmp_nvme_identify_ctrl(int fd, struct nvme_id_ctrl *ctrl);
int libmp_nvme_identify_ns(int fd, __u32 nsid, bool present,
			   struct nvme_id_ns *ns);
int libmp_nvme_ana_log(int fd, void *ana_log, size_t ana_log_len, int rgo);
/*
 * Identify controller, and return true if ANA is supported
 * ctrl will be filled in if controller is identified, even w/o ANA
 * ctrl may be NULL
 */
int nvme_id_ctrl_ana(int fd, struct nvme_id_ctrl *ctrl);

#ifndef _NVME_LIB_C
/*
 * In all files except nvme-lib.c, the nvme functions can be called
 * by their usual name.
 */
#define nvme_get_nsid libmp_nvme_get_nsid
#define nvme_identify_ctrl libmp_nvme_identify_ctrl
#define nvme_identify_ns libmp_nvme_identify_ns
#define nvme_ana_log libmp_nvme_ana_log
/*
 * Undefine these to avoid clashes with libmultipath's byteorder.h
 */
#undef cpu_to_le16
#undef cpu_to_le32
#undef cpu_to_le64
#undef le16_to_cpu
#undef le32_to_cpu
#undef le64_to_cpu
#endif

#endif /* NVME_LIB_H */
