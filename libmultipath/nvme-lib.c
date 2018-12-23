#include <sys/types.h>
/* avoid inclusion of standard API */
#define _NVME_LIB_C 1
#include "nvme-lib.h"
#include "nvme-ioctl.c"
#include "debug.h"

int log_nvme_errcode(int err, const char *dev, const char *msg)
{
	if (err > 0)
		condlog(3, "%s: %s: NVMe status %d", dev, msg, err);
	else if (err < 0)
		condlog(3, "%s: %s: %s", dev, msg, strerror(errno));
	return err;
}

int libmp_nvme_get_nsid(int fd)
{
	return nvme_get_nsid(fd);
}

int libmp_nvme_identify_ctrl(int fd, struct nvme_id_ctrl *ctrl)
{
	return nvme_identify_ctrl(fd, ctrl);
}

int libmp_nvme_identify_ns(int fd, __u32 nsid, bool present,
			   struct nvme_id_ns *ns)
{
	return nvme_identify_ns(fd, nsid, present, ns);
}

int libmp_nvme_ana_log(int fd, void *ana_log, size_t ana_log_len, int rgo)
{
	return nvme_ana_log(fd, ana_log, ana_log_len, rgo);
}

int nvme_id_ctrl_ana(int fd, struct nvme_id_ctrl *ctrl)
{
	int rc;
	struct nvme_id_ctrl c;

	rc = nvme_identify_ctrl(fd, &c);
	if (rc < 0)
		return rc;
	if (ctrl)
		*ctrl = c;
	return c.cmic & (1 << 3) ? 1 : 0;
}
