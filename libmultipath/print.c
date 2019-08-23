/*
 * Copyright (c) 2005 Christophe Varoqui
 */
#include <stdio.h>
#include <string.h>
#include <libdevmapper.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <libudev.h>

#include "checkers.h"
#include "vector.h"
#include "structs.h"
#include "structs_vec.h"
#include "dmparser.h"
#include "config.h"
#include "configure.h"
#include "pgpolicies.h"
#include "print.h"
#include "defaults.h"
#include "parser.h"
#include "blacklist.h"
#include "switchgroup.h"
#include "devmapper.h"
#include "uevent.h"
#include "debug.h"
#include "discovery.h"

#define MAX(x,y) (((x) > (y)) ? (x) : (y))
#define MIN(x,y) (((x) > (y)) ? (y) : (x))
#define TAIL     (line + len - 1 - c)
#define NOPAD    s = c
#define PAD(x) \
do { \
	while ((int)(c - s) < (x) && (c < (line + len - 1))) \
		*c++ = ' '; \
	s = c; \
} while (0)

static char *
__endline(char *line, size_t len, char *c)
{
	if (c > line) {
		if (c >= line + len)
			c = line + len - 1;
		*(c - 1) = '\n';
		*c = '\0';
	}
	return c;
}

#define PRINT(var, size, format, args...) \
do { \
	fwd = snprintf(var, size, format, ##args); \
	c += (fwd >= size) ? size : fwd; \
} while (0)

/*
 * information printing helpers
 */
static int
snprint_str (char * buff, size_t len, const char * str)
{
	return snprintf(buff, len, "%s", str);
}

static int
snprint_int (char * buff, size_t len, int val)
{
	return snprintf(buff, len, "%i", val);
}

static int
snprint_uint (char * buff, size_t len, unsigned int val)
{
	return snprintf(buff, len, "%u", val);
}

static int
snprint_size (char * buff, size_t len, unsigned long long size)
{
	float s = (float)(size >> 1); /* start with KB */
	char units[] = {'K','M','G','T','P'};
	char *u = units;

	while (s >= 1024 && *u != 'P') {
		s = s / 1024;
		u++;
	}

	return snprintf(buff, len, "%.*f%c", s < 10, s, *u);
}

/*
 * multipath info printing functions
 */
static int
snprint_name (char * buff, size_t len, const struct multipath * mpp)
{
	if (mpp->alias)
		return snprintf(buff, len, "%s", mpp->alias);
	else
		return snprintf(buff, len, "%s", mpp->wwid);
}

static int
snprint_sysfs (char * buff, size_t len, const struct multipath * mpp)
{
	if (mpp->dmi)
		return snprintf(buff, len, "dm-%i", mpp->dmi->minor);
	else
		return snprintf(buff, len, "undef");
}

static int
snprint_ro (char * buff, size_t len, const struct multipath * mpp)
{
	if (!mpp->dmi)
		return snprintf(buff, len, "undef");
	if (mpp->dmi->read_only)
		return snprintf(buff, len, "ro");
	else
		return snprintf(buff, len, "rw");
}

static int
snprint_progress (char * buff, size_t len, int cur, int total)
{
	char * c = buff;
	char * end = buff + len;

	if (total > 0) {
		int i = PROGRESS_LEN * cur / total;
		int j = PROGRESS_LEN - i;

		while (i-- > 0) {
			c += snprintf(c, len, "X");
			if ((len = (end - c)) <= 1) goto out;
		}

		while (j-- > 0) {
			c += snprintf(c, len,  ".");
			if ((len = (end - c)) <= 1) goto out;
		}
	}

	c += snprintf(c, len, " %i/%i", cur, total);

out:
	buff[c - buff + 1] = '\0';
	return (c - buff + 1);
}

static int
snprint_failback (char * buff, size_t len, const struct multipath * mpp)
{
	if (mpp->pgfailback == -FAILBACK_IMMEDIATE)
		return snprintf(buff, len, "immediate");
	if (mpp->pgfailback == -FAILBACK_FOLLOWOVER)
		return snprintf(buff, len, "followover");

	if (!mpp->failback_tick)
		return snprintf(buff, len, "-");
	else
		return snprint_progress(buff, len, mpp->failback_tick,
					mpp->pgfailback);
}

static int
snprint_queueing (char * buff, size_t len, const struct multipath * mpp)
{
	if (mpp->no_path_retry == NO_PATH_RETRY_FAIL)
		return snprintf(buff, len, "off");
	else if (mpp->no_path_retry == NO_PATH_RETRY_QUEUE)
		return snprintf(buff, len, "on");
	else if (mpp->no_path_retry == NO_PATH_RETRY_UNDEF)
		return snprintf(buff, len, "-");
	else if (mpp->no_path_retry > 0) {
		if (mpp->retry_tick > 0)
			return snprintf(buff, len, "%i sec",
					mpp->retry_tick);
		else if (mpp->retry_tick == 0 && mpp->nr_active > 0)
			return snprintf(buff, len, "%i chk",
					mpp->no_path_retry);
		else
			return snprintf(buff, len, "off");
	}
	return 0;
}

static int
snprint_nb_paths (char * buff, size_t len, const struct multipath * mpp)
{
	return snprint_int(buff, len, mpp->nr_active);
}

static int
snprint_dm_map_state (char * buff, size_t len, const struct multipath * mpp)
{
	if (mpp->dmi && mpp->dmi->suspended)
		return snprintf(buff, len, "suspend");
	else
		return snprintf(buff, len, "active");
}

static int
snprint_multipath_size (char * buff, size_t len, const struct multipath * mpp)
{
	return snprint_size(buff, len, mpp->size);
}

static int
snprint_features (char * buff, size_t len, const struct multipath * mpp)
{
	return snprint_str(buff, len, mpp->features);
}

static int
snprint_hwhandler (char * buff, size_t len, const struct multipath * mpp)
{
	return snprint_str(buff, len, mpp->hwhandler);
}

static int
snprint_path_faults (char * buff, size_t len, const struct multipath * mpp)
{
	return snprint_uint(buff, len, mpp->stat_path_failures);
}

static int
snprint_switch_grp (char * buff, size_t len, const struct multipath * mpp)
{
	return snprint_uint(buff, len, mpp->stat_switchgroup);
}

static int
snprint_map_loads (char * buff, size_t len, const struct multipath * mpp)
{
	return snprint_uint(buff, len, mpp->stat_map_loads);
}

static int
snprint_total_q_time (char * buff, size_t len, const struct multipath * mpp)
{
	return snprint_uint(buff, len, mpp->stat_total_queueing_time);
}

static int
snprint_q_timeouts (char * buff, size_t len, const struct multipath * mpp)
{
	return snprint_uint(buff, len, mpp->stat_queueing_timeouts);
}

static int
snprint_map_failures (char * buff, size_t len, const struct multipath * mpp)
{
	return snprint_uint(buff, len, mpp->stat_map_failures);
}

static int
snprint_multipath_uuid (char * buff, size_t len, const struct multipath * mpp)
{
	return snprint_str(buff, len, mpp->wwid);
}

static int
snprint_multipath_vpr (char * buff, size_t len, const struct multipath * mpp)
{
	struct pathgroup * pgp;
	struct path * pp;
	int i, j;

	vector_foreach_slot(mpp->pg, pgp, i) {
		vector_foreach_slot(pgp->paths, pp, j) {
			if (strlen(pp->vendor_id) && strlen(pp->product_id))
				return snprintf(buff, len, "%s,%s",
						pp->vendor_id, pp->product_id);
		}
	}
	return snprintf(buff, len, "##,##");
}


static int
snprint_multipath_vend (char * buff, size_t len, const struct multipath * mpp)
{
	struct pathgroup * pgp;
	struct path * pp;
	int i, j;

	vector_foreach_slot(mpp->pg, pgp, i) {
		vector_foreach_slot(pgp->paths, pp, j) {
			if (strlen(pp->vendor_id))
				return snprintf(buff, len, "%s", pp->vendor_id);
		}
	}
	return snprintf(buff, len, "##");
}

static int
snprint_multipath_prod (char * buff, size_t len, const struct multipath * mpp)
{
	struct pathgroup * pgp;
	struct path * pp;
	int i, j;

	vector_foreach_slot(mpp->pg, pgp, i) {
		vector_foreach_slot(pgp->paths, pp, j) {
			if (strlen(pp->product_id))
				return snprintf(buff, len, "%s", pp->product_id);
		}
	}
	return snprintf(buff, len, "##");
}

static int
snprint_multipath_rev (char * buff, size_t len, const struct multipath * mpp)
{
	struct pathgroup * pgp;
	struct path * pp;
	int i, j;

	vector_foreach_slot(mpp->pg, pgp, i) {
		vector_foreach_slot(pgp->paths, pp, j) {
			if (strlen(pp->rev))
				return snprintf(buff, len, "%s", pp->rev);
		}
	}
	return snprintf(buff, len, "##");
}

static int
snprint_multipath_foreign (char * buff, size_t len, const struct multipath * pp)
{
	return snprintf(buff, len, "%s", "--");
}

static int
snprint_action (char * buff, size_t len, const struct multipath * mpp)
{
	switch (mpp->action) {
	case ACT_REJECT:
		return snprint_str(buff, len, ACT_REJECT_STR);
	case ACT_RENAME:
		return snprint_str(buff, len, ACT_RENAME_STR);
	case ACT_RELOAD:
		return snprint_str(buff, len, ACT_RELOAD_STR);
	case ACT_CREATE:
		return snprint_str(buff, len, ACT_CREATE_STR);
	case ACT_SWITCHPG:
		return snprint_str(buff, len, ACT_SWITCHPG_STR);
	default:
		return 0;
	}
}

/*
 * path info printing functions
 */
static int
snprint_path_uuid (char * buff, size_t len, const struct path * pp)
{
	return snprint_str(buff, len, pp->wwid);
}

static int
snprint_hcil (char * buff, size_t len, const struct path * pp)
{
	if (!pp || pp->sg_id.host_no < 0)
		return snprintf(buff, len, "#:#:#:#");

	return snprintf(buff, len, "%i:%i:%i:%i",
			pp->sg_id.host_no,
			pp->sg_id.channel,
			pp->sg_id.scsi_id,
			pp->sg_id.lun);
}

static int
snprint_dev (char * buff, size_t len, const struct path * pp)
{
	if (!pp || !strlen(pp->dev))
		return snprintf(buff, len, "-");
	else
		return snprint_str(buff, len, pp->dev);
}

static int
snprint_dev_t (char * buff, size_t len, const struct path * pp)
{
	if (!pp || !strlen(pp->dev))
		return snprintf(buff, len, "#:#");
	else
		return snprint_str(buff, len, pp->dev_t);
}

static int
snprint_offline (char * buff, size_t len, const struct path * pp)
{
	if (!pp || !pp->mpp)
		return snprintf(buff, len, "unknown");
	else if (pp->offline)
		return snprintf(buff, len, "offline");
	else
		return snprintf(buff, len, "running");
}

static int
snprint_chk_state (char * buff, size_t len, const struct path * pp)
{
	if (!pp || !pp->mpp)
		return snprintf(buff, len, "undef");

	switch (pp->state) {
	case PATH_UP:
		return snprintf(buff, len, "ready");
	case PATH_DOWN:
		return snprintf(buff, len, "faulty");
	case PATH_SHAKY:
		return snprintf(buff, len, "shaky");
	case PATH_GHOST:
		return snprintf(buff, len, "ghost");
	case PATH_PENDING:
		return snprintf(buff, len, "i/o pending");
	case PATH_TIMEOUT:
		return snprintf(buff, len, "i/o timeout");
	case PATH_DELAYED:
		return snprintf(buff, len, "delayed");
	default:
		return snprintf(buff, len, "undef");
	}
}

static int
snprint_dm_path_state (char * buff, size_t len, const struct path * pp)
{
	if (!pp)
		return snprintf(buff, len, "undef");

	switch (pp->dmstate) {
	case PSTATE_ACTIVE:
		return snprintf(buff, len, "active");
	case PSTATE_FAILED:
		return snprintf(buff, len, "failed");
	default:
		return snprintf(buff, len, "undef");
	}
}

static int
snprint_vpr (char * buff, size_t len, const struct path * pp)
{
	return snprintf(buff, len, "%s,%s",
			pp->vendor_id, pp->product_id);
}

static int
snprint_next_check (char * buff, size_t len, const struct path * pp)
{
	if (!pp || !pp->mpp)
		return snprintf(buff, len, "orphan");

	return snprint_progress(buff, len, pp->tick, pp->checkint);
}

static int
snprint_pri (char * buff, size_t len, const struct path * pp)
{
	return snprint_int(buff, len, pp ? pp->priority : -1);
}

static int
snprint_pg_selector (char * buff, size_t len, const struct pathgroup * pgp)
{
	const char *s = pgp->mpp->selector;

	return snprint_str(buff, len, s ? s : "");
}

static int
snprint_pg_pri (char * buff, size_t len, const struct pathgroup * pgp)
{
	return snprint_int(buff, len, pgp->priority);
}

static int
snprint_pg_state (char * buff, size_t len, const struct pathgroup * pgp)
{
	switch (pgp->status) {
	case PGSTATE_ENABLED:
		return snprintf(buff, len, "enabled");
	case PGSTATE_DISABLED:
		return snprintf(buff, len, "disabled");
	case PGSTATE_ACTIVE:
		return snprintf(buff, len, "active");
	default:
		return snprintf(buff, len, "undef");
	}
}

static int
snprint_pg_marginal (char * buff, size_t len, const struct pathgroup * pgp)
{
	if (pgp->marginal)
		return snprintf(buff, len, "marginal");
	return snprintf(buff, len, "normal");
}

static int
snprint_path_size (char * buff, size_t len, const struct path * pp)
{
	return snprint_size(buff, len, pp->size);
}

int
snprint_path_serial (char * buff, size_t len, const struct path * pp)
{
	return snprint_str(buff, len, pp->serial);
}

static int
snprint_path_mpp (char * buff, size_t len, const struct path * pp)
{
	if (!pp->mpp)
		return snprintf(buff, len, "[orphan]");
	if (!pp->mpp->alias)
		return snprintf(buff, len, "[unknown]");
	return snprint_str(buff, len, pp->mpp->alias);
}

static int
snprint_host_attr (char * buff, size_t len, const struct path * pp, char *attr)
{
	struct udev_device *host_dev = NULL;
	char host_id[32];
	const char *value = NULL;
	int ret;

	if (pp->sg_id.proto_id != SCSI_PROTOCOL_FCP)
		return snprintf(buff, len, "[undef]");
	sprintf(host_id, "host%d", pp->sg_id.host_no);
	host_dev = udev_device_new_from_subsystem_sysname(udev, "fc_host",
							  host_id);
	if (!host_dev) {
		condlog(1, "%s: No fc_host device for '%s'", pp->dev, host_id);
		goto out;
	}
	value = udev_device_get_sysattr_value(host_dev, attr);
	if (value)
		ret = snprint_str(buff, len, value);
	udev_device_unref(host_dev);
out:
	if (!value)
		ret = snprintf(buff, len, "[unknown]");
	return ret;
}

int
snprint_host_wwnn (char * buff, size_t len, const struct path * pp)
{
	return snprint_host_attr(buff, len, pp, "node_name");
}

int
snprint_host_wwpn (char * buff, size_t len, const struct path * pp)
{
	return snprint_host_attr(buff, len, pp, "port_name");
}

int
snprint_tgt_wwpn (char * buff, size_t len, const struct path * pp)
{
	struct udev_device *rport_dev = NULL;
	char rport_id[32];
	const char *value = NULL;
	int ret;

	if (pp->sg_id.proto_id != SCSI_PROTOCOL_FCP)
		return snprintf(buff, len, "[undef]");
	sprintf(rport_id, "rport-%d:%d-%d",
		pp->sg_id.host_no, pp->sg_id.channel, pp->sg_id.transport_id);
	rport_dev = udev_device_new_from_subsystem_sysname(udev,
				"fc_remote_ports", rport_id);
	if (!rport_dev) {
		condlog(1, "%s: No fc_remote_port device for '%s'", pp->dev,
			rport_id);
		goto out;
	}
	value = udev_device_get_sysattr_value(rport_dev, "port_name");
	if (value)
		ret = snprint_str(buff, len, value);
	udev_device_unref(rport_dev);
out:
	if (!value)
		ret = snprintf(buff, len, "[unknown]");
	return ret;
}


int
snprint_tgt_wwnn (char * buff, size_t len, const struct path * pp)
{
	if (pp->tgt_node_name[0] == '\0')
		return snprintf(buff, len, "[undef]");
	return snprint_str(buff, len, pp->tgt_node_name);
}

static int
snprint_host_adapter (char * buff, size_t len, const struct path * pp)
{
	char adapter[SLOT_NAME_SIZE];

	if (sysfs_get_host_adapter_name(pp, adapter))
		return snprintf(buff, len, "[undef]");
	return snprint_str(buff, len, adapter);
}

static int
snprint_path_checker (char * buff, size_t len, const struct path * pp)
{
	const struct checker * c = &pp->checker;
	return snprint_str(buff, len, checker_name(c));
}

static int
snprint_path_foreign (char * buff, size_t len, const struct path * pp)
{
	return snprintf(buff, len, "%s", "--");
}

static int
snprint_path_failures(char * buff, size_t len, const struct path * pp)
{
	return snprint_int(buff, len, pp->failcount);
}

/* if you add a protocol string bigger than "scsi:unspec" you must
 * also change PROTOCOL_BUF_SIZE */
int
snprint_path_protocol(char * buff, size_t len, const struct path * pp)
{
	switch (pp->bus) {
	case SYSFS_BUS_SCSI:
		switch (pp->sg_id.proto_id) {
		case SCSI_PROTOCOL_FCP:
			return snprintf(buff, len, "scsi:fcp");
		case SCSI_PROTOCOL_SPI:
			return snprintf(buff, len, "scsi:spi");
		case SCSI_PROTOCOL_SSA:
			return snprintf(buff, len, "scsi:ssa");
		case SCSI_PROTOCOL_SBP:
			return snprintf(buff, len, "scsi:sbp");
		case SCSI_PROTOCOL_SRP:
			return snprintf(buff, len, "scsi:srp");
		case SCSI_PROTOCOL_ISCSI:
			return snprintf(buff, len, "scsi:iscsi");
		case SCSI_PROTOCOL_SAS:
			return snprintf(buff, len, "scsi:sas");
		case SCSI_PROTOCOL_ADT:
			return snprintf(buff, len, "scsi:adt");
		case SCSI_PROTOCOL_ATA:
			return snprintf(buff, len, "scsi:ata");
		case SCSI_PROTOCOL_UNSPEC:
		default:
			return snprintf(buff, len, "scsi:unspec");
		}
	case SYSFS_BUS_CCW:
		return snprintf(buff, len, "ccw");
	case SYSFS_BUS_CCISS:
		return snprintf(buff, len, "cciss");
	case SYSFS_BUS_NVME:
		return snprintf(buff, len, "nvme");
	case SYSFS_BUS_UNDEF:
	default:
		return snprintf(buff, len, "undef");
	}
}

int
snprint_path_marginal(char * buff, size_t len, const struct path * pp)
{
	if (pp->marginal)
		return snprintf(buff, len, "marginal");
	return snprintf(buff, len, "normal");
}

struct multipath_data mpd[] = {
	{'n', "name",          0, snprint_name},
	{'w', "uuid",          0, snprint_multipath_uuid},
	{'d', "sysfs",         0, snprint_sysfs},
	{'F', "failback",      0, snprint_failback},
	{'Q', "queueing",      0, snprint_queueing},
	{'N', "paths",         0, snprint_nb_paths},
	{'r', "write_prot",    0, snprint_ro},
	{'t', "dm-st",         0, snprint_dm_map_state},
	{'S', "size",          0, snprint_multipath_size},
	{'f', "features",      0, snprint_features},
	{'x', "failures",      0, snprint_map_failures},
	{'h', "hwhandler",     0, snprint_hwhandler},
	{'A', "action",        0, snprint_action},
	{'0', "path_faults",   0, snprint_path_faults},
	{'1', "switch_grp",    0, snprint_switch_grp},
	{'2', "map_loads",     0, snprint_map_loads},
	{'3', "total_q_time",  0, snprint_total_q_time},
	{'4', "q_timeouts",    0, snprint_q_timeouts},
	{'s', "vend/prod/rev", 0, snprint_multipath_vpr},
	{'v', "vend",          0, snprint_multipath_vend},
	{'p', "prod",          0, snprint_multipath_prod},
	{'e', "rev",           0, snprint_multipath_rev},
	{'G', "foreign",       0, snprint_multipath_foreign},
	{0, NULL, 0 , NULL}
};

struct path_data pd[] = {
	{'w', "uuid",          0, snprint_path_uuid},
	{'i', "hcil",          0, snprint_hcil},
	{'d', "dev",           0, snprint_dev},
	{'D', "dev_t",         0, snprint_dev_t},
	{'t', "dm_st",         0, snprint_dm_path_state},
	{'o', "dev_st",        0, snprint_offline},
	{'T', "chk_st",        0, snprint_chk_state},
	{'s', "vend/prod/rev", 0, snprint_vpr},
	{'c', "checker",       0, snprint_path_checker},
	{'C', "next_check",    0, snprint_next_check},
	{'p', "pri",           0, snprint_pri},
	{'S', "size",          0, snprint_path_size},
	{'z', "serial",        0, snprint_path_serial},
	{'M', "marginal_st",   0, snprint_path_marginal},
	{'m', "multipath",     0, snprint_path_mpp},
	{'N', "host WWNN",     0, snprint_host_wwnn},
	{'n', "target WWNN",   0, snprint_tgt_wwnn},
	{'R', "host WWPN",     0, snprint_host_wwpn},
	{'r', "target WWPN",   0, snprint_tgt_wwpn},
	{'a', "host adapter",  0, snprint_host_adapter},
	{'G', "foreign",       0, snprint_path_foreign},
	{'0', "failures",      0, snprint_path_failures},
	{'P', "protocol",      0, snprint_path_protocol},
	{0, NULL, 0 , NULL}
};

struct pathgroup_data pgd[] = {
	{'s', "selector",      0, snprint_pg_selector},
	{'p', "pri",           0, snprint_pg_pri},
	{'t', "dm_st",         0, snprint_pg_state},
	{'M', "marginal_st",   0, snprint_pg_marginal},
	{0, NULL, 0 , NULL}
};

int
snprint_wildcards (char * buff, int len)
{
	int i, fwd = 0;

	fwd += snprintf(buff + fwd, len - fwd, "multipath format wildcards:\n");
	for (i = 0; mpd[i].header; i++)
		fwd += snprintf(buff + fwd, len - fwd, "%%%c  %s\n",
				mpd[i].wildcard, mpd[i].header);
	fwd += snprintf(buff + fwd, len - fwd, "\npath format wildcards:\n");
	for (i = 0; pd[i].header; i++)
		fwd += snprintf(buff + fwd, len - fwd, "%%%c  %s\n",
				pd[i].wildcard, pd[i].header);
	fwd += snprintf(buff + fwd, len - fwd, "\npathgroup format wildcards:\n");
	for (i = 0; pgd[i].header; i++)
		fwd += snprintf(buff + fwd, len - fwd, "%%%c  %s\n",
				pgd[i].wildcard, pgd[i].header);
	return fwd;
}

void
get_path_layout(vector pathvec, int header)
{
	vector gpvec = vector_convert(NULL, pathvec, struct path,
				      dm_path_to_gen);
	_get_path_layout(gpvec,
			 header ? LAYOUT_RESET_HEADER : LAYOUT_RESET_ZERO);
	vector_free(gpvec);
}

static void
reset_width(int *width, enum layout_reset reset, const char *header)
{
	switch (reset) {
	case LAYOUT_RESET_HEADER:
		*width = strlen(header);
		break;
	case LAYOUT_RESET_ZERO:
		*width = 0;
		break;
	default:
		/* don't reset */
		break;
	}
}

void
_get_path_layout (const struct _vector *gpvec, enum layout_reset reset)
{
	int i, j;
	char buff[MAX_FIELD_LEN];
	const struct gen_path *gp;

	for (j = 0; pd[j].header; j++) {

		reset_width(&pd[j].width, reset, pd[j].header);

		if (gpvec == NULL)
			continue;

		vector_foreach_slot (gpvec, gp, i) {
			gp->ops->snprint(gp, buff, MAX_FIELD_LEN,
					 pd[j].wildcard);
			pd[j].width = MAX(pd[j].width, strlen(buff));
		}
	}
}

static void
reset_multipath_layout (void)
{
	int i;

	for (i = 0; mpd[i].header; i++)
		mpd[i].width = 0;
}

void
get_multipath_layout (vector mpvec, int header) {
	vector gmvec = vector_convert(NULL, mpvec, struct multipath,
				      dm_multipath_to_gen);
	_get_multipath_layout(gmvec,
			 header ? LAYOUT_RESET_HEADER : LAYOUT_RESET_ZERO);
	vector_free(gmvec);
}

void
_get_multipath_layout (const struct _vector *gmvec,
			    enum layout_reset reset)
{
	int i, j;
	char buff[MAX_FIELD_LEN];
	const struct gen_multipath * gm;

	for (j = 0; mpd[j].header; j++) {

		reset_width(&mpd[j].width, reset, mpd[j].header);

		if (gmvec == NULL)
			continue;

		vector_foreach_slot (gmvec, gm, i) {
			gm->ops->snprint(gm, buff, MAX_FIELD_LEN,
					 mpd[j].wildcard);
			mpd[j].width = MAX(mpd[j].width, strlen(buff));
		}
		condlog(4, "%s: width %d", mpd[j].header, mpd[j].width);
	}
}

static struct multipath_data *
mpd_lookup(char wildcard)
{
	int i;

	for (i = 0; mpd[i].header; i++)
		if (mpd[i].wildcard == wildcard)
			return &mpd[i];

	return NULL;
}

int snprint_multipath_attr(const struct gen_multipath* gm,
			   char *buf, int len, char wildcard)
{
	const struct multipath *mpp = gen_multipath_to_dm(gm);
	struct multipath_data *mpd = mpd_lookup(wildcard);

	if (mpd == NULL)
		return 0;
	return mpd->snprint(buf, len, mpp);
}

static struct path_data *
pd_lookup(char wildcard)
{
	int i;

	for (i = 0; pd[i].header; i++)
		if (pd[i].wildcard == wildcard)
			return &pd[i];

	return NULL;
}

int snprint_path_attr(const struct gen_path* gp,
		      char *buf, int len, char wildcard)
{
	const struct path *pp = gen_path_to_dm(gp);
	struct path_data *pd = pd_lookup(wildcard);

	if (pd == NULL)
		return 0;
	return pd->snprint(buf, len, pp);
}

static struct pathgroup_data *
pgd_lookup(char wildcard)
{
	int i;

	for (i = 0; pgd[i].header; i++)
		if (pgd[i].wildcard == wildcard)
			return &pgd[i];

	return NULL;
}

int snprint_pathgroup_attr(const struct gen_pathgroup* gpg,
			   char *buf, int len, char wildcard)
{
	const struct pathgroup *pg = gen_pathgroup_to_dm(gpg);
	struct pathgroup_data *pdg = pgd_lookup(wildcard);

	if (pdg == NULL)
		return 0;
	return pdg->snprint(buf, len, pg);
}

int
snprint_multipath_header (char * line, int len, const char * format)
{
	char * c = line;   /* line cursor */
	char * s = line;   /* for padding */
	const char * f = format; /* format string cursor */
	int fwd;
	struct multipath_data * data;

	do {
		if (TAIL <= 0)
			break;

		if (*f != '%') {
			*c++ = *f;
			NOPAD;
			continue;
		}
		f++;

		if (!(data = mpd_lookup(*f)))
			continue; /* unknown wildcard */

		PRINT(c, TAIL, "%s", data->header);
		PAD(data->width);
	} while (*f++);

	__endline(line, len, c);
	return (c - line);
}

int
_snprint_multipath (const struct gen_multipath * gmp,
		    char * line, int len, const char * format, int pad)
{
	char * c = line;   /* line cursor */
	char * s = line;   /* for padding */
	const char * f = format; /* format string cursor */
	int fwd;
	struct multipath_data * data;
	char buff[MAX_FIELD_LEN] = {};

	do {
		if (TAIL <= 0)
			break;

		if (*f != '%') {
			*c++ = *f;
			NOPAD;
			continue;
		}
		f++;

		if (!(data = mpd_lookup(*f)))
			continue;

		gmp->ops->snprint(gmp, buff, MAX_FIELD_LEN, *f);
		PRINT(c, TAIL, "%s", buff);
		if (pad)
			PAD(data->width);
		buff[0] = '\0';
	} while (*f++);

	__endline(line, len, c);
	return (c - line);
}

int
snprint_path_header (char * line, int len, const char * format)
{
	char * c = line;   /* line cursor */
	char * s = line;   /* for padding */
	const char * f = format; /* format string cursor */
	int fwd;
	struct path_data * data;

	do {
		if (TAIL <= 0)
			break;

		if (*f != '%') {
			*c++ = *f;
			NOPAD;
			continue;
		}
		f++;

		if (!(data = pd_lookup(*f)))
			continue; /* unknown wildcard */

		PRINT(c, TAIL, "%s", data->header);
		PAD(data->width);
	} while (*f++);

	__endline(line, len, c);
	return (c - line);
}

int
_snprint_path (const struct gen_path * gp, char * line, int len,
	       const char * format, int pad)
{
	char * c = line;   /* line cursor */
	char * s = line;   /* for padding */
	const char * f = format; /* format string cursor */
	int fwd;
	struct path_data * data;
	char buff[MAX_FIELD_LEN];

	do {
		if (TAIL <= 0)
			break;

		if (*f != '%') {
			*c++ = *f;
			NOPAD;
			continue;
		}
		f++;

		if (!(data = pd_lookup(*f)))
			continue;

		gp->ops->snprint(gp, buff, MAX_FIELD_LEN, *f);
		PRINT(c, TAIL, "%s", buff);
		if (pad)
			PAD(data->width);
	} while (*f++);

	__endline(line, len, c);
	return (c - line);
}

int
_snprint_pathgroup (const struct gen_pathgroup * ggp, char * line, int len,
		    char * format)
{
	char * c = line;   /* line cursor */
	char * s = line;   /* for padding */
	char * f = format; /* format string cursor */
	int fwd;
	struct pathgroup_data * data;
	char buff[MAX_FIELD_LEN];

	do {
		if (TAIL <= 0)
			break;

		if (*f != '%') {
			*c++ = *f;
			NOPAD;
			continue;
		}
		f++;

		if (!(data = pgd_lookup(*f)))
			continue;

		ggp->ops->snprint(ggp, buff, MAX_FIELD_LEN, *f);
		PRINT(c, TAIL, "%s", buff);
		PAD(data->width);
	} while (*f++);

	__endline(line, len, c);
	return (c - line);
}
#define snprint_pathgroup(line, len, fmt, pgp) \
	_snprint_pathgroup(dm_pathgroup_to_gen(pgp), line, len, fmt)

void _print_multipath_topology(const struct gen_multipath *gmp, int verbosity)
{
	int resize;
	char *buff = NULL;
	char *old = NULL;
	int len, maxlen = MAX_LINE_LEN * MAX_LINES;

	buff = MALLOC(maxlen);
	do {
		if (!buff) {
			if (old)
				FREE(old);
			condlog(0, "couldn't allocate memory for list: %s\n",
				strerror(errno));
			return;
		}

		len = _snprint_multipath_topology(gmp, buff, maxlen, verbosity);
		resize = (len == maxlen - 1);

		if (resize) {
			maxlen *= 2;
			old = buff;
			buff = REALLOC(buff, maxlen);
		}
	} while (resize);
	printf("%s", buff);
	FREE(buff);
}

int
snprint_multipath_style(const struct gen_multipath *gmp, char *style, int len,
			int verbosity)
{
	int n;
	const struct multipath *mpp = gen_multipath_to_dm(gmp);
	bool need_action = (verbosity > 1 &&
			    mpp->action != ACT_NOTHING &&
			    mpp->action != ACT_UNDEF &&
			    mpp->action != ACT_IMPOSSIBLE);
	bool need_wwid = (strncmp(mpp->alias, mpp->wwid, WWID_SIZE));

	n = snprintf(style, len, "%s%s%s%s",
		     need_action ? "%A: " : "", "%n",
		     need_wwid ? " (%w)" : "", " %d %s");
	return MIN(n, len - 1);
}

int _snprint_multipath_topology(const struct gen_multipath *gmp,
				char *buff, int len, int verbosity)
{
	int j, i, fwd = 0;
	const struct _vector *pgvec;
	const struct gen_pathgroup *gpg;
	char style[64];
	char * c = style;
	char fmt[64];
	char * f;

	if (verbosity <= 0)
		return fwd;

	reset_multipath_layout();

	if (verbosity == 1)
		return _snprint_multipath(gmp, buff, len, "%n", 1);

	if(isatty(1))
		c += sprintf(c, "%c[%dm", 0x1B, 1); /* bold on */

	c += gmp->ops->style(gmp, c, sizeof(style) - (c - style),
			     verbosity);
	if(isatty(1))
		c += sprintf(c, "%c[%dm", 0x1B, 0); /* bold off */

	fwd += _snprint_multipath(gmp, buff + fwd, len - fwd, style, 1);
	if (fwd >= len)
		return len;
	fwd += _snprint_multipath(gmp, buff + fwd, len - fwd,
				  PRINT_MAP_PROPS, 1);
	if (fwd >= len)
		return len;

	pgvec = gmp->ops->get_pathgroups(gmp);
	if (pgvec == NULL)
		return fwd;

	vector_foreach_slot (pgvec, gpg, j) {
		const struct _vector *pathvec;
		struct gen_path *gp;

		f=fmt;

		if (j + 1 < VECTOR_SIZE(pgvec)) {
			strcpy(f, "|-+- " PRINT_PG_INDENT);
		} else
			strcpy(f, "`-+- " PRINT_PG_INDENT);
		fwd += _snprint_pathgroup(gpg, buff + fwd, len - fwd, fmt);

		if (fwd >= len) {
			fwd = len;
			break;
		}

		pathvec = gpg->ops->get_paths(gpg);
		if (pathvec == NULL)
			continue;

		vector_foreach_slot (pathvec, gp, i) {
			f=fmt;
			if (*f != '|')
				*f=' ';
			f++;
			if (i + 1 < VECTOR_SIZE(pathvec))
				strcpy(f, " |- " PRINT_PATH_INDENT);
			else
				strcpy(f, " `- " PRINT_PATH_INDENT);
			fwd += _snprint_path(gp, buff + fwd, len - fwd, fmt, 1);
			if (fwd >= len) {
				fwd = len;
				break;
			}
		}
		gpg->ops->rel_paths(gpg, pathvec);

		if (fwd == len)
			break;
	}
	gmp->ops->rel_pathgroups(gmp, pgvec);
	return fwd;
}


static int
snprint_json (char * buff, int len, int indent, char *json_str)
{
	int fwd = 0, i;

	for (i = 0; i < indent; i++) {
		fwd += snprintf(buff + fwd, len - fwd, PRINT_JSON_INDENT);
		if (fwd >= len)
			return fwd;
	}

	fwd += snprintf(buff + fwd, len - fwd, "%s", json_str);
	return fwd;
}

static int
snprint_json_header (char * buff, int len)
{
	int fwd = 0;

	fwd +=  snprint_json(buff, len, 0, PRINT_JSON_START_ELEM);
	if (fwd >= len)
		return fwd;

	fwd +=  snprintf(buff + fwd, len  - fwd, PRINT_JSON_START_VERSION,
			PRINT_JSON_MAJOR_VERSION, PRINT_JSON_MINOR_VERSION);
	return fwd;
}

static int
snprint_json_elem_footer (char * buff, int len, int indent, int last)
{
	int fwd = 0, i;

	for (i = 0; i < indent; i++) {
		fwd += snprintf(buff + fwd, len - fwd, PRINT_JSON_INDENT);
		if (fwd >= len)
			return fwd;
	}

	if (last == 1)
		fwd += snprintf(buff + fwd, len - fwd, "%s", PRINT_JSON_END_LAST_ELEM);
	else
		fwd += snprintf(buff + fwd, len - fwd, "%s", PRINT_JSON_END_ELEM);
	return fwd;
}

static int
snprint_multipath_fields_json (char * buff, int len,
		const struct multipath * mpp, int last)
{
	int i, j, fwd = 0;
	struct path *pp;
	struct pathgroup *pgp;

	fwd += snprint_multipath(buff, len, PRINT_JSON_MAP, mpp, 0);
	if (fwd >= len)
		return fwd;

	fwd += snprint_json(buff + fwd, len - fwd, 2, PRINT_JSON_START_GROUPS);
	if (fwd >= len)
		return fwd;

	vector_foreach_slot (mpp->pg, pgp, i) {

		fwd += snprint_pathgroup(buff + fwd, len - fwd, PRINT_JSON_GROUP, pgp);
		if (fwd >= len)
			return fwd;

		fwd += snprintf(buff + fwd, len - fwd, PRINT_JSON_GROUP_NUM, i + 1);
		if (fwd >= len)
			return fwd;

		fwd += snprint_json(buff + fwd, len - fwd, 3, PRINT_JSON_START_PATHS);
		if (fwd >= len)
			return fwd;

		vector_foreach_slot (pgp->paths, pp, j) {
			fwd += snprint_path(buff + fwd, len - fwd, PRINT_JSON_PATH, pp, 0);
			if (fwd >= len)
				return fwd;

			fwd += snprint_json_elem_footer(buff + fwd,
					len - fwd, 3, j + 1 == VECTOR_SIZE(pgp->paths));
			if (fwd >= len)
				return fwd;
		}
		fwd += snprint_json(buff + fwd, len - fwd, 0, PRINT_JSON_END_ARRAY);
		if (fwd >= len)
			return fwd;

		fwd +=  snprint_json_elem_footer(buff + fwd,
				len - fwd, 2, i + 1 == VECTOR_SIZE(mpp->pg));
		if (fwd >= len)
			return fwd;
	}

	fwd += snprint_json(buff + fwd, len - fwd, 0, PRINT_JSON_END_ARRAY);
	if (fwd >= len)
		return fwd;

	fwd += snprint_json_elem_footer(buff + fwd, len - fwd, 1, last);
	return fwd;
}

int
snprint_multipath_map_json (char * buff, int len,
		const struct multipath * mpp, int last){
	int fwd = 0;

	fwd +=  snprint_json_header(buff, len);
	if (fwd >= len)
		return len;

	fwd +=  snprint_json(buff + fwd, len - fwd, 0, PRINT_JSON_START_MAP);
	if (fwd >= len)
		return len;

	fwd += snprint_multipath_fields_json(buff + fwd, len - fwd, mpp, 1);
	if (fwd >= len)
		return len;

	fwd +=  snprint_json(buff + fwd, len - fwd, 0, "\n");
	if (fwd >= len)
		return len;

	fwd +=  snprint_json(buff + fwd, len - fwd, 0, PRINT_JSON_END_LAST);
	if (fwd >= len)
		return len;
	return fwd;
}

int
snprint_multipath_topology_json (char * buff, int len, const struct vectors * vecs)
{
	int i, fwd = 0;
	struct multipath * mpp;

	fwd +=  snprint_json_header(buff, len);
	if (fwd >= len)
		return len;

	fwd +=  snprint_json(buff + fwd, len  - fwd, 1, PRINT_JSON_START_MAPS);
	if (fwd >= len)
		return len;

	vector_foreach_slot(vecs->mpvec, mpp, i) {
		fwd += snprint_multipath_fields_json(buff + fwd, len - fwd,
				mpp, i + 1 == VECTOR_SIZE(vecs->mpvec));
		if (fwd >= len)
			return len;
	}

	fwd +=  snprint_json(buff + fwd, len - fwd, 0, PRINT_JSON_END_ARRAY);
	if (fwd >= len)
		return len;

	fwd +=  snprint_json(buff + fwd, len - fwd, 0, PRINT_JSON_END_LAST);
	if (fwd >= len)
		return len;
	return fwd;
}

static int
snprint_hwentry (const struct config *conf,
		 char * buff, int len, const struct hwentry * hwe)
{
	int i;
	int fwd = 0;
	struct keyword * kw;
	struct keyword * rootkw;

	rootkw = find_keyword(conf->keywords, NULL, "devices");

	if (!rootkw || !rootkw->sub)
		return 0;

	rootkw = find_keyword(conf->keywords, rootkw->sub, "device");

	if (!rootkw)
		return 0;

	fwd += snprintf(buff + fwd, len - fwd, "\tdevice {\n");
	if (fwd >= len)
		return len;
	iterate_sub_keywords(rootkw, kw, i) {
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t\t%k %v\n",
				kw, hwe);
		if (fwd >= len)
			return len;
	}
	fwd += snprintf(buff + fwd, len - fwd, "\t}\n");
	if (fwd >= len)
		return len;
	return fwd;
}

static int snprint_hwtable(const struct config *conf,
			   char *buff, int len,
			   const struct _vector *hwtable)
{
	int fwd = 0;
	int i;
	struct hwentry * hwe;
	struct keyword * rootkw;

	rootkw = find_keyword(conf->keywords, NULL, "devices");
	if (!rootkw)
		return 0;

	fwd += snprintf(buff + fwd, len - fwd, "devices {\n");
	if (fwd >= len)
		return len;
	vector_foreach_slot (hwtable, hwe, i) {
		fwd += snprint_hwentry(conf, buff + fwd, len - fwd, hwe);
		if (fwd >= len)
			return len;
	}
	fwd += snprintf(buff + fwd, len - fwd, "}\n");
	if (fwd >= len)
		return len;
	return fwd;
}

static int
snprint_mpentry (const struct config *conf, char * buff, int len,
		 const struct mpentry * mpe, const struct _vector *mpvec)
{
	int i;
	int fwd = 0;
	struct keyword * kw;
	struct keyword * rootkw;
	struct multipath *mpp = NULL;

	if (mpvec != NULL && (mpp = find_mp_by_wwid(mpvec, mpe->wwid)) == NULL)
		return 0;

	rootkw = find_keyword(conf->keywords, NULL, "multipath");
	if (!rootkw)
		return 0;

	fwd += snprintf(buff + fwd, len - fwd, "\tmultipath {\n");
	if (fwd >= len)
		return len;
	iterate_sub_keywords(rootkw, kw, i) {
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t\t%k %v\n",
				kw, mpe);
		if (fwd >= len)
			return len;
	}
	/*
	 * This mpp doesn't have alias defined. Add the alias in a comment.
	 */
	if (mpp != NULL && strcmp(mpp->alias, mpp->wwid)) {
		fwd += snprintf(buff + fwd, len - fwd, "\t\t# alias \"%s\"\n",
				mpp->alias);
		if (fwd >= len)
			return len;
	}
	fwd += snprintf(buff + fwd, len - fwd, "\t}\n");
	if (fwd >= len)
		return len;
	return fwd;
}

static int snprint_mptable(const struct config *conf,
			   char *buff, int len, const struct _vector *mpvec)
{
	int fwd = 0;
	int i;
	struct mpentry * mpe;
	struct keyword * rootkw;

	rootkw = find_keyword(conf->keywords, NULL, "multipaths");
	if (!rootkw)
		return 0;

	fwd += snprintf(buff + fwd, len - fwd, "multipaths {\n");
	if (fwd >= len)
		return len;
	vector_foreach_slot (conf->mptable, mpe, i) {
		fwd += snprint_mpentry(conf, buff + fwd, len - fwd, mpe, mpvec);
		if (fwd >= len)
			return len;
	}
	if (mpvec != NULL) {
		struct multipath *mpp;

		vector_foreach_slot(mpvec, mpp, i) {
			if (find_mpe(conf->mptable, mpp->wwid) != NULL)
				continue;

			fwd += snprintf(buff + fwd, len - fwd,
					"\tmultipath {\n");
			if (fwd >= len)
				return len;
			fwd += snprintf(buff + fwd, len - fwd,
					"\t\twwid \"%s\"\n", mpp->wwid);
			if (fwd >= len)
				return len;
			/*
			 * This mpp doesn't have alias defined in
			 * multipath.conf - otherwise find_mpe would have
			 * found it. Add the alias in a comment.
			 */
			if (strcmp(mpp->alias, mpp->wwid)) {
				fwd += snprintf(buff + fwd, len - fwd,
						"\t\t# alias \"%s\"\n",
						mpp->alias);
				if (fwd >= len)
					return len;
			}
			fwd += snprintf(buff + fwd, len - fwd, "\t}\n");
			if (fwd >= len)
				return len;
		}
	}
	fwd += snprintf(buff + fwd, len - fwd, "}\n");
	if (fwd >= len)
		return len;
	return fwd;
}

static int snprint_overrides(const struct config *conf, char * buff, int len,
			     const struct hwentry *overrides)
{
	int fwd = 0;
	int i;
	struct keyword *rootkw;
	struct keyword *kw;

	rootkw = find_keyword(conf->keywords, NULL, "overrides");
	if (!rootkw)
		return 0;

	fwd += snprintf(buff + fwd, len - fwd, "overrides {\n");
	if (fwd >= len)
		return len;
	if (!overrides)
		goto out;
	iterate_sub_keywords(rootkw, kw, i) {
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t%k %v\n",
				       kw, NULL);
		if (fwd >= len)
			return len;
	}
out:
	fwd += snprintf(buff + fwd, len - fwd, "}\n");
	if (fwd >= len)
		return len;
	return fwd;
}

static int snprint_defaults(const struct config *conf, char *buff, int len)
{
	int fwd = 0;
	int i;
	struct keyword *rootkw;
	struct keyword *kw;

	rootkw = find_keyword(conf->keywords, NULL, "defaults");
	if (!rootkw)
		return 0;

	fwd += snprintf(buff + fwd, len - fwd, "defaults {\n");
	if (fwd >= len)
		return len;

	iterate_sub_keywords(rootkw, kw, i) {
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t%k %v\n",
				kw, NULL);
		if (fwd >= len)
			return len;
	}
	fwd += snprintf(buff + fwd, len - fwd, "}\n");
	if (fwd >= len)
		return len;
	return fwd;
}

static int
snprint_blacklist_group (char *buff, int len, int *fwd, vector *vec)
{
	int threshold = MAX_LINE_LEN;
	struct blentry * ble;
	int pos;
	int i;

	pos = *fwd;
	if (!VECTOR_SIZE(*vec)) {
		if ((len - pos - threshold) <= 0)
			return 0;
		pos += snprintf(buff + pos, len - pos, "        <empty>\n");
	} else vector_foreach_slot (*vec, ble, i) {
		if ((len - pos - threshold) <= 0)
			return 0;
		if (ble->origin == ORIGIN_CONFIG)
			pos += snprintf(buff + pos, len - pos, "        (config file rule) ");
		else if (ble->origin == ORIGIN_DEFAULT)
			pos += snprintf(buff + pos, len - pos, "        (default rule)     ");
		pos += snprintf(buff + pos, len - pos, "%s\n", ble->str);
	}

	*fwd = pos;
	return pos;
}

static int
snprint_blacklist_devgroup (char *buff, int len, int *fwd, vector *vec)
{
	int threshold = MAX_LINE_LEN;
	struct blentry_device * bled;
	int pos;
	int i;

	pos = *fwd;
	if (!VECTOR_SIZE(*vec)) {
		if ((len - pos - threshold) <= 0)
			return 0;
		pos += snprintf(buff + pos, len - pos, "        <empty>\n");
	} else vector_foreach_slot (*vec, bled, i) {
		if ((len - pos - threshold) <= 0)
			return 0;
		if (bled->origin == ORIGIN_CONFIG)
			pos += snprintf(buff + pos, len - pos, "        (config file rule) ");
		else if (bled->origin == ORIGIN_DEFAULT)
			pos += snprintf(buff + pos, len - pos, "        (default rule)     ");
		pos += snprintf(buff + pos, len - pos, "%s:%s\n", bled->vendor, bled->product);
	}

	*fwd = pos;
	return pos;
}

int snprint_blacklist_report(struct config *conf, char *buff, int len)
{
	int threshold = MAX_LINE_LEN;
	int fwd = 0;

	if ((len - fwd - threshold) <= 0)
		return len;
	fwd += snprintf(buff + fwd, len - fwd, "device node rules:\n"
					       "- blacklist:\n");
	if (!snprint_blacklist_group(buff, len, &fwd, &conf->blist_devnode))
		return len;

	if ((len - fwd - threshold) <= 0)
		return len;
	fwd += snprintf(buff + fwd, len - fwd, "- exceptions:\n");
	if (snprint_blacklist_group(buff, len, &fwd, &conf->elist_devnode) == 0)
		return len;

	if ((len - fwd - threshold) <= 0)
		return len;
	fwd += snprintf(buff + fwd, len - fwd, "udev property rules:\n"
					       "- blacklist:\n");
	if (!snprint_blacklist_group(buff, len, &fwd, &conf->blist_property))
		return len;

	if ((len - fwd - threshold) <= 0)
		return len;
	fwd += snprintf(buff + fwd, len - fwd, "- exceptions:\n");
	if (snprint_blacklist_group(buff, len, &fwd, &conf->elist_property) == 0)
		return len;

	if ((len - fwd - threshold) <= 0)
		return len;
	fwd += snprintf(buff + fwd, len - fwd, "protocol rules:\n"
					       "- blacklist:\n");
	if (!snprint_blacklist_group(buff, len, &fwd, &conf->blist_protocol))
		return len;

	if ((len - fwd - threshold) <= 0)
		return len;
	fwd += snprintf(buff + fwd, len - fwd, "- exceptions:\n");
	if (snprint_blacklist_group(buff, len, &fwd, &conf->elist_protocol) == 0)
		return len;

	if ((len - fwd - threshold) <= 0)
		return len;
	fwd += snprintf(buff + fwd, len - fwd, "wwid rules:\n"
					       "- blacklist:\n");
	if (snprint_blacklist_group(buff, len, &fwd, &conf->blist_wwid) == 0)
		return len;

	if ((len - fwd - threshold) <= 0)
		return len;
	fwd += snprintf(buff + fwd, len - fwd, "- exceptions:\n");
	if (snprint_blacklist_group(buff, len, &fwd, &conf->elist_wwid) == 0)
		return len;

	if ((len - fwd - threshold) <= 0)
		return len;
	fwd += snprintf(buff + fwd, len - fwd, "device rules:\n"
					       "- blacklist:\n");
	if (snprint_blacklist_devgroup(buff, len, &fwd, &conf->blist_device) == 0)
		return len;

	if ((len - fwd - threshold) <= 0)
		return len;
	fwd += snprintf(buff + fwd, len - fwd, "- exceptions:\n");
	if (snprint_blacklist_devgroup(buff, len, &fwd, &conf->elist_device) == 0)
		return len;

	if (fwd > len)
		return len;
	return fwd;
}

static int snprint_blacklist(const struct config *conf, char *buff, int len)
{
	int i;
	struct blentry * ble;
	struct blentry_device * bled;
	int fwd = 0;
	struct keyword *rootkw;
	struct keyword *kw;

	rootkw = find_keyword(conf->keywords, NULL, "blacklist");
	if (!rootkw)
		return 0;

	fwd += snprintf(buff + fwd, len - fwd, "blacklist {\n");
	if (fwd >= len)
		return len;

	vector_foreach_slot (conf->blist_devnode, ble, i) {
		kw = find_keyword(conf->keywords, rootkw->sub, "devnode");
		if (!kw)
			return 0;
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t%k %v\n",
				       kw, ble);
		if (fwd >= len)
			return len;
	}
	vector_foreach_slot (conf->blist_wwid, ble, i) {
		kw = find_keyword(conf->keywords, rootkw->sub, "wwid");
		if (!kw)
			return 0;
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t%k %v\n",
				       kw, ble);
		if (fwd >= len)
			return len;
	}
	vector_foreach_slot (conf->blist_property, ble, i) {
		kw = find_keyword(conf->keywords, rootkw->sub, "property");
		if (!kw)
			return 0;
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t%k %v\n",
				       kw, ble);
		if (fwd >= len)
			return len;
	}
	vector_foreach_slot (conf->blist_protocol, ble, i) {
		kw = find_keyword(conf->keywords, rootkw->sub, "protocol");
		if (!kw)
			return 0;
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t%k %v\n",
				       kw, ble);
		if (fwd >= len)
			return len;
	}
	rootkw = find_keyword(conf->keywords, rootkw->sub, "device");
	if (!rootkw)
		return 0;

	vector_foreach_slot (conf->blist_device, bled, i) {
		fwd += snprintf(buff + fwd, len - fwd, "\tdevice {\n");
		if (fwd >= len)
			return len;
		kw = find_keyword(conf->keywords, rootkw->sub, "vendor");
		if (!kw)
			return 0;
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t\t%k %v\n",
				       kw, bled);
		if (fwd >= len)
			return len;
		kw = find_keyword(conf->keywords, rootkw->sub, "product");
		if (!kw)
			return 0;
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t\t%k %v\n",
				       kw, bled);
		if (fwd >= len)
			return len;
		fwd += snprintf(buff + fwd, len - fwd, "\t}\n");
		if (fwd >= len)
			return len;
	}
	fwd += snprintf(buff + fwd, len - fwd, "}\n");
	if (fwd >= len)
		return len;
	return fwd;
}

static int snprint_blacklist_except(const struct config *conf,
				    char *buff, int len)
{
	int i;
	struct blentry * ele;
	struct blentry_device * eled;
	int fwd = 0;
	struct keyword *rootkw;
	struct keyword *kw;

	rootkw = find_keyword(conf->keywords, NULL, "blacklist_exceptions");
	if (!rootkw)
		return 0;

	fwd += snprintf(buff + fwd, len - fwd, "blacklist_exceptions {\n");
	if (fwd >= len)
		return len;

	vector_foreach_slot (conf->elist_devnode, ele, i) {
		kw = find_keyword(conf->keywords, rootkw->sub, "devnode");
		if (!kw)
			return 0;
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t%k %v\n",
				       kw, ele);
		if (fwd >= len)
			return len;
	}
	vector_foreach_slot (conf->elist_wwid, ele, i) {
		kw = find_keyword(conf->keywords, rootkw->sub, "wwid");
		if (!kw)
			return 0;
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t%k %v\n",
				       kw, ele);
		if (fwd >= len)
			return len;
	}
	vector_foreach_slot (conf->elist_property, ele, i) {
		kw = find_keyword(conf->keywords, rootkw->sub, "property");
		if (!kw)
			return 0;
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t%k %v\n",
				       kw, ele);
		if (fwd >= len)
			return len;
	}
	vector_foreach_slot (conf->elist_protocol, ele, i) {
		kw = find_keyword(conf->keywords, rootkw->sub, "protocol");
		if (!kw)
			return 0;
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t%k %v\n",
				       kw, ele);
		if (fwd >= len)
			return len;
	}
	rootkw = find_keyword(conf->keywords, rootkw->sub, "device");
	if (!rootkw)
		return 0;

	vector_foreach_slot (conf->elist_device, eled, i) {
		fwd += snprintf(buff + fwd, len - fwd, "\tdevice {\n");
		if (fwd >= len)
			return len;
		kw = find_keyword(conf->keywords, rootkw->sub, "vendor");
		if (!kw)
			return 0;
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t\t%k %v\n",
				       kw, eled);
		if (fwd >= len)
			return len;
		kw = find_keyword(conf->keywords, rootkw->sub, "product");
		if (!kw)
			return 0;
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t\t%k %v\n",
				       kw, eled);
		if (fwd >= len)
			return len;
		fwd += snprintf(buff + fwd, len - fwd, "\t}\n");
		if (fwd >= len)
			return len;
	}
	fwd += snprintf(buff + fwd, len - fwd, "}\n");
	if (fwd >= len)
		return len;
	return fwd;
}

char *snprint_config(const struct config *conf, int *len,
		     const struct _vector *hwtable, const struct _vector *mpvec)
{
	char *reply;
	/* built-in config is >20kB already */
	unsigned int maxlen = 32768;

	for (reply = NULL; maxlen <= UINT_MAX/2; maxlen *= 2) {
		char *c, *tmp = reply;

		reply = REALLOC(reply, maxlen);
		if (!reply) {
			if (tmp)
				free(tmp);
			return NULL;
		}

		c = reply + snprint_defaults(conf, reply, maxlen);
		if ((c - reply) == maxlen)
			continue;

		c += snprint_blacklist(conf, c, reply + maxlen - c);
		if ((c - reply) == maxlen)
			continue;

		c += snprint_blacklist_except(conf, c, reply + maxlen - c);
		if ((c - reply) == maxlen)
			continue;

		c += snprint_hwtable(conf, c, reply + maxlen - c,
				     hwtable ? hwtable : conf->hwtable);
		if ((c - reply) == maxlen)
			continue;

		c += snprint_overrides(conf, c, reply + maxlen - c,
				       conf->overrides);
		if ((c - reply) == maxlen)
			continue;

		if (VECTOR_SIZE(conf->mptable) > 0 ||
		    (mpvec != NULL && VECTOR_SIZE(mpvec) > 0))
			c += snprint_mptable(conf, c, reply + maxlen - c,
					     mpvec);

		if ((c - reply) < maxlen) {
			if (len)
				*len = c - reply;
			return reply;
		}
	}

	free(reply);
	return NULL;
}

int snprint_status(char *buff, int len, const struct vectors *vecs)
{
	int fwd = 0;
	int i;
	unsigned int count[PATH_MAX_STATE] = {0};
	struct path * pp;

	vector_foreach_slot (vecs->pathvec, pp, i) {
		count[pp->state]++;
	}
	fwd += snprintf(buff + fwd, len - fwd, "path checker states:\n");
	for (i=0; i<PATH_MAX_STATE; i++) {
		if (!count[i])
			continue;
		fwd += snprintf(buff + fwd, len - fwd, "%-20s%u\n",
				checker_state_name(i), count[i]);
	}

	int monitored_count = 0;

	vector_foreach_slot(vecs->pathvec, pp, i)
		if (pp->fd >= 0)
			monitored_count++;
	fwd += snprintf(buff + fwd, len - fwd, "\npaths: %d\nbusy: %s\n",
			monitored_count, is_uevent_busy()? "True" : "False");

	if (fwd >= len)
		return len;
	return fwd;
}

int snprint_devices(struct config *conf, char * buff, int len,
		    const struct vectors *vecs)
{
	DIR *blkdir;
	struct dirent *blkdev;
	struct stat statbuf;
	char devpath[PATH_MAX];
	char *devptr;
	int threshold = MAX_LINE_LEN;
	int fwd = 0;
	int r;

	struct path * pp;

	if (!(blkdir = opendir("/sys/block")))
		return 1;

	if ((len - fwd - threshold) <= 0) {
		closedir(blkdir);
		return len;
	}
	fwd += snprintf(buff + fwd, len - fwd, "available block devices:\n");

	strcpy(devpath,"/sys/block/");
	while ((blkdev = readdir(blkdir)) != NULL) {
		if ((strcmp(blkdev->d_name,".") == 0) ||
		    (strcmp(blkdev->d_name,"..") == 0))
			continue;

		devptr = devpath + 11;
		*devptr = '\0';
		strncat(devptr, blkdev->d_name, PATH_MAX-12);
		if (stat(devpath, &statbuf) < 0)
			continue;

		if (S_ISDIR(statbuf.st_mode) == 0)
			continue;

		if ((len - fwd - threshold)  <= 0) {
			closedir(blkdir);
			return len;
		}

		fwd += snprintf(buff + fwd, len - fwd, "    %s", devptr);
		pp = find_path_by_dev(vecs->pathvec, devptr);
		if (!pp) {
			r = filter_devnode(conf->blist_devnode,
					   conf->elist_devnode, devptr);
			if (r > 0)
				fwd += snprintf(buff + fwd, len - fwd,
						" devnode blacklisted, unmonitored");
			else if (r <= 0)
				fwd += snprintf(buff + fwd, len - fwd,
						" devnode whitelisted, unmonitored");
		} else
			fwd += snprintf(buff + fwd, len - fwd,
					" devnode whitelisted, monitored");
		fwd += snprintf(buff + fwd, len - fwd, "\n");
	}
	closedir(blkdir);

	if (fwd >= len)
		return len;
	return fwd;
}

/*
 * stdout printing helpers
 */
void print_path(struct path *pp, char *style)
{
	char line[MAX_LINE_LEN];

	memset(&line[0], 0, MAX_LINE_LEN);
	snprint_path(&line[0], MAX_LINE_LEN, style, pp, 1);
	printf("%s", line);
}

void print_all_paths(vector pathvec, int banner)
{
	print_all_paths_custo(pathvec, banner, PRINT_PATH_LONG);
}

void print_all_paths_custo(vector pathvec, int banner, char *fmt)
{
	int i;
	struct path * pp;
	char line[MAX_LINE_LEN];

	if (!VECTOR_SIZE(pathvec)) {
		if (banner)
			fprintf(stdout, "===== no paths =====\n");
		return;
	}

	if (banner)
		fprintf(stdout, "===== paths list =====\n");

	get_path_layout(pathvec, 1);
	snprint_path_header(line, MAX_LINE_LEN, fmt);
	fprintf(stdout, "%s", line);

	vector_foreach_slot (pathvec, pp, i)
		print_path(pp, fmt);
}
