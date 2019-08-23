/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 * Copyright (c) 2005 Benjamin Marzinski, Redhat
 * Copyright (c) 2005 Kiyoshi Ueda, NEC
 */
#include <stdio.h>

#include "nvme-lib.h"
#include "checkers.h"
#include "memory.h"
#include "vector.h"
#include "structs.h"
#include "config.h"
#include "debug.h"
#include "pgpolicies.h"
#include "alias.h"
#include "defaults.h"
#include "devmapper.h"
#include "prio.h"
#include "discovery.h"
#include "dict.h"
#include "util.h"
#include "sysfs.h"
#include "prioritizers/alua_rtpg.h"
#include "prkey.h"
#include "propsel.h"
#include <inttypes.h>
#include <libudev.h>

pgpolicyfn *pgpolicies[] = {
	NULL,
	one_path_per_group,
	one_group,
	group_by_serial,
	group_by_prio,
	group_by_node_name
};

#define do_set(var, src, dest, msg)					\
do {									\
	if (src && src->var) {						\
		dest = src->var;					\
		origin = msg;						\
		goto out;						\
	}								\
} while(0)

#define __do_set_from_vec(type, var, src, dest)				\
({									\
	type *_p;							\
	bool _found = false;						\
	int i;								\
									\
	vector_foreach_slot(src, _p, i) {				\
		if (_p->var) {						\
			dest = _p->var;					\
			_found = true;					\
			break;						\
		}							\
	}								\
	_found;								\
})

#define __do_set_from_hwe(var, src, dest) \
	__do_set_from_vec(struct hwentry, var, (src)->hwe, dest)

#define do_set_from_hwe(var, src, dest, msg)				\
	if (__do_set_from_hwe(var, src, dest)) {			\
		origin = msg;						\
		goto out;						\
	}

static const char default_origin[] = "(setting: multipath internal)";
static const char hwe_origin[] =
	"(setting: storage device configuration)";
static const char multipaths_origin[] =
	"(setting: multipath.conf multipaths section)";
static const char conf_origin[] =
	"(setting: multipath.conf defaults/devices section)";
static const char overrides_origin[] =
	"(setting: multipath.conf overrides section)";
static const char cmdline_origin[] =
	"(setting: multipath command line [-p] flag)";
static const char autodetect_origin[] =
	"(setting: storage device autodetected)";
static const char marginal_path_origin[] =
	"(setting: implied by marginal_path check)";
static const char delay_watch_origin[] =
	"(setting: implied by delay_watch_checks)";
static const char delay_wait_origin[] =
	"(setting: implied by delay_wait_checks)";

#define do_default(dest, value)						\
do {									\
	dest = value;							\
	origin = default_origin;					\
} while(0)

#define mp_set_mpe(var)							\
do_set(var, mp->mpe, mp->var, multipaths_origin)
#define mp_set_hwe(var)							\
do_set_from_hwe(var, mp, mp->var, hwe_origin)
#define mp_set_ovr(var)							\
do_set(var, conf->overrides, mp->var, overrides_origin)
#define mp_set_conf(var)						\
do_set(var, conf, mp->var, conf_origin)
#define mp_set_default(var, value)					\
do_default(mp->var, value)

#define pp_set_mpe(var)							\
do_set(var, mpe, pp->var, multipaths_origin)
#define pp_set_hwe(var)							\
do_set_from_hwe(var, pp, pp->var, hwe_origin)
#define pp_set_conf(var)						\
do_set(var, conf, pp->var, conf_origin)
#define pp_set_ovr(var)							\
do_set(var, conf->overrides, pp->var, overrides_origin)
#define pp_set_default(var, value)					\
do_default(pp->var, value)

#define do_attr_set(var, src, shift, msg)				\
do {									\
	if (src && (src->attribute_flags & (1 << shift))) {		\
		mp->attribute_flags |= (1 << shift);			\
		mp->var = src->var;					\
		origin = msg;						\
		goto out;						\
	}								\
} while(0)

#define set_attr_mpe(var, shift)					\
do_attr_set(var, mp->mpe, shift, "(setting: multipath.conf multipaths section)")
#define set_attr_conf(var, shift)					\
do_attr_set(var, conf, shift, "(setting: multipath.conf defaults/devices section)")

#define do_prkey_set(src, msg)						\
do {									\
	if (src && src->prkey_source != PRKEY_SOURCE_NONE) {		\
		mp->prkey_source = src->prkey_source;			\
		mp->reservation_key = src->reservation_key;		\
		mp->sa_flags = src->sa_flags;				\
		origin = msg;						\
		goto out;						\
	}								\
} while (0)

int select_mode(struct config *conf, struct multipath *mp)
{
	const char *origin;

	set_attr_mpe(mode, ATTR_MODE);
	set_attr_conf(mode, ATTR_MODE);
	mp->attribute_flags &= ~(1 << ATTR_MODE);
	return 0;
out:
	condlog(3, "%s: mode = 0%o %s", mp->alias, mp->mode, origin);
	return 0;
}

int select_uid(struct config *conf, struct multipath *mp)
{
	const char *origin;

	set_attr_mpe(uid, ATTR_UID);
	set_attr_conf(uid, ATTR_UID);
	mp->attribute_flags &= ~(1 << ATTR_UID);
	return 0;
out:
	condlog(3, "%s: uid = 0%o %s", mp->alias, mp->uid, origin);
	return 0;
}

int select_gid(struct config *conf, struct multipath *mp)
{
	const char *origin;

	set_attr_mpe(gid, ATTR_GID);
	set_attr_conf(gid, ATTR_GID);
	mp->attribute_flags &= ~(1 << ATTR_GID);
	return 0;
out:
	condlog(3, "%s: gid = 0%o %s", mp->alias, mp->gid, origin);
	return 0;
}

/*
 * selectors :
 * traverse the configuration layers from most specific to most generic
 * stop at first explicit setting found
 */
int select_rr_weight(struct config *conf, struct multipath * mp)
{
	const char *origin;
	char buff[13];

	mp_set_mpe(rr_weight);
	mp_set_ovr(rr_weight);
	mp_set_hwe(rr_weight);
	mp_set_conf(rr_weight);
	mp_set_default(rr_weight, DEFAULT_RR_WEIGHT);
out:
	print_rr_weight(buff, 13, mp->rr_weight);
	condlog(3, "%s: rr_weight = %s %s", mp->alias, buff, origin);
	return 0;
}

int select_pgfailback(struct config *conf, struct multipath * mp)
{
	const char *origin;
	char buff[13];

	mp_set_mpe(pgfailback);
	mp_set_ovr(pgfailback);
	mp_set_hwe(pgfailback);
	mp_set_conf(pgfailback);
	mp_set_default(pgfailback, DEFAULT_FAILBACK);
out:
	print_pgfailback(buff, 13, mp->pgfailback);
	condlog(3, "%s: failback = %s %s", mp->alias, buff, origin);
	return 0;
}

int select_pgpolicy(struct config *conf, struct multipath * mp)
{
	const char *origin;
	char buff[POLICY_NAME_SIZE];

	if (conf->pgpolicy_flag > 0) {
		mp->pgpolicy = conf->pgpolicy_flag;
		origin = cmdline_origin;
		goto out;
	}
	mp_set_mpe(pgpolicy);
	mp_set_ovr(pgpolicy);
	mp_set_hwe(pgpolicy);
	mp_set_conf(pgpolicy);
	mp_set_default(pgpolicy, DEFAULT_PGPOLICY);
out:
	mp->pgpolicyfn = pgpolicies[mp->pgpolicy];
	get_pgpolicy_name(buff, POLICY_NAME_SIZE, mp->pgpolicy);
	condlog(3, "%s: path_grouping_policy = %s %s", mp->alias, buff, origin);
	return 0;
}

int select_selector(struct config *conf, struct multipath * mp)
{
	const char *origin;

	mp_set_mpe(selector);
	mp_set_ovr(selector);
	mp_set_hwe(selector);
	mp_set_conf(selector);
	mp_set_default(selector, DEFAULT_SELECTOR);
out:
	mp->selector = STRDUP(mp->selector);
	condlog(3, "%s: path_selector = \"%s\" %s", mp->alias, mp->selector,
		origin);
	return 0;
}

static void
select_alias_prefix (struct config *conf, struct multipath * mp)
{
	const char *origin;

	mp_set_ovr(alias_prefix);
	mp_set_hwe(alias_prefix);
	mp_set_conf(alias_prefix);
	mp_set_default(alias_prefix, DEFAULT_ALIAS_PREFIX);
out:
	condlog(3, "%s: alias_prefix = %s %s", mp->wwid, mp->alias_prefix,
		origin);
}

static int
want_user_friendly_names(struct config *conf, struct multipath * mp)
{

	const char *origin;
	int user_friendly_names;

	do_set(user_friendly_names, mp->mpe, user_friendly_names,
	       multipaths_origin);
	do_set(user_friendly_names, conf->overrides, user_friendly_names,
	       overrides_origin);
	do_set_from_hwe(user_friendly_names, mp, user_friendly_names,
			hwe_origin);
	do_set(user_friendly_names, conf, user_friendly_names,
	       conf_origin);
	do_default(user_friendly_names, DEFAULT_USER_FRIENDLY_NAMES);
out:
	condlog(3, "%s: user_friendly_names = %s %s", mp->wwid,
		(user_friendly_names == USER_FRIENDLY_NAMES_ON)? "yes" : "no",
		origin);
	return (user_friendly_names == USER_FRIENDLY_NAMES_ON);
}

int select_alias(struct config *conf, struct multipath * mp)
{
	const char *origin = NULL;

	if (mp->mpe && mp->mpe->alias) {
		mp->alias = STRDUP(mp->mpe->alias);
		origin = multipaths_origin;
		goto out;
	}

	mp->alias = NULL;
	if (!want_user_friendly_names(conf, mp))
		goto out;

	select_alias_prefix(conf, mp);

	if (strlen(mp->alias_old) > 0) {
		mp->alias = use_existing_alias(mp->wwid, conf->bindings_file,
				mp->alias_old, mp->alias_prefix,
				conf->bindings_read_only);
		memset (mp->alias_old, 0, WWID_SIZE);
		origin = "(setting: using existing alias)";
	}

	if (mp->alias == NULL) {
		mp->alias = get_user_friendly_alias(mp->wwid,
				conf->bindings_file, mp->alias_prefix, conf->bindings_read_only);
		origin = "(setting: user_friendly_name)";
	}
out:
	if (mp->alias == NULL) {
		mp->alias = STRDUP(mp->wwid);
		origin = "(setting: default to WWID)";
	}
	if (mp->alias)
		condlog(3, "%s: alias = %s %s", mp->wwid, mp->alias, origin);
	return mp->alias ? 0 : 1;
}

void reconcile_features_with_options(const char *id, char **features, int* no_path_retry,
		  int *retain_hwhandler)
{
	static const char q_i_n_p[] = "queue_if_no_path";
	static const char r_a_h_h[] = "retain_attached_hw_handler";
	char buff[12];

	if (*features == NULL)
		return;
	if (id == NULL)
		id = "UNKNOWN";

	/*
	 * We only use no_path_retry internally. The "queue_if_no_path"
	 * device-mapper feature is derived from it when the map is loaded.
	 * For consistency, "queue_if_no_path" is removed from the
	 * internal libmultipath features string.
	 * For backward compatibility we allow 'features "1 queue_if_no_path"';
	 * it's translated into "no_path_retry queue" here.
	 */
	if (strstr(*features, q_i_n_p)) {
		condlog(0, "%s: option 'features \"1 %s\"' is deprecated, "
			"please use 'no_path_retry queue' instead",
			id, q_i_n_p);
		if (*no_path_retry == NO_PATH_RETRY_UNDEF) {
			*no_path_retry = NO_PATH_RETRY_QUEUE;
			print_no_path_retry(buff, sizeof(buff),
					    *no_path_retry);
			condlog(3, "%s: no_path_retry = %s (inherited setting from feature '%s')",
				id, buff, q_i_n_p);
		};
		/* Warn only if features string is overridden */
		if (*no_path_retry != NO_PATH_RETRY_QUEUE) {
			print_no_path_retry(buff, sizeof(buff),
					    *no_path_retry);
			condlog(2, "%s: ignoring feature '%s' because no_path_retry is set to '%s'",
				id, q_i_n_p, buff);
		}
		remove_feature(features, q_i_n_p);
	}
	if (strstr(*features, r_a_h_h)) {
		condlog(0, "%s: option 'features \"1 %s\"' is deprecated",
			id, r_a_h_h);
		if (*retain_hwhandler == RETAIN_HWHANDLER_UNDEF) {
			condlog(3, "%s: %s = on (inherited setting from feature '%s')",
				id, r_a_h_h, r_a_h_h);
			*retain_hwhandler = RETAIN_HWHANDLER_ON;
		} else if (*retain_hwhandler == RETAIN_HWHANDLER_OFF)
			condlog(2, "%s: ignoring feature '%s' because %s is set to 'off'",
				id, r_a_h_h, r_a_h_h);
		remove_feature(features, r_a_h_h);
	}
}

int select_features(struct config *conf, struct multipath *mp)
{
	const char *origin;

	mp_set_mpe(features);
	mp_set_ovr(features);
	mp_set_hwe(features);
	mp_set_conf(features);
	mp_set_default(features, DEFAULT_FEATURES);
out:
	mp->features = STRDUP(mp->features);

	reconcile_features_with_options(mp->alias, &mp->features,
					&mp->no_path_retry,
					&mp->retain_hwhandler);
	condlog(3, "%s: features = \"%s\" %s", mp->alias, mp->features, origin);
	return 0;
}

static int get_dh_state(struct path *pp, char *value, size_t value_len)
{
	struct udev_device *ud;

	if (pp->udev == NULL)
		return -1;

	ud = udev_device_get_parent_with_subsystem_devtype(
		pp->udev, "scsi", "scsi_device");
	if (ud == NULL)
		return -1;

	return sysfs_attr_get_value(ud, "dh_state", value, value_len);
}

int select_hwhandler(struct config *conf, struct multipath *mp)
{
	const char *origin;
	struct path *pp;
	/* dh_state is no longer than "detached" */
	char handler[12];
	static char alua_name[] = "1 alua";
	static const char tpgs_origin[]= "(setting: autodetected from TPGS)";
	char *dh_state;
	int i;
	bool all_tpgs = true;

	dh_state = &handler[2];

	vector_foreach_slot(mp->paths, pp, i)
		all_tpgs = all_tpgs && (path_get_tpgs(pp) > 0);
	if (mp->retain_hwhandler != RETAIN_HWHANDLER_OFF) {
		vector_foreach_slot(mp->paths, pp, i) {
			if (get_dh_state(pp, dh_state, sizeof(handler) - 2) > 0
			    && strcmp(dh_state, "detached")) {
				memcpy(handler, "1 ", 2);
				mp->hwhandler = handler;
				origin = "(setting: retained by kernel driver)";
				goto out;
			}
		}
	}

	mp_set_hwe(hwhandler);
	mp_set_conf(hwhandler);
	mp_set_default(hwhandler, DEFAULT_HWHANDLER);
out:
	if (all_tpgs && !strcmp(mp->hwhandler, DEFAULT_HWHANDLER) &&
		origin == default_origin) {
		mp->hwhandler = alua_name;
		origin = tpgs_origin;
	} else if (!all_tpgs && !strcmp(mp->hwhandler, alua_name)) {
		mp->hwhandler = DEFAULT_HWHANDLER;
		origin = tpgs_origin;
	}
	mp->hwhandler = STRDUP(mp->hwhandler);
	condlog(3, "%s: hardware_handler = \"%s\" %s", mp->alias, mp->hwhandler,
		origin);
	return 0;
}

/*
 * Current RDAC (NetApp E-Series) firmware relies
 * on periodic REPORT TARGET PORT GROUPS for
 * internal load balancing.
 * Using the sysfs priority checker defeats this purpose.
 *
 * Moreover, NetApp would also prefer the RDAC checker over ALUA.
 * (https://www.redhat.com/archives/dm-devel/2017-September/msg00326.html)
 */
static int
check_rdac(struct path * pp)
{
	int len;
	char buff[44];
	const char *checker_name;

	if (pp->bus != SYSFS_BUS_SCSI)
		return 0;
	/* Avoid ioctl if this is likely not an RDAC array */
	if (__do_set_from_hwe(checker_name, pp, checker_name) &&
	    strcmp(checker_name, RDAC))
		return 0;
	len = get_vpd_sgio(pp->fd, 0xC9, buff, 44);
	if (len <= 0)
		return 0;
	return !(memcmp(buff + 4, "vac1", 4));
}

int select_checker(struct config *conf, struct path *pp)
{
	const char *origin;
	char *ckr_name;
	struct checker * c = &pp->checker;

	if (pp->detect_checker == DETECT_CHECKER_ON) {
		origin = autodetect_origin;
		if (check_rdac(pp)) {
			ckr_name = RDAC;
			goto out;
		} else if (path_get_tpgs(pp) != TPGS_NONE) {
			ckr_name = TUR;
			goto out;
		}
	}
	do_set(checker_name, conf->overrides, ckr_name, overrides_origin);
	do_set_from_hwe(checker_name, pp, ckr_name, hwe_origin);
	do_set(checker_name, conf, ckr_name, conf_origin);
	do_default(ckr_name, DEFAULT_CHECKER);
out:
	checker_get(conf->multipath_dir, c, ckr_name);
	condlog(3, "%s: path_checker = %s %s", pp->dev,
		checker_name(c), origin);
	if (conf->checker_timeout) {
		c->timeout = conf->checker_timeout;
		condlog(3, "%s: checker timeout = %u s %s",
			pp->dev, c->timeout, conf_origin);
	}
	else if (sysfs_get_timeout(pp, &c->timeout) > 0)
		condlog(3, "%s: checker timeout = %u s (setting: kernel sysfs)",
			pp->dev, c->timeout);
	else {
		c->timeout = DEF_TIMEOUT;
		condlog(3, "%s: checker timeout = %u s %s",
			pp->dev, c->timeout, default_origin);
	}
	return 0;
}

int select_getuid(struct config *conf, struct path *pp)
{
	const char *origin;

	pp->uid_attribute = get_uid_attribute_by_attrs(conf, pp->dev);
	if (pp->uid_attribute) {
		origin = "(setting: multipath.conf defaults section / uid_attrs)";
		goto out;
	}

	pp_set_ovr(getuid);
	pp_set_ovr(uid_attribute);
	pp_set_hwe(getuid);
	pp_set_hwe(uid_attribute);
	pp_set_conf(getuid);
	pp_set_conf(uid_attribute);
	pp_set_default(uid_attribute, DEFAULT_UID_ATTRIBUTE);
out:
	if (pp->uid_attribute)
		condlog(3, "%s: uid_attribute = %s %s", pp->dev,
			pp->uid_attribute, origin);
	else if (pp->getuid)
		condlog(3, "%s: getuid = \"%s\" %s", pp->dev, pp->getuid,
			origin);
	return 0;
}

void
detect_prio(struct config *conf, struct path * pp)
{
	struct prio *p = &pp->prio;
	char buff[512];
	char *default_prio;
	int tpgs;

	switch(pp->bus) {
	case SYSFS_BUS_NVME:
		if (nvme_id_ctrl_ana(pp->fd, NULL) == 0)
			return;
		default_prio = PRIO_ANA;
		break;
	case SYSFS_BUS_SCSI:
		tpgs = path_get_tpgs(pp);
		if (tpgs == TPGS_NONE)
			return;
		if ((tpgs == TPGS_EXPLICIT || !check_rdac(pp)) &&
		    sysfs_get_asymmetric_access_state(pp, buff, 512) >= 0)
			default_prio = PRIO_SYSFS;
		else
			default_prio = PRIO_ALUA;
		break;
	default:
		return;
	}
	prio_get(conf->multipath_dir, p, default_prio, DEFAULT_PRIO_ARGS);
}

#define set_prio(dir, src, msg)					\
do {									\
	if (src && src->prio_name) {					\
		prio_get(dir, p, src->prio_name, src->prio_args);	\
		origin = msg;						\
		goto out;						\
	}								\
} while(0)

#define set_prio_from_vec(type, dir, src, msg, p)			\
do {									\
	type *_p;							\
	int i;								\
	char *prio_name = NULL, *prio_args = NULL;			\
									\
	vector_foreach_slot(src, _p, i) {				\
		if (prio_name == NULL && _p->prio_name)		\
			prio_name = _p->prio_name;			\
		if (prio_args == NULL && _p->prio_args)		\
			prio_args = _p->prio_args;			\
	}								\
	if (prio_name != NULL) {					\
		prio_get(dir, p, prio_name, prio_args);			\
		origin = msg;						\
		goto out;						\
	}								\
} while (0)

int select_prio(struct config *conf, struct path *pp)
{
	const char *origin;
	struct mpentry * mpe;
	struct prio * p = &pp->prio;
	int log_prio = 3;

	if (pp->detect_prio == DETECT_PRIO_ON) {
		detect_prio(conf, pp);
		if (prio_selected(p)) {
			origin = autodetect_origin;
			goto out;
		}
	}
	mpe = find_mpe(conf->mptable, pp->wwid);
	set_prio(conf->multipath_dir, mpe, multipaths_origin);
	set_prio(conf->multipath_dir, conf->overrides, overrides_origin);
	set_prio_from_vec(struct hwentry, conf->multipath_dir,
			  pp->hwe, hwe_origin, p);
	set_prio(conf->multipath_dir, conf, conf_origin);
	prio_get(conf->multipath_dir, p, DEFAULT_PRIO, DEFAULT_PRIO_ARGS);
	origin = default_origin;
out:
	/*
	 * fetch tpgs mode for alua, if its not already obtained
	 */
	if (!strncmp(prio_name(p), PRIO_ALUA, PRIO_NAME_LEN)) {
		int tpgs = path_get_tpgs(pp);

		if (tpgs == TPGS_NONE) {
			prio_get(conf->multipath_dir,
				 p, DEFAULT_PRIO, DEFAULT_PRIO_ARGS);
			origin = "(setting: emergency fallback - alua failed)";
			log_prio = 1;
		}
	}
	condlog(log_prio, "%s: prio = %s %s", pp->dev, prio_name(p), origin);
	condlog(3, "%s: prio args = \"%s\" %s", pp->dev, prio_args(p), origin);
	return 0;
}

int select_no_path_retry(struct config *conf, struct multipath *mp)
{
	const char *origin = NULL;
	char buff[12];

	if (mp->disable_queueing) {
		condlog(0, "%s: queueing disabled", mp->alias);
		mp->no_path_retry = NO_PATH_RETRY_FAIL;
		return 0;
	}
	mp_set_mpe(no_path_retry);
	mp_set_ovr(no_path_retry);
	mp_set_hwe(no_path_retry);
	mp_set_conf(no_path_retry);
out:
	print_no_path_retry(buff, 12, mp->no_path_retry);
	if (origin)
		condlog(3, "%s: no_path_retry = %s %s", mp->alias, buff,
			origin);
	else
		condlog(3, "%s: no_path_retry = undef %s",
			mp->alias, default_origin);
	return 0;
}

int
select_minio_rq (struct config *conf, struct multipath * mp)
{
	const char *origin;

	do_set(minio_rq, mp->mpe, mp->minio, multipaths_origin);
	do_set(minio_rq, conf->overrides, mp->minio, overrides_origin);
	do_set_from_hwe(minio_rq, mp, mp->minio, hwe_origin);
	do_set(minio_rq, conf, mp->minio, conf_origin);
	do_default(mp->minio, DEFAULT_MINIO_RQ);
out:
	condlog(3, "%s: minio = %i %s", mp->alias, mp->minio, origin);
	return 0;
}

int
select_minio_bio (struct config *conf, struct multipath * mp)
{
	const char *origin;

	mp_set_mpe(minio);
	mp_set_ovr(minio);
	mp_set_hwe(minio);
	mp_set_conf(minio);
	mp_set_default(minio, DEFAULT_MINIO);
out:
	condlog(3, "%s: minio = %i %s", mp->alias, mp->minio, origin);
	return 0;
}

int select_minio(struct config *conf, struct multipath *mp)
{
	unsigned int minv_dmrq[3] = {1, 1, 0};

	if (VERSION_GE(conf->version, minv_dmrq))
		return select_minio_rq(conf, mp);
	else
		return select_minio_bio(conf, mp);
}

int select_fast_io_fail(struct config *conf, struct multipath *mp)
{
	const char *origin;
	char buff[12];

	mp_set_ovr(fast_io_fail);
	mp_set_hwe(fast_io_fail);
	mp_set_conf(fast_io_fail);
	mp_set_default(fast_io_fail, DEFAULT_FAST_IO_FAIL);
out:
	print_fast_io_fail(buff, 12, mp->fast_io_fail);
	condlog(3, "%s: fast_io_fail_tmo = %s %s", mp->alias, buff, origin);
	return 0;
}

int select_dev_loss(struct config *conf, struct multipath *mp)
{
	const char *origin;
	char buff[12];

	mp_set_ovr(dev_loss);
	mp_set_hwe(dev_loss);
	mp_set_conf(dev_loss);
	mp->dev_loss = 0;
	return 0;
out:
	print_dev_loss(buff, 12, mp->dev_loss);
	condlog(3, "%s: dev_loss_tmo = %s %s", mp->alias, buff, origin);
	return 0;
}

int select_flush_on_last_del(struct config *conf, struct multipath *mp)
{
	const char *origin;

	mp_set_mpe(flush_on_last_del);
	mp_set_ovr(flush_on_last_del);
	mp_set_hwe(flush_on_last_del);
	mp_set_conf(flush_on_last_del);
	mp_set_default(flush_on_last_del, DEFAULT_FLUSH);
out:
	condlog(3, "%s: flush_on_last_del = %s %s", mp->alias,
		(mp->flush_on_last_del == FLUSH_ENABLED)? "yes" : "no", origin);
	return 0;
}

int select_reservation_key(struct config *conf, struct multipath *mp)
{
	const char *origin;
	char buff[PRKEY_SIZE];
	char *from_file = "";
	uint64_t prkey = 0;

	do_prkey_set(mp->mpe, multipaths_origin);
	do_prkey_set(conf, conf_origin);
	put_be64(mp->reservation_key, 0);
	mp->sa_flags = 0;
	mp->prkey_source = PRKEY_SOURCE_NONE;
	return 0;
out:
	if (mp->prkey_source == PRKEY_SOURCE_FILE) {
		from_file = " (from prkeys file)";
		if (get_prkey(conf, mp, &prkey, &mp->sa_flags) != 0)
			put_be64(mp->reservation_key, 0);
		else
			put_be64(mp->reservation_key, prkey);
	}
	print_reservation_key(buff, PRKEY_SIZE, mp->reservation_key,
			      mp->sa_flags, mp->prkey_source);
	condlog(3, "%s: reservation_key = %s %s%s", mp->alias, buff, origin,
		from_file);
	return 0;
}

int select_retain_hwhandler(struct config *conf, struct multipath *mp)
{
	const char *origin;
	unsigned int minv_dm_retain[3] = {1, 5, 0};

	if (!VERSION_GE(conf->version, minv_dm_retain)) {
		mp->retain_hwhandler = RETAIN_HWHANDLER_OFF;
		origin = "(setting: WARNING, requires kernel dm-mpath version >= 1.5.0)";
		goto out;
	}
	if (get_linux_version_code() >= KERNEL_VERSION(4, 3, 0)) {
		mp->retain_hwhandler = RETAIN_HWHANDLER_ON;
		origin = "(setting: implied in kernel >= 4.3.0)";
		goto out;
	}
	mp_set_ovr(retain_hwhandler);
	mp_set_hwe(retain_hwhandler);
	mp_set_conf(retain_hwhandler);
	mp_set_default(retain_hwhandler, DEFAULT_RETAIN_HWHANDLER);
out:
	condlog(3, "%s: retain_attached_hw_handler = %s %s", mp->alias,
		(mp->retain_hwhandler == RETAIN_HWHANDLER_ON)? "yes" : "no",
		origin);
	return 0;
}

int select_detect_prio(struct config *conf, struct path *pp)
{
	const char *origin;

	pp_set_ovr(detect_prio);
	pp_set_hwe(detect_prio);
	pp_set_conf(detect_prio);
	pp_set_default(detect_prio, DEFAULT_DETECT_PRIO);
out:
	condlog(3, "%s: detect_prio = %s %s", pp->dev,
		(pp->detect_prio == DETECT_PRIO_ON)? "yes" : "no", origin);
	return 0;
}

int select_detect_checker(struct config *conf, struct path *pp)
{
	const char *origin;

	pp_set_ovr(detect_checker);
	pp_set_hwe(detect_checker);
	pp_set_conf(detect_checker);
	pp_set_default(detect_checker, DEFAULT_DETECT_CHECKER);
out:
	condlog(3, "%s: detect_checker = %s %s", pp->dev,
		(pp->detect_checker == DETECT_CHECKER_ON)? "yes" : "no",
		origin);
	return 0;
}

int select_deferred_remove(struct config *conf, struct multipath *mp)
{
	const char *origin;

#ifndef LIBDM_API_DEFERRED
	mp->deferred_remove = DEFERRED_REMOVE_OFF;
	origin = "(setting: WARNING, not compiled with support)";
	goto out;
#endif
	if (mp->deferred_remove == DEFERRED_REMOVE_IN_PROGRESS) {
		condlog(3, "%s: deferred remove in progress", mp->alias);
		return 0;
	}
	mp_set_mpe(deferred_remove);
	mp_set_ovr(deferred_remove);
	mp_set_hwe(deferred_remove);
	mp_set_conf(deferred_remove);
	mp_set_default(deferred_remove, DEFAULT_DEFERRED_REMOVE);
out:
	condlog(3, "%s: deferred_remove = %s %s", mp->alias,
		(mp->deferred_remove == DEFERRED_REMOVE_ON)? "yes" : "no",
		origin);
	return 0;
}

static inline int san_path_check_options_set(const struct multipath *mp)
{
	return mp->san_path_err_threshold > 0 ||
	       mp->san_path_err_forget_rate > 0 ||
	       mp->san_path_err_recovery_time > 0;
}

static int
use_delay_watch_checks(struct config *conf, struct multipath *mp)
{
	int value = NU_UNDEF;
	const char *origin = default_origin;
	char buff[12];

	do_set(delay_watch_checks, mp->mpe, value, multipaths_origin);
	do_set(delay_watch_checks, conf->overrides, value, overrides_origin);
	do_set_from_hwe(delay_watch_checks, mp, value, hwe_origin);
	do_set(delay_watch_checks, conf, value, conf_origin);
out:
	if (print_off_int_undef(buff, 12, value) != 0)
		condlog(3, "%s: delay_watch_checks = %s %s", mp->alias, buff,
			origin);
	return value;
}

static int
use_delay_wait_checks(struct config *conf, struct multipath *mp)
{
	int value = NU_UNDEF;
	const char *origin = default_origin;
	char buff[12];

	do_set(delay_wait_checks, mp->mpe, value, multipaths_origin);
	do_set(delay_wait_checks, conf->overrides, value, overrides_origin);
	do_set_from_hwe(delay_wait_checks, mp, value, hwe_origin);
	do_set(delay_wait_checks, conf, value, conf_origin);
out:
	if (print_off_int_undef(buff, 12, value) != 0)
		condlog(3, "%s: delay_wait_checks = %s %s", mp->alias, buff,
			origin);
	return value;
}

int select_delay_checks(struct config *conf, struct multipath *mp)
{
	int watch_checks, wait_checks;
	char buff[12];

	watch_checks = use_delay_watch_checks(conf, mp);
	wait_checks = use_delay_wait_checks(conf, mp);
	if (watch_checks <= 0 && wait_checks <= 0)
		return 0;
	if (san_path_check_options_set(mp)) {
		condlog(3, "%s: both marginal_path and delay_checks error detection options selected", mp->alias);
		condlog(3, "%s: ignoring delay_checks options", mp->alias);
		return 0;
	}
	mp->san_path_err_threshold = 1;
	condlog(3, "%s: san_path_err_threshold = 1 %s", mp->alias,
		(watch_checks > 0)? delay_watch_origin : delay_wait_origin);
	if (watch_checks > 0) {
		mp->san_path_err_forget_rate = watch_checks;
		print_off_int_undef(buff, 12, mp->san_path_err_forget_rate);
		condlog(3, "%s: san_path_err_forget_rate = %s %s", mp->alias,
			buff, delay_watch_origin);
	}
	if (wait_checks > 0) {
		mp->san_path_err_recovery_time = wait_checks *
						 conf->max_checkint;
		print_off_int_undef(buff, 12, mp->san_path_err_recovery_time);
		condlog(3, "%s: san_path_err_recovery_time = %s %s", mp->alias,
			buff, delay_wait_origin);
	}
	return 0;
}

static int san_path_deprecated_warned;
#define warn_san_path_deprecated(v, x)					\
	do {								\
		if (v->x > 0 && !san_path_deprecated_warned) {		\
		san_path_deprecated_warned = 1;				\
		condlog(1, "WARNING: option %s is deprecated, "		\
			"please use marginal_path options instead",	\
			#x);						\
		}							\
	} while(0)

int select_san_path_err_threshold(struct config *conf, struct multipath *mp)
{
	const char *origin;
	char buff[12];

	if (marginal_path_check_enabled(mp)) {
		mp->san_path_err_threshold = NU_NO;
		origin = marginal_path_origin;
		goto out;
	}
	mp_set_mpe(san_path_err_threshold);
	mp_set_ovr(san_path_err_threshold);
	mp_set_hwe(san_path_err_threshold);
	mp_set_conf(san_path_err_threshold);
	mp_set_default(san_path_err_threshold, DEFAULT_ERR_CHECKS);
out:
	if (print_off_int_undef(buff, 12, mp->san_path_err_threshold) != 0)
		condlog(3, "%s: san_path_err_threshold = %s %s",
			mp->alias, buff, origin);
	warn_san_path_deprecated(mp, san_path_err_threshold);
	return 0;
}

int select_san_path_err_forget_rate(struct config *conf, struct multipath *mp)
{
	const char *origin;
	char buff[12];

	if (marginal_path_check_enabled(mp)) {
		mp->san_path_err_forget_rate = NU_NO;
		origin = marginal_path_origin;
		goto out;
	}
	mp_set_mpe(san_path_err_forget_rate);
	mp_set_ovr(san_path_err_forget_rate);
	mp_set_hwe(san_path_err_forget_rate);
	mp_set_conf(san_path_err_forget_rate);
	mp_set_default(san_path_err_forget_rate, DEFAULT_ERR_CHECKS);
out:
	if (print_off_int_undef(buff, 12, mp->san_path_err_forget_rate) != 0)
		condlog(3, "%s: san_path_err_forget_rate = %s %s", mp->alias,
			buff, origin);
	warn_san_path_deprecated(mp, san_path_err_forget_rate);
	return 0;

}

int select_san_path_err_recovery_time(struct config *conf, struct multipath *mp)
{
	const char *origin;
	char buff[12];

	if (marginal_path_check_enabled(mp)) {
		mp->san_path_err_recovery_time = NU_NO;
		origin = marginal_path_origin;
		goto out;
	}
	mp_set_mpe(san_path_err_recovery_time);
	mp_set_ovr(san_path_err_recovery_time);
	mp_set_hwe(san_path_err_recovery_time);
	mp_set_conf(san_path_err_recovery_time);
	mp_set_default(san_path_err_recovery_time, DEFAULT_ERR_CHECKS);
out:
	if (print_off_int_undef(buff, 12, mp->san_path_err_recovery_time) != 0)
		condlog(3, "%s: san_path_err_recovery_time = %s %s", mp->alias,
			buff, origin);
	warn_san_path_deprecated(mp, san_path_err_recovery_time);
	return 0;

}

int select_marginal_path_err_sample_time(struct config *conf, struct multipath *mp)
{
	const char *origin;
	char buff[12];

	mp_set_mpe(marginal_path_err_sample_time);
	mp_set_ovr(marginal_path_err_sample_time);
	mp_set_hwe(marginal_path_err_sample_time);
	mp_set_conf(marginal_path_err_sample_time);
	mp_set_default(marginal_path_err_sample_time, DEFAULT_ERR_CHECKS);
out:
	if (print_off_int_undef(buff, 12, mp->marginal_path_err_sample_time)
	    != 0)
		condlog(3, "%s: marginal_path_err_sample_time = %s %s",
			mp->alias, buff, origin);
	return 0;
}

int select_marginal_path_err_rate_threshold(struct config *conf, struct multipath *mp)
{
	const char *origin;
	char buff[12];

	mp_set_mpe(marginal_path_err_rate_threshold);
	mp_set_ovr(marginal_path_err_rate_threshold);
	mp_set_hwe(marginal_path_err_rate_threshold);
	mp_set_conf(marginal_path_err_rate_threshold);
	mp_set_default(marginal_path_err_rate_threshold, DEFAULT_ERR_CHECKS);
out:
	if (print_off_int_undef(buff, 12, mp->marginal_path_err_rate_threshold)
	    != 0)
		condlog(3, "%s: marginal_path_err_rate_threshold = %s %s",
			mp->alias, buff, origin);
	return 0;
}

int select_marginal_path_err_recheck_gap_time(struct config *conf, struct multipath *mp)
{
	const char *origin;
	char buff[12];

	mp_set_mpe(marginal_path_err_recheck_gap_time);
	mp_set_ovr(marginal_path_err_recheck_gap_time);
	mp_set_hwe(marginal_path_err_recheck_gap_time);
	mp_set_conf(marginal_path_err_recheck_gap_time);
	mp_set_default(marginal_path_err_recheck_gap_time, DEFAULT_ERR_CHECKS);
out:
	if (print_off_int_undef(buff, 12,
				mp->marginal_path_err_recheck_gap_time) != 0)
		condlog(3, "%s: marginal_path_err_recheck_gap_time = %s %s",
			mp->alias, buff, origin);
	return 0;
}

int select_marginal_path_double_failed_time(struct config *conf, struct multipath *mp)
{
	const char *origin;
	char buff[12];

	mp_set_mpe(marginal_path_double_failed_time);
	mp_set_ovr(marginal_path_double_failed_time);
	mp_set_hwe(marginal_path_double_failed_time);
	mp_set_conf(marginal_path_double_failed_time);
	mp_set_default(marginal_path_double_failed_time, DEFAULT_ERR_CHECKS);
out:
	if (print_off_int_undef(buff, 12, mp->marginal_path_double_failed_time)
	    != 0)
		condlog(3, "%s: marginal_path_double_failed_time = %s %s",
			mp->alias, buff, origin);
	return 0;
}

int select_skip_kpartx (struct config *conf, struct multipath * mp)
{
	const char *origin;

	mp_set_mpe(skip_kpartx);
	mp_set_ovr(skip_kpartx);
	mp_set_hwe(skip_kpartx);
	mp_set_conf(skip_kpartx);
	mp_set_default(skip_kpartx, DEFAULT_SKIP_KPARTX);
out:
	condlog(3, "%s: skip_kpartx = %s %s", mp->alias,
		(mp->skip_kpartx == SKIP_KPARTX_ON)? "yes" : "no",
		origin);
	return 0;
}

int select_max_sectors_kb(struct config *conf, struct multipath * mp)
{
	const char *origin;

	mp_set_mpe(max_sectors_kb);
	mp_set_ovr(max_sectors_kb);
	mp_set_hwe(max_sectors_kb);
	mp_set_conf(max_sectors_kb);
	mp_set_default(max_sectors_kb, DEFAULT_MAX_SECTORS_KB);
	/*
	 * In the default case, we will not modify max_sectors_kb in sysfs
	 * (see sysfs_set_max_sectors_kb()).
	 * Don't print a log message here to avoid user confusion.
	 */
	return 0;
out:
	condlog(3, "%s: max_sectors_kb = %i %s", mp->alias, mp->max_sectors_kb,
		origin);
	return 0;
}

int select_ghost_delay (struct config *conf, struct multipath * mp)
{
	const char *origin;
	char buff[12];

	mp_set_mpe(ghost_delay);
	mp_set_ovr(ghost_delay);
	mp_set_hwe(ghost_delay);
	mp_set_conf(ghost_delay);
	mp_set_default(ghost_delay, DEFAULT_GHOST_DELAY);
out:
	if (print_off_int_undef(buff, 12, mp->ghost_delay) != 0)
		condlog(3, "%s: ghost_delay = %s %s", mp->alias, buff, origin);
	return 0;
}

int select_find_multipaths_timeout(struct config *conf, struct path *pp)
{
	const char *origin;

	pp_set_conf(find_multipaths_timeout);
	pp_set_default(find_multipaths_timeout,
		       DEFAULT_FIND_MULTIPATHS_TIMEOUT);
out:
	/*
	 * If configured value is negative, and this "unknown" hardware
	 * (no hwentry), use very small timeout to avoid delays.
	 */
	if (pp->find_multipaths_timeout < 0) {
		pp->find_multipaths_timeout = -pp->find_multipaths_timeout;
		if (!pp->hwe) {
			pp->find_multipaths_timeout =
				DEFAULT_UNKNOWN_FIND_MULTIPATHS_TIMEOUT;
			origin = "(default for unknown hardware)";
		}
	}
	condlog(3, "%s: timeout for find_multipaths \"smart\" = %ds %s",
		pp->dev, pp->find_multipaths_timeout, origin);
	return 0;
}

int select_all_tg_pt (struct config *conf, struct multipath * mp)
{
	const char *origin;

	mp_set_ovr(all_tg_pt);
	mp_set_hwe(all_tg_pt);
	mp_set_conf(all_tg_pt);
	mp_set_default(all_tg_pt, DEFAULT_ALL_TG_PT);
out:
	condlog(3, "%s: all_tg_pt = %s %s", mp->alias,
		(mp->all_tg_pt == ALL_TG_PT_ON)? "yes" : "no",
		origin);
	return 0;
}
