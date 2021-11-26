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
#include <assert.h>
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
#include "util.h"
#include "foreign.h"
#include "strbuf.h"

#define PRINT_PATH_LONG      "%w %i %d %D %p %t %T %s %o"
#define PRINT_PATH_INDENT    "%i %d %D %t %T %o"
#define PRINT_MAP_PROPS      "size=%S features='%f' hwhandler='%h' wp=%r"
#define PRINT_PG_INDENT      "policy='%s' prio=%p status=%t"

#define PRINT_JSON_MULTIPLIER     5
#define PRINT_JSON_MAJOR_VERSION  0
#define PRINT_JSON_MINOR_VERSION  1
#define PRINT_JSON_START_VERSION  "   \"major_version\": %d,\n" \
				  "   \"minor_version\": %d,\n"
#define PRINT_JSON_START_ELEM     "{\n"
#define PRINT_JSON_START_MAP      "   \"map\":"
#define PRINT_JSON_START_MAPS     "\"maps\": ["
#define PRINT_JSON_START_PATHS    "\"paths\": ["
#define PRINT_JSON_START_GROUPS   "\"path_groups\": ["
#define PRINT_JSON_END_ELEM       "},"
#define PRINT_JSON_END_LAST_ELEM  "}"
#define PRINT_JSON_END_LAST       "}\n"
#define PRINT_JSON_END_ARRAY      "]\n"
#define PRINT_JSON_INDENT_N    3
#define PRINT_JSON_MAP       "{\n" \
			     "      \"name\" : \"%n\",\n" \
			     "      \"uuid\" : \"%w\",\n" \
			     "      \"sysfs\" : \"%d\",\n" \
			     "      \"failback\" : \"%F\",\n" \
			     "      \"queueing\" : \"%Q\",\n" \
			     "      \"paths\" : %N,\n" \
			     "      \"write_prot\" : \"%r\",\n" \
			     "      \"dm_st\" : \"%t\",\n" \
			     "      \"features\" : \"%f\",\n" \
			     "      \"hwhandler\" : \"%h\",\n" \
			     "      \"action\" : \"%A\",\n" \
			     "      \"path_faults\" : %0,\n" \
			     "      \"vend\" : \"%v\",\n" \
			     "      \"prod\" : \"%p\",\n" \
			     "      \"rev\" : \"%e\",\n" \
			     "      \"switch_grp\" : %1,\n" \
			     "      \"map_loads\" : %2,\n" \
			     "      \"total_q_time\" : %3,\n" \
			     "      \"q_timeouts\" : %4,"

#define PRINT_JSON_GROUP     "{\n" \
			     "         \"selector\" : \"%s\",\n" \
			     "         \"pri\" : %p,\n" \
			     "         \"dm_st\" : \"%t\",\n" \
			     "         \"marginal_st\" : \"%M\","

#define PRINT_JSON_GROUP_NUM "         \"group\" : %d,\n"

#define PRINT_JSON_PATH      "{\n" \
			     "            \"dev\" : \"%d\",\n"\
			     "            \"dev_t\" : \"%D\",\n" \
			     "            \"dm_st\" : \"%t\",\n" \
			     "            \"dev_st\" : \"%o\",\n" \
			     "            \"chk_st\" : \"%T\",\n" \
			     "            \"checker\" : \"%c\",\n" \
			     "            \"pri\" : %p,\n" \
			     "            \"host_wwnn\" : \"%N\",\n" \
			     "            \"target_wwnn\" : \"%n\",\n" \
			     "            \"host_wwpn\" : \"%R\",\n" \
			     "            \"target_wwpn\" : \"%r\",\n" \
			     "            \"host_adapter\" : \"%a\",\n" \
			     "            \"marginal_st\" : \"%M\""

#define PROGRESS_LEN  10

struct path_data {
	char wildcard;
	char * header;
	unsigned int width;
	int (*snprint)(struct strbuf *, const struct path * pp);
};

struct multipath_data {
	char wildcard;
	char * header;
	unsigned int width;
	int (*snprint)(struct strbuf *, const struct multipath * mpp);
};

struct pathgroup_data {
	char wildcard;
	char * header;
	unsigned int width;
	int (*snprint)(struct strbuf *, const struct pathgroup * pgp);
};

#define MAX(x,y) (((x) > (y)) ? (x) : (y))
#define MIN(x,y) (((x) > (y)) ? (y) : (x))
/*
 * information printing helpers
 */
static int
snprint_str(struct strbuf *buff, const char *str)
{
	return append_strbuf_str(buff, str);
}

static int
snprint_int (struct strbuf *buff, int val)
{
	return print_strbuf(buff, "%i", val);
}

static int
snprint_uint (struct strbuf *buff, unsigned int val)
{
	return print_strbuf(buff, "%u", val);
}

static int
snprint_size (struct strbuf *buff, unsigned long long size)
{
	float s = (float)(size >> 1); /* start with KB */
	char units[] = {'K','M','G','T','P'};
	char *u = units;

	while (s >= 1024 && *u != 'P') {
		s = s / 1024;
		u++;
	}

	return print_strbuf(buff, "%.*f%c", s < 10, s, *u);
}

/*
 * multipath info printing functions
 */
static int
snprint_name (struct strbuf *buff, const struct multipath * mpp)
{
	if (mpp->alias)
		return append_strbuf_str(buff, mpp->alias);
	else
		return append_strbuf_str(buff, mpp->wwid);
}

static int
snprint_sysfs (struct strbuf *buff, const struct multipath * mpp)
{
	if (mpp->dmi)
		return print_strbuf(buff, "dm-%i", mpp->dmi->minor);
	else
		return append_strbuf_str(buff, "undef");
}

static int
snprint_ro (struct strbuf *buff, const struct multipath * mpp)
{
	if (!mpp->dmi)
		return append_strbuf_str(buff, "undef");
	if (mpp->dmi->read_only)
		return append_strbuf_str(buff, "ro");
	else
		return append_strbuf_str(buff, "rw");
}

static int
snprint_progress (struct strbuf *buff, int cur, int total)
{
	size_t initial_len = get_strbuf_len(buff);
	int rc;

	if (total > 0) {
		int i = PROGRESS_LEN * cur / total;
		int j = PROGRESS_LEN - i;

		if ((rc = fill_strbuf(buff, 'X', i)) < 0 ||
		    (rc = fill_strbuf(buff, '.', j) < 0)) {
			truncate_strbuf(buff, initial_len);
			return rc;
		}
	}

	if ((rc = print_strbuf(buff, " %i/%i", cur, total)) < 0)
		return rc;
	return get_strbuf_len(buff) - initial_len;
}

static int
snprint_failback (struct strbuf *buff, const struct multipath * mpp)
{
	if (mpp->pgfailback == -FAILBACK_IMMEDIATE)
		return append_strbuf_str(buff, "immediate");
	if (mpp->pgfailback == -FAILBACK_FOLLOWOVER)
		return append_strbuf_str(buff, "followover");

	if (!mpp->failback_tick)
		return append_strbuf_str(buff, "-");
	else
		return snprint_progress(buff, mpp->failback_tick,
					mpp->pgfailback);
}

static int
snprint_queueing (struct strbuf *buff, const struct multipath * mpp)
{
	if (mpp->no_path_retry == NO_PATH_RETRY_FAIL)
		return append_strbuf_str(buff, "off");
	else if (mpp->no_path_retry == NO_PATH_RETRY_QUEUE)
		return append_strbuf_str(buff, "on");
	else if (mpp->no_path_retry == NO_PATH_RETRY_UNDEF)
		return append_strbuf_str(buff, "-");
	else if (mpp->no_path_retry > 0) {
		if (mpp->retry_tick > 0)

			return print_strbuf(buff, "%i sec", mpp->retry_tick);
		else if (mpp->retry_tick == 0 && count_active_paths(mpp) > 0)
			return print_strbuf(buff, "%i chk",
					    mpp->no_path_retry);
		else
			return append_strbuf_str(buff, "off");
	}
	return 0;
}

static int
snprint_nb_paths (struct strbuf *buff, const struct multipath * mpp)
{
	return snprint_int(buff, count_active_paths(mpp));
}

static int
snprint_dm_map_state (struct strbuf *buff, const struct multipath * mpp)
{
	if (mpp->dmi && mpp->dmi->suspended)
		return append_strbuf_str(buff, "suspend");
	else
		return append_strbuf_str(buff, "active");
}

static int
snprint_multipath_size (struct strbuf *buff, const struct multipath * mpp)
{
	return snprint_size(buff, mpp->size);
}

static int
snprint_features (struct strbuf *buff, const struct multipath * mpp)
{
	return snprint_str(buff, mpp->features);
}

static int
snprint_hwhandler (struct strbuf *buff, const struct multipath * mpp)
{
	return snprint_str(buff, mpp->hwhandler);
}

static int
snprint_path_faults (struct strbuf *buff, const struct multipath * mpp)
{
	return snprint_uint(buff, mpp->stat_path_failures);
}

static int
snprint_switch_grp (struct strbuf *buff, const struct multipath * mpp)
{
	return snprint_uint(buff, mpp->stat_switchgroup);
}

static int
snprint_map_loads (struct strbuf *buff, const struct multipath * mpp)
{
	return snprint_uint(buff, mpp->stat_map_loads);
}

static int
snprint_total_q_time (struct strbuf *buff, const struct multipath * mpp)
{
	return snprint_uint(buff, mpp->stat_total_queueing_time);
}

static int
snprint_q_timeouts (struct strbuf *buff, const struct multipath * mpp)
{
	return snprint_uint(buff, mpp->stat_queueing_timeouts);
}

static int
snprint_map_failures (struct strbuf *buff, const struct multipath * mpp)
{
	return snprint_uint(buff, mpp->stat_map_failures);
}

static int
snprint_multipath_uuid (struct strbuf *buff, const struct multipath * mpp)
{
	return snprint_str(buff, mpp->wwid);
}

static int
snprint_multipath_vpr (struct strbuf *buff, const struct multipath * mpp)
{
	struct pathgroup * pgp;
	struct path * pp;
	int i, j;

	vector_foreach_slot(mpp->pg, pgp, i) {
		vector_foreach_slot(pgp->paths, pp, j) {
			if (strlen(pp->vendor_id) && strlen(pp->product_id))
				return print_strbuf(buff, "%s,%s",
						    pp->vendor_id, pp->product_id);
		}
	}
	return append_strbuf_str(buff, "##,##");
}


static int
snprint_multipath_vend (struct strbuf *buff, const struct multipath * mpp)
{
	struct pathgroup * pgp;
	struct path * pp;
	int i, j;

	vector_foreach_slot(mpp->pg, pgp, i) {
		vector_foreach_slot(pgp->paths, pp, j) {
			if (strlen(pp->vendor_id))
				return append_strbuf_str(buff, pp->vendor_id);
		}
	}
	return append_strbuf_str(buff, "##");
}

static int
snprint_multipath_prod (struct strbuf *buff, const struct multipath * mpp)
{
	struct pathgroup * pgp;
	struct path * pp;
	int i, j;

	vector_foreach_slot(mpp->pg, pgp, i) {
		vector_foreach_slot(pgp->paths, pp, j) {
			if (strlen(pp->product_id))
				return append_strbuf_str(buff, pp->product_id);
		}
	}
	return append_strbuf_str(buff, "##");
}

static int
snprint_multipath_rev (struct strbuf *buff, const struct multipath * mpp)
{
	struct pathgroup * pgp;
	struct path * pp;
	int i, j;

	vector_foreach_slot(mpp->pg, pgp, i) {
		vector_foreach_slot(pgp->paths, pp, j) {
			if (strlen(pp->rev))
				return append_strbuf_str(buff, pp->rev);
		}
	}
	return append_strbuf_str(buff, "##");
}

static int
snprint_multipath_foreign (struct strbuf *buff,
			   __attribute__((unused)) const struct multipath * pp)
{
	return append_strbuf_str(buff, "--");
}

static int
snprint_action (struct strbuf *buff, const struct multipath * mpp)
{
	switch (mpp->action) {
	case ACT_REJECT:
		return snprint_str(buff, ACT_REJECT_STR);
	case ACT_RENAME:
		return snprint_str(buff, ACT_RENAME_STR);
	case ACT_RELOAD:
		return snprint_str(buff, ACT_RELOAD_STR);
	case ACT_CREATE:
		return snprint_str(buff, ACT_CREATE_STR);
	case ACT_SWITCHPG:
		return snprint_str(buff, ACT_SWITCHPG_STR);
	default:
		return 0;
	}
}

static int
snprint_multipath_vpd_data(struct strbuf *buff,
			   const struct multipath * mpp)
{
	struct pathgroup * pgp;
	struct path * pp;
	int i, j;

	vector_foreach_slot(mpp->pg, pgp, i)
		vector_foreach_slot(pgp->paths, pp, j)
			if (pp->vpd_data)
				return append_strbuf_str(buff, pp->vpd_data);
	return append_strbuf_str(buff, "[undef]");
}

/*
 * path info printing functions
 */
static int
snprint_path_uuid (struct strbuf *buff, const struct path * pp)
{
	return snprint_str(buff, pp->wwid);
}

static int
snprint_hcil (struct strbuf *buff, const struct path * pp)
{
	if (!pp || pp->sg_id.host_no < 0)
		return append_strbuf_str(buff, "#:#:#:#");

	return print_strbuf(buff, "%i:%i:%i:%" PRIu64,
			pp->sg_id.host_no,
			pp->sg_id.channel,
			pp->sg_id.scsi_id,
			pp->sg_id.lun);
}

static int
snprint_dev (struct strbuf *buff, const struct path * pp)
{
	if (!pp || !strlen(pp->dev))
		return append_strbuf_str(buff, "-");
	else
		return snprint_str(buff, pp->dev);
}

static int
snprint_dev_t (struct strbuf *buff, const struct path * pp)
{
	if (!pp || !strlen(pp->dev))
		return append_strbuf_str(buff, "#:#");
	else
		return snprint_str(buff, pp->dev_t);
}

static int
snprint_offline (struct strbuf *buff, const struct path * pp)
{
	if (!pp || !pp->mpp)
		return append_strbuf_str(buff, "unknown");
	else if (pp->offline)
		return append_strbuf_str(buff, "offline");
	else
		return append_strbuf_str(buff, "running");
}

static int
snprint_chk_state (struct strbuf *buff, const struct path * pp)
{
	if (!pp || !pp->mpp)
		return append_strbuf_str(buff, "undef");

	switch (pp->state) {
	case PATH_UP:
		return append_strbuf_str(buff, "ready");
	case PATH_DOWN:
		return append_strbuf_str(buff, "faulty");
	case PATH_SHAKY:
		return append_strbuf_str(buff, "shaky");
	case PATH_GHOST:
		return append_strbuf_str(buff, "ghost");
	case PATH_PENDING:
		return append_strbuf_str(buff, "i/o pending");
	case PATH_TIMEOUT:
		return append_strbuf_str(buff, "i/o timeout");
	case PATH_DELAYED:
		return append_strbuf_str(buff, "delayed");
	default:
		return append_strbuf_str(buff, "undef");
	}
}

static int
snprint_dm_path_state (struct strbuf *buff, const struct path * pp)
{
	if (!pp)
		return append_strbuf_str(buff, "undef");

	switch (pp->dmstate) {
	case PSTATE_ACTIVE:
		return append_strbuf_str(buff, "active");
	case PSTATE_FAILED:
		return append_strbuf_str(buff, "failed");
	default:
		return append_strbuf_str(buff, "undef");
	}
}

static int snprint_initialized(struct strbuf *buff, const struct path * pp)
{
	static const char *init_state_name[] = {
		[INIT_NEW] = "new",
		[INIT_FAILED] = "failed",
		[INIT_MISSING_UDEV] = "udev-missing",
		[INIT_REQUESTED_UDEV] = "udev-requested",
		[INIT_OK] = "ok",
		[INIT_REMOVED] = "removed",
		[INIT_PARTIAL] = "partial",
	};
	const char *str;

	if (pp->initialized < INIT_NEW || pp->initialized >= __INIT_LAST)
		str = "undef";
	else
		str = init_state_name[pp->initialized];
	return append_strbuf_str(buff, str);
}

static int
snprint_vpr (struct strbuf *buff, const struct path * pp)
{
	return print_strbuf(buff, "%s,%s", pp->vendor_id, pp->product_id);
}

static int
snprint_next_check (struct strbuf *buff, const struct path * pp)
{
	if (!pp || !pp->mpp)
		return append_strbuf_str(buff, "orphan");

	return snprint_progress(buff, pp->tick, pp->checkint);
}

static int
snprint_pri (struct strbuf *buff, const struct path * pp)
{
	return snprint_int(buff, pp ? pp->priority : -1);
}

static int
snprint_pg_selector (struct strbuf *buff, const struct pathgroup * pgp)
{
	const char *s = pgp->mpp->selector;

	return snprint_str(buff, s ? s : "");
}

static int
snprint_pg_pri (struct strbuf *buff, const struct pathgroup * pgp)
{
	return snprint_int(buff, pgp->priority);
}

static int
snprint_pg_state (struct strbuf *buff, const struct pathgroup * pgp)
{
	switch (pgp->status) {
	case PGSTATE_ENABLED:
		return append_strbuf_str(buff, "enabled");
	case PGSTATE_DISABLED:
		return append_strbuf_str(buff, "disabled");
	case PGSTATE_ACTIVE:
		return append_strbuf_str(buff, "active");
	default:
		return append_strbuf_str(buff, "undef");
	}
}

static int
snprint_pg_marginal (struct strbuf *buff, const struct pathgroup * pgp)
{
	if (pgp->marginal)
		return append_strbuf_str(buff, "marginal");
	return append_strbuf_str(buff, "normal");
}

static int
snprint_path_size (struct strbuf *buff, const struct path * pp)
{
	return snprint_size(buff, pp->size);
}

int
snprint_path_serial (struct strbuf *buff, const struct path * pp)
{
	return snprint_str(buff, pp->serial);
}

static int
snprint_path_mpp (struct strbuf *buff, const struct path * pp)
{
	if (!pp->mpp)
		return append_strbuf_str(buff, "[orphan]");
	if (!pp->mpp->alias)
		return append_strbuf_str(buff, "[unknown]");
	return snprint_str(buff, pp->mpp->alias);
}

static int
snprint_host_attr (struct strbuf *buff, const struct path * pp, char *attr)
{
	struct udev_device *host_dev = NULL;
	char host_id[32];
	const char *value = NULL;
	int ret;

	if (pp->sg_id.proto_id != SCSI_PROTOCOL_FCP)
		return append_strbuf_str(buff, "[undef]");
	sprintf(host_id, "host%d", pp->sg_id.host_no);
	host_dev = udev_device_new_from_subsystem_sysname(udev, "fc_host",
							  host_id);
	if (!host_dev) {
		condlog(1, "%s: No fc_host device for '%s'", pp->dev, host_id);
		goto out;
	}
	value = udev_device_get_sysattr_value(host_dev, attr);
	if (value)
		ret = snprint_str(buff, value);
	udev_device_unref(host_dev);
out:
	if (!value)
		ret = append_strbuf_str(buff, "[unknown]");
	return ret;
}

int
snprint_host_wwnn (struct strbuf *buff, const struct path * pp)
{
	return snprint_host_attr(buff, pp, "node_name");
}

int
snprint_host_wwpn (struct strbuf *buff, const struct path * pp)
{
	return snprint_host_attr(buff, pp, "port_name");
}

int
snprint_tgt_wwpn (struct strbuf *buff, const struct path * pp)
{
	struct udev_device *rport_dev = NULL;
	char rport_id[42];
	const char *value = NULL;
	int ret;

	if (pp->sg_id.proto_id != SCSI_PROTOCOL_FCP)
		return append_strbuf_str(buff, "[undef]");
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
		ret = snprint_str(buff, value);
	udev_device_unref(rport_dev);
out:
	if (!value)
		ret = append_strbuf_str(buff, "[unknown]");
	return ret;
}


int
snprint_tgt_wwnn (struct strbuf *buff, const struct path * pp)
{
	if (pp->tgt_node_name[0] == '\0')
		return append_strbuf_str(buff, "[undef]");
	return snprint_str(buff, pp->tgt_node_name);
}

static int
snprint_host_adapter (struct strbuf *buff, const struct path * pp)
{
	char adapter[SLOT_NAME_SIZE];

	if (sysfs_get_host_adapter_name(pp, adapter))
		return append_strbuf_str(buff, "[undef]");
	return snprint_str(buff, adapter);
}

static int
snprint_path_checker (struct strbuf *buff, const struct path * pp)
{
	const struct checker * c = &pp->checker;
	return snprint_str(buff, checker_name(c));
}

static int
snprint_path_foreign (struct strbuf *buff,
		      __attribute__((unused)) const struct path * pp)
{
	return append_strbuf_str(buff, "--");
}

static int
snprint_path_failures(struct strbuf *buff, const struct path * pp)
{
	return snprint_int(buff, pp->failcount);
}

/* if you add a protocol string bigger than "scsi:unspec" you must
 * also change PROTOCOL_BUF_SIZE */
int
snprint_path_protocol(struct strbuf *buff, const struct path * pp)
{
	switch (pp->bus) {
	case SYSFS_BUS_SCSI:
		switch (pp->sg_id.proto_id) {
		case SCSI_PROTOCOL_FCP:
			return append_strbuf_str(buff, "scsi:fcp");
		case SCSI_PROTOCOL_SPI:
			return append_strbuf_str(buff, "scsi:spi");
		case SCSI_PROTOCOL_SSA:
			return append_strbuf_str(buff, "scsi:ssa");
		case SCSI_PROTOCOL_SBP:
			return append_strbuf_str(buff, "scsi:sbp");
		case SCSI_PROTOCOL_SRP:
			return append_strbuf_str(buff, "scsi:srp");
		case SCSI_PROTOCOL_ISCSI:
			return append_strbuf_str(buff, "scsi:iscsi");
		case SCSI_PROTOCOL_SAS:
			return append_strbuf_str(buff, "scsi:sas");
		case SCSI_PROTOCOL_ADT:
			return append_strbuf_str(buff, "scsi:adt");
		case SCSI_PROTOCOL_ATA:
			return append_strbuf_str(buff, "scsi:ata");
		case SCSI_PROTOCOL_USB:
			return append_strbuf_str(buff, "scsi:usb");
		case SCSI_PROTOCOL_UNSPEC:
		default:
			return append_strbuf_str(buff, "scsi:unspec");
		}
	case SYSFS_BUS_CCW:
		return append_strbuf_str(buff, "ccw");
	case SYSFS_BUS_CCISS:
		return append_strbuf_str(buff, "cciss");
	case SYSFS_BUS_NVME:
		return append_strbuf_str(buff, "nvme");
	case SYSFS_BUS_UNDEF:
	default:
		return append_strbuf_str(buff, "undef");
	}
}

int
snprint_path_marginal(struct strbuf *buff, const struct path * pp)
{
	if (pp->marginal)
		return append_strbuf_str(buff, "marginal");
	return append_strbuf_str(buff, "normal");
}

static int
snprint_path_vpd_data(struct strbuf *buff, const struct path * pp)
{
	if (pp->vpd_data)
		return append_strbuf_str(buff, pp->vpd_data);
	return append_strbuf_str(buff, "[undef]");
}

static struct multipath_data mpd[] = {
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
	{'g', "vpd page data", 0, snprint_multipath_vpd_data},
	{0, NULL, 0 , NULL}
};

static struct path_data pd[] = {
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
	{'g', "vpd page data", 0, snprint_path_vpd_data},
	{'0', "failures",      0, snprint_path_failures},
	{'P', "protocol",      0, snprint_path_protocol},
	{'I', "init_st",       0, snprint_initialized},
	{0, NULL, 0 , NULL}
};

static struct pathgroup_data pgd[] = {
	{'s', "selector",      0, snprint_pg_selector},
	{'p', "pri",           0, snprint_pg_pri},
	{'t', "dm_st",         0, snprint_pg_state},
	{'M', "marginal_st",   0, snprint_pg_marginal},
	{0, NULL, 0 , NULL}
};

int snprint_wildcards(struct strbuf *buff)
{
	int initial_len = get_strbuf_len(buff);
	int i, rc;

	if ((rc = append_strbuf_str(buff, "multipath format wildcards:\n")) < 0)
		return rc;
	for (i = 0; mpd[i].header; i++)
		if ((rc = print_strbuf(buff, "%%%c  %s\n",
				       mpd[i].wildcard, mpd[i].header)) < 0)
			return rc;

	if ((rc = append_strbuf_str(buff, "\npath format wildcards:\n")) < 0)
		return rc;
	for (i = 0; pd[i].header; i++)
		if ((rc = print_strbuf(buff, "%%%c  %s\n",
				       pd[i].wildcard, pd[i].header)) < 0)
			return rc;

	if ((rc = append_strbuf_str(buff, "\npathgroup format wildcards:\n")) < 0)
		return rc;
	for (i = 0; pgd[i].header; i++)
		if ((rc = print_strbuf(buff, "%%%c  %s\n",
				       pgd[i].wildcard, pgd[i].header)) < 0)
			return rc;

	return get_strbuf_len(buff) - initial_len;
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
reset_width(unsigned int *width, enum layout_reset reset, const char *header)
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
	const struct gen_path *gp;

	for (j = 0; pd[j].header; j++) {
		STRBUF_ON_STACK(buff);

		reset_width(&pd[j].width, reset, pd[j].header);

		if (gpvec == NULL)
			continue;

		vector_foreach_slot (gpvec, gp, i) {
			gp->ops->snprint(gp, &buff, pd[j].wildcard);
			pd[j].width = MAX(pd[j].width, get_strbuf_len(&buff));
			truncate_strbuf(&buff, 0);
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
	const struct gen_multipath * gm;

	for (j = 0; mpd[j].header; j++) {
		STRBUF_ON_STACK(buff);

		reset_width(&mpd[j].width, reset, mpd[j].header);

		if (gmvec == NULL)
			continue;

		vector_foreach_slot (gmvec, gm, i) {
			gm->ops->snprint(gm, &buff, mpd[j].wildcard);
			mpd[j].width = MAX(mpd[j].width, get_strbuf_len(&buff));
			truncate_strbuf(&buff, 0);
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
			   struct strbuf *buf, char wildcard)
{
	const struct multipath *mpp = gen_multipath_to_dm(gm);
	struct multipath_data *mpd = mpd_lookup(wildcard);

	if (mpd == NULL)
		return 0;
	return mpd->snprint(buf, mpp);
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
		      struct strbuf *buf, char wildcard)
{
	const struct path *pp = gen_path_to_dm(gp);
	struct path_data *pd = pd_lookup(wildcard);

	if (pd == NULL)
		return 0;
	return pd->snprint(buf, pp);
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
			   struct strbuf *buf, char wildcard)
{
	const struct pathgroup *pg = gen_pathgroup_to_dm(gpg);
	struct pathgroup_data *pdg = pgd_lookup(wildcard);

	if (pdg == NULL)
		return 0;
	return pdg->snprint(buf, pg);
}

int snprint_multipath_header(struct strbuf *line, const char *format)
{
	int initial_len = get_strbuf_len(line);
	const char *f;
	struct multipath_data * data;
	int rc;

	for (f = strchr(format, '%'); f; f = strchr(++format, '%')) {
		if ((rc = __append_strbuf_str(line, format, f - format)) < 0)
			return rc;

		format = f + 1;
		if (!(data = mpd_lookup(*format)))
			continue; /* unknown wildcard */

		if ((rc = append_strbuf_str(line, data->header)) < 0)
			return rc;
		else if ((unsigned int)rc < data->width)
			if ((rc = fill_strbuf(line, ' ', data->width - rc)) < 0)
				return rc;
	}

	if ((rc = print_strbuf(line, "%s\n", format)) < 0)
		return rc;
	return get_strbuf_len(line) - initial_len;
}

int _snprint_multipath(const struct gen_multipath *gmp,
		       struct strbuf *line, const char *format, int pad)
{
	int initial_len = get_strbuf_len(line);
	const char *f;
	struct multipath_data * data;
	int rc;

	for (f = strchr(format, '%'); f; f = strchr(++format, '%')) {
		if ((rc = __append_strbuf_str(line, format, f - format)) < 0)
			return rc;

		format = f + 1;
		if (!(data = mpd_lookup(*format)))
			continue; /* unknown wildcard */

		if ((rc = gmp->ops->snprint(gmp, line, *format)) < 0)
			return rc;
		else if (pad && (unsigned int)rc < data->width)
			if ((rc = fill_strbuf(line, ' ', data->width - rc)) < 0)
				return rc;
	}

	if ((rc = print_strbuf(line, "%s\n", format)) < 0)
		return rc;
	return get_strbuf_len(line) - initial_len;
}

int snprint_path_header(struct strbuf *line, const char *format)
{
	int initial_len = get_strbuf_len(line);
	const char *f;
	struct path_data *data;
	int rc;

	for (f = strchr(format, '%'); f; f = strchr(++format, '%')) {
		if ((rc = __append_strbuf_str(line, format, f - format)) < 0)
			return rc;

		format = f + 1;
		if (!(data = pd_lookup(*format)))
			continue; /* unknown wildcard */

		if ((rc = append_strbuf_str(line, data->header)) < 0)
			return rc;
		else if ((unsigned int)rc < data->width)
			if ((rc = fill_strbuf(line, ' ', data->width - rc)) < 0)
				return rc;
	}

	if ((rc = print_strbuf(line, "%s\n", format)) < 0)
		return rc;
	return get_strbuf_len(line) - initial_len;
}

int _snprint_path(const struct gen_path *gp, struct strbuf *line,
		  const char *format, int pad)
{
	int initial_len = get_strbuf_len(line);
	const char *f;
	struct path_data * data;
	int rc;

	for (f = strchr(format, '%'); f; f = strchr(++format, '%')) {
		if ((rc = __append_strbuf_str(line, format, f - format)) < 0)
			return rc;

		format = f + 1;
		if (!(data = pd_lookup(*format)))
			continue; /* unknown wildcard */

		if ((rc = gp->ops->snprint(gp, line, *format)) < 0)
			return rc;
		else if (pad && (unsigned int)rc < data->width)
			if ((rc = fill_strbuf(line, ' ', data->width - rc)) < 0)
				return rc;
	}

	if ((rc = print_strbuf(line, "%s\n", format)) < 0)
		return rc;
	return get_strbuf_len(line) - initial_len;
}

int _snprint_pathgroup(const struct gen_pathgroup *ggp, struct strbuf *line,
		       const char *format)
{
	int initial_len = get_strbuf_len(line);
	const char *f;
	struct pathgroup_data *data;
	int rc;

	for (f = strchr(format, '%'); f; f = strchr(++format, '%')) {
		if ((rc = __append_strbuf_str(line, format, f - format)) < 0)
			return rc;

		format = f + 1;
		if (!(data = pgd_lookup(*format)))
			continue; /* unknown wildcard */

		if ((rc = ggp->ops->snprint(ggp, line, *format)) < 0)
			return rc;
		else if ((unsigned int)rc < data->width)
			if ((rc = fill_strbuf(line, ' ', data->width - rc)) < 0)
				return rc;
	}

	if ((rc = print_strbuf(line, "%s\n", format)) < 0)
		return rc;
	return get_strbuf_len(line) - initial_len;
}

#define snprint_pathgroup(line, fmt, pgp)				\
	_snprint_pathgroup(dm_pathgroup_to_gen(pgp), line, fmt)

void _print_multipath_topology(const struct gen_multipath *gmp, int verbosity)
{
	STRBUF_ON_STACK(buff);

	_snprint_multipath_topology(gmp, &buff, verbosity);
	printf("%s", get_strbuf_str(&buff));
}

int snprint_multipath_style(const struct gen_multipath *gmp,
			    struct strbuf *style, int verbosity)
{
	const struct multipath *mpp = gen_multipath_to_dm(gmp);
	bool need_action = (verbosity > 1 &&
			    mpp->action != ACT_NOTHING &&
			    mpp->action != ACT_UNDEF &&
			    mpp->action != ACT_IMPOSSIBLE);
	bool need_wwid = (strncmp(mpp->alias, mpp->wwid, WWID_SIZE));

	return print_strbuf(style, "%s%s%s%s",
			    need_action ? "%A: " : "", "%n",
			    need_wwid ? " (%w)" : "", " %d %s");
}

int _snprint_multipath_topology(const struct gen_multipath *gmp,
				struct strbuf *buff, int verbosity)
{
	int j, i, rc;
	const struct _vector *pgvec;
	const struct gen_pathgroup *gpg;
	STRBUF_ON_STACK(style);
	size_t initial_len = get_strbuf_len(buff);

	if (verbosity <= 0)
		return 0;

	reset_multipath_layout();

	if (verbosity == 1)
		return _snprint_multipath(gmp, buff, "%n", 1);

	if(isatty(1) &&
	   (rc = print_strbuf(&style, "%c[%dm", 0x1B, 1)) < 0) /* bold on */
		return rc;
	if ((rc = gmp->ops->style(gmp, &style, verbosity)) < 0)
		return rc;
	if(isatty(1) &&
	   (rc = print_strbuf(&style, "%c[%dm", 0x1B, 0)) < 0) /* bold off */
		return rc;

	if ((rc = _snprint_multipath(gmp, buff, get_strbuf_str(&style), 1)) < 0
	    || (rc = _snprint_multipath(gmp, buff, PRINT_MAP_PROPS, 1)) < 0)
		return rc;

	pgvec = gmp->ops->get_pathgroups(gmp);
	if (pgvec == NULL)
		goto out;

	vector_foreach_slot (pgvec, gpg, j) {
		const struct _vector *pathvec;
		struct gen_path *gp;
		bool last_group = j + 1 == VECTOR_SIZE(pgvec);

		if ((rc = print_strbuf(buff, "%c-+- ",
				       last_group ? '`' : '|')) < 0 ||
		    (rc = _snprint_pathgroup(gpg, buff, PRINT_PG_INDENT)) < 0)
			return rc;

		pathvec = gpg->ops->get_paths(gpg);
		if (pathvec == NULL)
			continue;

		vector_foreach_slot (pathvec, gp, i) {
			if ((rc = print_strbuf(buff, "%c %c- ",
					       last_group ? ' ' : '|',
					       i + 1 == VECTOR_SIZE(pathvec) ?
					       '`': '|')) < 0 ||
			    (rc = _snprint_path(gp, buff,
						PRINT_PATH_INDENT, 1)) < 0)
				return rc;
		}
		gpg->ops->rel_paths(gpg, pathvec);
	}

	gmp->ops->rel_pathgroups(gmp, pgvec);
out:
	return get_strbuf_len(buff) - initial_len;
}


static int
snprint_json(struct strbuf *buff, int indent, const char *json_str)
{
	int rc;

	if ((rc = fill_strbuf(buff, ' ', indent * PRINT_JSON_INDENT_N)) < 0)
		return rc;

	return append_strbuf_str(buff, json_str);
}

static int snprint_json_header(struct strbuf *buff)
{
	int rc;

	if ((rc = snprint_json(buff, 0, PRINT_JSON_START_ELEM)) < 0)
		return rc;
	return print_strbuf(buff, PRINT_JSON_START_VERSION,
			    PRINT_JSON_MAJOR_VERSION, PRINT_JSON_MINOR_VERSION);
}

static int snprint_json_elem_footer(struct strbuf *buff, int indent, bool last)
{
	int rc;

	if ((rc = fill_strbuf(buff, ' ', indent * PRINT_JSON_INDENT_N)) < 0)
		return rc;

	if (last)
		return append_strbuf_str(buff, PRINT_JSON_END_LAST_ELEM);
	else
		return append_strbuf_str(buff, PRINT_JSON_END_ELEM);
}

static int snprint_multipath_fields_json(struct strbuf *buff,
					 const struct multipath *mpp, int last)
{
	int i, j, rc;
	struct path *pp;
	struct pathgroup *pgp;
	size_t initial_len = get_strbuf_len(buff);

	if ((rc = snprint_multipath(buff, PRINT_JSON_MAP, mpp, 0)) < 0 ||
	    (rc = snprint_json(buff, 2, PRINT_JSON_START_GROUPS)) < 0)
		return rc;

	vector_foreach_slot (mpp->pg, pgp, i) {

		if ((rc = snprint_pathgroup(buff, PRINT_JSON_GROUP, pgp)) < 0 ||
		    (rc = print_strbuf(buff, PRINT_JSON_GROUP_NUM, i + 1)) < 0 ||
		    (rc = snprint_json(buff, 3, PRINT_JSON_START_PATHS)) < 0)
			return rc;

		vector_foreach_slot (pgp->paths, pp, j) {
			if ((rc = snprint_path(buff, PRINT_JSON_PATH,
					       pp, 0)) < 0 ||
			    (rc = snprint_json_elem_footer(
				    buff, 3,
				    j + 1 == VECTOR_SIZE(pgp->paths))) < 0)
				return rc;
		}
		if ((rc = snprint_json(buff, 0, PRINT_JSON_END_ARRAY)) < 0 ||
		    (rc = snprint_json_elem_footer(
			    buff, 2, i + 1 == VECTOR_SIZE(mpp->pg))) < 0)
			return rc;
	}

	if ((rc = snprint_json(buff, 0, PRINT_JSON_END_ARRAY)) < 0 ||
	    (rc = snprint_json_elem_footer(buff, 1, last)) < 0)
		return rc;

	return get_strbuf_len(buff) - initial_len;
}

int snprint_multipath_map_json(struct strbuf *buff, const struct multipath * mpp)
{
	size_t initial_len = get_strbuf_len(buff);
	int rc;

	if ((rc = snprint_json_header(buff)) < 0 ||
	    (rc = snprint_json(buff, 0, PRINT_JSON_START_MAP)) < 0)
		return rc;

	if ((rc = snprint_multipath_fields_json(buff, mpp, 1)) < 0)
		return rc;

	if ((rc = snprint_json(buff, 0, "\n")) < 0 ||
	    (rc = snprint_json(buff, 0, PRINT_JSON_END_LAST)) < 0)
		return rc;

	return get_strbuf_len(buff) - initial_len;
}

int snprint_multipath_topology_json (struct strbuf *buff,
				     const struct vectors * vecs)
{
	int i;
	struct multipath * mpp;
	size_t initial_len = get_strbuf_len(buff);
	int rc;

	if ((rc = snprint_json_header(buff)) < 0 ||
	    (rc = snprint_json(buff, 1, PRINT_JSON_START_MAPS)) < 0)
		return rc;

	vector_foreach_slot(vecs->mpvec, mpp, i) {
		if ((rc = snprint_multipath_fields_json(
			     buff, mpp, i + 1 == VECTOR_SIZE(vecs->mpvec))) < 0)
			return rc;
	}

	if ((rc = snprint_json(buff, 0, PRINT_JSON_END_ARRAY)) < 0 ||
	    (rc = snprint_json(buff, 0, PRINT_JSON_END_LAST)) < 0)
		return rc;

	return get_strbuf_len(buff) - initial_len;
}

static int
snprint_hwentry (const struct config *conf,
		 struct strbuf *buff, const struct hwentry * hwe)
{
	int i, rc;
	struct keyword * kw;
	struct keyword * rootkw;
	size_t initial_len = get_strbuf_len(buff);

	rootkw = find_keyword(conf->keywords, NULL, "devices");
	assert(rootkw && rootkw->sub);
	rootkw = find_keyword(conf->keywords, rootkw->sub, "device");
	assert(rootkw);

	if ((rc = append_strbuf_str(buff, "\tdevice {\n")) < 0)
		return rc;

	iterate_sub_keywords(rootkw, kw, i) {
		if ((rc = snprint_keyword(buff, "\t\t%k %v\n", kw, hwe)) < 0)
			return rc;
	}
	if ((rc = append_strbuf_str(buff, "\t}\n")) < 0)
		return rc;
	return get_strbuf_len(buff) - initial_len;
}

static int snprint_hwtable(const struct config *conf, struct strbuf *buff,
			   const struct _vector *hwtable)
{
	int i, rc;
	struct hwentry * hwe;
	struct keyword * rootkw;
	size_t initial_len = get_strbuf_len(buff);

	rootkw = find_keyword(conf->keywords, NULL, "devices");
	assert(rootkw);

	if ((rc = append_strbuf_str(buff, "devices {\n")) < 0)
		return rc;

	vector_foreach_slot (hwtable, hwe, i) {
		if ((rc = snprint_hwentry(conf, buff, hwe)) < 0)
			return rc;
	}

	if ((rc = append_strbuf_str(buff, "}\n")) < 0)
		return rc;

	return get_strbuf_len(buff) - initial_len;
}

static int
snprint_mpentry (const struct config *conf, struct strbuf *buff,
		 const struct mpentry * mpe, const struct _vector *mpvec)
{
	int i, rc;
	struct keyword * kw;
	struct keyword * rootkw;
	struct multipath *mpp = NULL;
	size_t initial_len = get_strbuf_len(buff);

	if (mpvec != NULL && (mpp = find_mp_by_wwid(mpvec, mpe->wwid)) == NULL)
		return 0;

	rootkw = find_keyword(conf->keywords, NULL, "multipath");
	assert(rootkw);

	if ((rc = append_strbuf_str(buff, "\tmultipath {\n")) < 0)
		return rc;

	iterate_sub_keywords(rootkw, kw, i) {
		if ((rc = snprint_keyword(buff, "\t\t%k %v\n", kw, mpe)) < 0)
			return rc;
	}
	/*
	 * This mpp doesn't have alias defined. Add the alias in a comment.
	 */
	if (mpp != NULL && strcmp(mpp->alias, mpp->wwid) &&
	    (rc = print_strbuf(buff, "\t\t# alias \"%s\"\n", mpp->alias)) < 0)
		return rc;

	if ((rc = append_strbuf_str(buff, "\t}\n")) < 0)
		return rc;

	return get_strbuf_len(buff) - initial_len;
}

static int snprint_mptable(const struct config *conf, struct strbuf *buff,
			   const struct _vector *mpvec)
{
	int i, rc;
	struct mpentry * mpe;
	struct keyword * rootkw;
	size_t initial_len = get_strbuf_len(buff);

	rootkw = find_keyword(conf->keywords, NULL, "multipaths");
	assert(rootkw);

	if ((rc = append_strbuf_str(buff, "multipaths {\n")) < 0)
		return rc;

	vector_foreach_slot (conf->mptable, mpe, i) {
		if ((rc = snprint_mpentry(conf, buff, mpe, mpvec)) < 0)
			return rc;
	}
	if (mpvec != NULL) {
		struct multipath *mpp;

		vector_foreach_slot(mpvec, mpp, i) {
			if (find_mpe(conf->mptable, mpp->wwid) != NULL)
				continue;

			if ((rc = print_strbuf(buff,
					       "\tmultipath {\n\t\twwid \"%s\"\n",
					       mpp->wwid)) < 0)
				return rc;
			/*
			 * This mpp doesn't have alias defined in
			 * multipath.conf - otherwise find_mpe would have
			 * found it. Add the alias in a comment.
			 */
			if (strcmp(mpp->alias, mpp->wwid) &&
			    (rc = print_strbuf(buff, "\t\t# alias \"%s\"\n",
					       mpp->alias)) < 0)
				return rc;
			if ((rc = append_strbuf_str(buff, "\t}\n")) < 0)
				return rc;
		}
	}
	if ((rc = append_strbuf_str(buff, "}\n")) < 0)
		return rc;
	return get_strbuf_len(buff) - initial_len;
}

static int snprint_overrides(const struct config *conf, struct strbuf *buff,
			     const struct hwentry *overrides)
{
	int i, rc;
	struct keyword *rootkw;
	struct keyword *kw;
	size_t initial_len = get_strbuf_len(buff);

	rootkw = find_keyword(conf->keywords, NULL, "overrides");
	assert(rootkw);

	if ((rc = append_strbuf_str(buff, "overrides {\n")) < 0)
		return rc;
	if (!overrides)
		goto out;

	iterate_sub_keywords(rootkw, kw, i) {
		if ((rc = snprint_keyword(buff, "\t%k %v\n", kw, NULL)) < 0)
			return rc;
	}
out:
	if ((rc = append_strbuf_str(buff, "}\n")) < 0)
		return rc;
	return get_strbuf_len(buff) - initial_len;
}

static int snprint_defaults(const struct config *conf, struct strbuf *buff)
{
	int i, rc;
	struct keyword *rootkw;
	struct keyword *kw;
	size_t initial_len = get_strbuf_len(buff);

	rootkw = find_keyword(conf->keywords, NULL, "defaults");
	assert(rootkw);

	if ((rc = append_strbuf_str(buff, "defaults {\n")) < 0)
		return rc;

	iterate_sub_keywords(rootkw, kw, i) {
		if ((rc = snprint_keyword(buff, "\t%k %v\n", kw, NULL)) < 0)
			return rc;
	}
	if ((rc = append_strbuf_str(buff, "}\n")) < 0)
		return rc;
	return get_strbuf_len(buff) - initial_len;
}

static int snprint_blacklist_group(struct strbuf *buff, vector *vec)
{
	struct blentry * ble;
	size_t initial_len = get_strbuf_len(buff);
	int rc, i;

	if (!VECTOR_SIZE(*vec)) {
		if ((rc = append_strbuf_str(buff, "        <empty>\n")) < 0)
			return rc;
	} else vector_foreach_slot (*vec, ble, i) {
		rc = print_strbuf(buff, "        %s %s\n",
				   ble->origin == ORIGIN_CONFIG ?
				   "(config file rule)" :
				   "(default rule)    ", ble->str);
		if (rc < 0)
			return rc;
	}

	return get_strbuf_len(buff) - initial_len;
}

static int
snprint_blacklist_devgroup (struct strbuf *buff, vector *vec)
{
	struct blentry_device * bled;
	size_t initial_len = get_strbuf_len(buff);
	int rc, i;

	if (!VECTOR_SIZE(*vec)) {
		if ((rc = append_strbuf_str(buff, "        <empty>\n")) < 0)
			return rc;
	} else vector_foreach_slot (*vec, bled, i) {
		rc = print_strbuf(buff, "        %s %s:%s\n",
				  bled->origin == ORIGIN_CONFIG ?
				  "(config file rule)" :
				  "(default rule)    ",
				  bled->vendor, bled->product);
		if (rc < 0)
			return rc;
	}

	return get_strbuf_len(buff) - initial_len;
}

int snprint_blacklist_report(struct config *conf, struct strbuf *buff)
{
	size_t initial_len = get_strbuf_len(buff);
	int rc;

	if ((rc = append_strbuf_str(buff, "device node rules:\n- blacklist:\n")) < 0)
		return rc;
	if ((rc = snprint_blacklist_group(buff, &conf->blist_devnode)) < 0)
		return rc;

	if ((rc = append_strbuf_str(buff, "- exceptions:\n")) < 0)
		return rc;
	if ((rc = snprint_blacklist_group(buff, &conf->elist_devnode)) < 0)
		return rc;

	if ((rc = append_strbuf_str(buff, "udev property rules:\n- blacklist:\n")) < 0)
		return rc;
	if ((rc = snprint_blacklist_group(buff, &conf->blist_property)) < 0)
		return rc;

	if ((rc = append_strbuf_str(buff, "- exceptions:\n")) < 0)
		return rc;
	if ((rc = snprint_blacklist_group(buff, &conf->elist_property)) < 0)
		return rc;

	if ((rc = append_strbuf_str(buff, "protocol rules:\n- blacklist:\n")) < 0)
		return rc;
	if ((rc = snprint_blacklist_group(buff, &conf->blist_protocol)) < 0)
		return rc;

	if ((rc = append_strbuf_str(buff, "- exceptions:\n")) < 0)
		return rc;
	if ((rc = snprint_blacklist_group(buff, &conf->elist_protocol)) < 0)
		return rc;

	if ((rc = append_strbuf_str(buff, "wwid rules:\n- blacklist:\n")) < 0)
		return rc;
	if ((rc = snprint_blacklist_group(buff, &conf->blist_wwid)) < 0)
		return rc;

	if ((rc = append_strbuf_str(buff, "- exceptions:\n")) < 0)
		return rc;
	if ((rc = snprint_blacklist_group(buff, &conf->elist_wwid)) < 0)
		return rc;

	if ((rc = append_strbuf_str(buff, "device rules:\n- blacklist:\n")) < 0)
		return rc;
	if ((rc = snprint_blacklist_devgroup(buff, &conf->blist_device)) < 0)
		return rc;

	if ((rc = append_strbuf_str(buff, "- exceptions:\n")) < 0)
	     return rc;
	if ((rc = snprint_blacklist_devgroup(buff, &conf->elist_device)) < 0)
		return rc;

	return get_strbuf_len(buff) - initial_len;
}

static int snprint_blacklist(const struct config *conf, struct strbuf *buff)
{
	int i, rc;
	struct blentry * ble;
	struct blentry_device * bled;
	struct keyword *rootkw;
	struct keyword *kw, *pkw;
	size_t initial_len = get_strbuf_len(buff);

	rootkw = find_keyword(conf->keywords, NULL, "blacklist");
	assert(rootkw);

	if ((rc = append_strbuf_str(buff, "blacklist {\n")) < 0)
		return rc;

	vector_foreach_slot (conf->blist_devnode, ble, i) {
		kw = find_keyword(conf->keywords, rootkw->sub, "devnode");
		assert(kw);
		if ((rc = snprint_keyword(buff, "\t%k %v\n", kw, ble)) < 0)
			return rc;
	}
	vector_foreach_slot (conf->blist_wwid, ble, i) {
		kw = find_keyword(conf->keywords, rootkw->sub, "wwid");
		assert(kw);
		if ((rc = snprint_keyword(buff, "\t%k %v\n", kw, ble)) < 0)
			return rc;
	}
	vector_foreach_slot (conf->blist_property, ble, i) {
		kw = find_keyword(conf->keywords, rootkw->sub, "property");
		assert(kw);
		if ((rc = snprint_keyword(buff, "\t%k %v\n", kw, ble)) < 0)
			return rc;
	}
	vector_foreach_slot (conf->blist_protocol, ble, i) {
		kw = find_keyword(conf->keywords, rootkw->sub, "protocol");
		assert(kw);
		if ((rc = snprint_keyword(buff, "\t%k %v\n", kw, ble)) < 0)
			return rc;
	}

	rootkw = find_keyword(conf->keywords, rootkw->sub, "device");
	assert(rootkw);
	kw = find_keyword(conf->keywords, rootkw->sub, "vendor");
	pkw = find_keyword(conf->keywords, rootkw->sub, "product");
	assert(kw && pkw);

	vector_foreach_slot (conf->blist_device, bled, i) {
		if ((rc = snprint_keyword(buff, "\tdevice {\n\t\t%k %v\n",
					  kw, bled)) < 0)
			return rc;
		if ((rc = snprint_keyword(buff, "\t\t%k %v\n\t}\n",
					  pkw, bled)) < 0)
			return rc;
	}

	if ((rc = append_strbuf_str(buff, "}\n")) < 0)
		return rc;
	return get_strbuf_len(buff) - initial_len;
}

static int snprint_blacklist_except(const struct config *conf,
				    struct strbuf *buff)
{
	int i, rc;
	struct blentry * ele;
	struct blentry_device * eled;
	struct keyword *rootkw;
	struct keyword *kw, *pkw;
	size_t initial_len = get_strbuf_len(buff);

	rootkw = find_keyword(conf->keywords, NULL, "blacklist_exceptions");
	assert(rootkw);

	if ((rc = append_strbuf_str(buff, "blacklist_exceptions {\n")) < 0)
		return rc;

	vector_foreach_slot (conf->elist_devnode, ele, i) {
		kw = find_keyword(conf->keywords, rootkw->sub, "devnode");
		assert(kw);
		if ((rc = snprint_keyword(buff, "\t%k %v\n", kw, ele)) < 0)
			return rc;
	}
	vector_foreach_slot (conf->elist_wwid, ele, i) {
		kw = find_keyword(conf->keywords, rootkw->sub, "wwid");
		assert(kw);
		if ((rc = snprint_keyword(buff, "\t%k %v\n", kw, ele)) < 0)
			return rc;
	}
	vector_foreach_slot (conf->elist_property, ele, i) {
		kw = find_keyword(conf->keywords, rootkw->sub, "property");
		assert(kw);
		if ((rc = snprint_keyword(buff, "\t%k %v\n", kw, ele)) < 0)
			return rc;
	}
	vector_foreach_slot (conf->elist_protocol, ele, i) {
		kw = find_keyword(conf->keywords, rootkw->sub, "protocol");
		assert(kw);
		if ((rc = snprint_keyword(buff, "\t%k %v\n", kw, ele)) < 0)
			return rc;
	}

	rootkw = find_keyword(conf->keywords, rootkw->sub, "device");
	assert(rootkw);
	kw = find_keyword(conf->keywords, rootkw->sub, "vendor");
	pkw = find_keyword(conf->keywords, rootkw->sub, "product");
	assert(kw && pkw);

	vector_foreach_slot (conf->elist_device, eled, i) {
		if ((rc = snprint_keyword(buff, "\tdevice {\n\t\t%k %v\n",
					  kw, eled)) < 0)
			return rc;
		if ((rc = snprint_keyword(buff, "\t\t%k %v\n\t}\n",
					  pkw, eled)) < 0)
			return rc;
	}

	if ((rc = append_strbuf_str(buff, "}\n")) < 0)
		return rc;
	return get_strbuf_len(buff) - initial_len;
}

int __snprint_config(const struct config *conf, struct strbuf *buff,
		     const struct _vector *hwtable, const struct _vector *mpvec)
{
	int rc;

	if ((rc = snprint_defaults(conf, buff)) < 0 ||
	    (rc = snprint_blacklist(conf, buff)) < 0 ||
	    (rc = snprint_blacklist_except(conf, buff)) < 0 ||
	    (rc = snprint_hwtable(conf, buff,
				  hwtable ? hwtable : conf->hwtable)) < 0 ||
	    (rc = snprint_overrides(conf, buff, conf->overrides)) < 0)
		return rc;

	if (VECTOR_SIZE(conf->mptable) > 0 ||
	    (mpvec != NULL && VECTOR_SIZE(mpvec) > 0))
		if ((rc = snprint_mptable(conf, buff, mpvec)) < 0)
			return rc;

	return 0;
}

char *snprint_config(const struct config *conf, int *len,
		     const struct _vector *hwtable, const struct _vector *mpvec)
{
	STRBUF_ON_STACK(buff);
	char *reply;
	int rc = __snprint_config(conf, &buff, hwtable, mpvec);

	if (rc < 0)
		return NULL;

	if (len)
		*len = get_strbuf_len(&buff);
	reply = steal_strbuf_str(&buff);

	return reply;
}

int snprint_status(struct strbuf *buff, const struct vectors *vecs)
{
	int i, rc;
	unsigned int count[PATH_MAX_STATE] = {0};
	int monitored_count = 0;
	struct path * pp;
	size_t initial_len = get_strbuf_len(buff);

	vector_foreach_slot (vecs->pathvec, pp, i) {
		count[pp->state]++;
	}
	if ((rc = append_strbuf_str(buff, "path checker states:\n")) < 0)
		return rc;
	for (i = 0; i < PATH_MAX_STATE; i++) {
		if (!count[i])
			continue;
		if ((rc = print_strbuf(buff, "%-20s%u\n",
				       checker_state_name(i), count[i])) < 0)
			return rc;
	}

	vector_foreach_slot(vecs->pathvec, pp, i)
		if (pp->fd >= 0)
			monitored_count++;
	if ((rc = print_strbuf(buff, "\npaths: %d\nbusy: %s\n",
			       monitored_count,
			       is_uevent_busy()? "True" : "False")) < 0)
		return rc;

	return get_strbuf_len(buff) - initial_len;
}

int snprint_devices(struct config *conf, struct strbuf *buff,
		    const struct vectors *vecs)
{
	int r;
	struct udev_enumerate *enm;
	struct udev_list_entry *item, *first;
	struct path * pp;
	size_t initial_len = get_strbuf_len(buff);

	enm = udev_enumerate_new(udev);
	if (!enm)
		return 1;
	udev_enumerate_add_match_subsystem(enm, "block");

	if ((r = append_strbuf_str(buff, "available block devices:\n")) < 0)
		goto out;
	r = udev_enumerate_scan_devices(enm);
	if (r < 0)
		goto out;

	first = udev_enumerate_get_list_entry(enm);
	udev_list_entry_foreach(item, first) {
		const char *path, *devname, *status;
		struct udev_device *u_dev;

		path = udev_list_entry_get_name(item);
		if (!path)
			continue;
		u_dev = udev_device_new_from_syspath(udev, path);
		if (!u_dev)
			continue;
		devname = udev_device_get_sysname(u_dev);
		if (!devname) {
			udev_device_unref(u_dev);
			continue;
		}

		pp = find_path_by_dev(vecs->pathvec, devname);
		if (!pp) {
			const char *hidden;

			hidden = udev_device_get_sysattr_value(u_dev,
							       "hidden");
			if (hidden && !strcmp(hidden, "1"))
				status = "hidden, unmonitored";
			else if (is_claimed_by_foreign(u_dev))
				status = "foreign, monitored";
			else {
				r = filter_devnode(conf->blist_devnode,
						   conf->elist_devnode,
						   devname);
				if (r > 0)
					status = "devnode blacklisted, unmonitored";
				else
					status = "devnode whitelisted, unmonitored";
			}
		} else
			status = " devnode whitelisted, monitored";

		r = print_strbuf(buff, "    %s %s\n", devname, status);
		udev_device_unref(u_dev);
		if (r < 0)
			break;
	}
out:
	udev_enumerate_unref(enm);
	if (r < 0)
		return r;

	return get_strbuf_len(buff) - initial_len;
}

/*
 * stdout printing helpers
 */
static void print_all_paths_custo(vector pathvec, int banner, const char *fmt)
{
	int i;
	struct path * pp;
	STRBUF_ON_STACK(line);

	if (!VECTOR_SIZE(pathvec)) {
		if (banner)
			fprintf(stdout, "===== no paths =====\n");
		return;
	}

	if (banner)
		append_strbuf_str(&line, "===== paths list =====\n");

	get_path_layout(pathvec, 1);
	snprint_path_header(&line, fmt);

	vector_foreach_slot (pathvec, pp, i)
		snprint_path(&line, fmt, pp, 1);

	printf("%s", get_strbuf_str(&line));
}

void print_all_paths(vector pathvec, int banner)
{
	print_all_paths_custo(pathvec, banner, PRINT_PATH_LONG);
}
