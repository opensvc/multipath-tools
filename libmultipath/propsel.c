/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 * Copyright (c) 2005 Benjamin Marzinski, Redhat
 * Copyright (c) 2005 Kiyoshi Ueda, NEC
 */
#include <stdio.h>

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
#include "prioritizers/alua_rtpg.h"
#include <inttypes.h>

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
#define do_default(dest, value)						\
do {									\
	dest = value;							\
	origin = "(internal default)";					\
} while(0)

#define mp_set_mpe(var)							\
do_set(var, mp->mpe, mp->var, "(LUN setting)")
#define mp_set_hwe(var)							\
do_set(var, mp->hwe, mp->var, "(controller setting)")
#define mp_set_ovr(var)							\
do_set(var, conf->overrides, mp->var, "(overrides setting)")
#define mp_set_conf(var)						\
do_set(var, conf, mp->var, "(config file default)")
#define mp_set_default(var, value)					\
do_default(mp->var, value)

#define pp_set_mpe(var)							\
do_set(var, mpe, pp->var, "(LUN setting)")
#define pp_set_hwe(var)							\
do_set(var, pp->hwe, pp->var, "(controller setting)")
#define pp_set_conf(var)						\
do_set(var, conf, pp->var, "(config file default)")
#define pp_set_ovr(var)							\
do_set(var, conf->overrides, pp->var, "(overrides setting)")
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
do_attr_set(var, mp->mpe, shift, "(LUN setting)")
#define set_attr_conf(var, shift)					\
do_attr_set(var, conf, shift, "(config file default)")

extern int
select_mode (struct config *conf, struct multipath *mp)
{
	char *origin;

	set_attr_mpe(mode, ATTR_MODE);
	set_attr_conf(mode, ATTR_MODE);
	mp->attribute_flags &= ~(1 << ATTR_MODE);
	return 0;
out:
	condlog(3, "%s: mode = 0%o %s", mp->alias, mp->mode, origin);
	return 0;
}

extern int
select_uid (struct config *conf, struct multipath *mp)
{
	char *origin;

	set_attr_mpe(uid, ATTR_UID);
	set_attr_conf(uid, ATTR_UID);
	mp->attribute_flags &= ~(1 << ATTR_UID);
	return 0;
out:
	condlog(3, "%s: uid = 0%o %s", mp->alias, mp->uid, origin);
	return 0;
}

extern int
select_gid (struct config *conf, struct multipath *mp)
{
	char *origin;

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
extern int
select_rr_weight (struct config *conf, struct multipath * mp)
{
	char *origin, buff[13];

	mp_set_mpe(rr_weight);
	mp_set_ovr(rr_weight);
	mp_set_hwe(rr_weight);
	mp_set_conf(rr_weight);
	mp_set_default(rr_weight, DEFAULT_RR_WEIGHT);
out:
	print_rr_weight(buff, 13, &mp->rr_weight);
	condlog(3, "%s: rr_weight = %s %s", mp->alias, buff, origin);
	return 0;
}

extern int
select_pgfailback (struct config *conf, struct multipath * mp)
{
	char *origin, buff[13];

	mp_set_mpe(pgfailback);
	mp_set_ovr(pgfailback);
	mp_set_hwe(pgfailback);
	mp_set_conf(pgfailback);
	mp_set_default(pgfailback, DEFAULT_FAILBACK);
out:
	print_pgfailback(buff, 13, &mp->pgfailback);
	condlog(3, "%s: failback = %s %s", mp->alias, buff, origin);
	return 0;
}

extern int
select_pgpolicy (struct config *conf, struct multipath * mp)
{
	char *origin, buff[POLICY_NAME_SIZE];

	if (conf->pgpolicy_flag > 0) {
		mp->pgpolicy = conf->pgpolicy_flag;
		origin = "(cmd line flag)";
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

extern int
select_selector (struct config *conf, struct multipath * mp)
{
	char *origin;

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
	char *origin;

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

	char *origin;
	int user_friendly_names;

	do_set(user_friendly_names, mp->mpe, user_friendly_names,
	       "(LUN setting)");
	do_set(user_friendly_names, conf->overrides, user_friendly_names,
	       "(overrides setting)");
	do_set(user_friendly_names, mp->hwe, user_friendly_names,
	       "(controller setting)");
	do_set(user_friendly_names, conf, user_friendly_names,
	       "(config file setting)");
	do_default(user_friendly_names, DEFAULT_USER_FRIENDLY_NAMES);
out:
	condlog(3, "%s: user_friendly_names = %s %s", mp->wwid,
		(user_friendly_names == USER_FRIENDLY_NAMES_ON)? "yes" : "no",
		origin);
	return (user_friendly_names == USER_FRIENDLY_NAMES_ON);
}

extern int
select_alias (struct config *conf, struct multipath * mp)
{
	char *origin = NULL;

	if (mp->mpe && mp->mpe->alias) {
		mp->alias = STRDUP(mp->mpe->alias);
		origin = "(LUN setting)";
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
		origin = "(using existing alias)";
	}

	if (mp->alias == NULL) {
		mp->alias = get_user_friendly_alias(mp->wwid,
				conf->bindings_file, mp->alias_prefix, conf->bindings_read_only);
		origin = "(user_friendly_name)";
	}
out:
	if (mp->alias == NULL) {
		mp->alias = STRDUP(mp->wwid);
		origin = "(default to wwid)";
	}
	if (mp->alias)
		condlog(3, "%s: alias = %s %s", mp->wwid, mp->alias, origin);
	return mp->alias ? 0 : 1;
}

extern int
select_features (struct config *conf, struct multipath * mp)
{
	char *origin;

	mp_set_mpe(features);
	mp_set_ovr(features);
	mp_set_hwe(features);
	mp_set_conf(features);
	mp_set_default(features, DEFAULT_FEATURES);
out:
	mp->features = STRDUP(mp->features);
	condlog(3, "%s: features = \"%s\" %s", mp->alias, mp->features, origin);

	if (strstr(mp->features, "queue_if_no_path")) {
		if (mp->no_path_retry == NO_PATH_RETRY_UNDEF)
			mp->no_path_retry = NO_PATH_RETRY_QUEUE;
		else if (mp->no_path_retry == NO_PATH_RETRY_FAIL) {
			condlog(1, "%s: config error, overriding 'no_path_retry' value",
				mp->alias);
			mp->no_path_retry = NO_PATH_RETRY_QUEUE;
		}
	}
	return 0;
}

extern int
select_hwhandler (struct config *conf, struct multipath * mp)
{
	char *origin;

	mp_set_hwe(hwhandler);
	mp_set_conf(hwhandler);
	mp_set_default(hwhandler, DEFAULT_HWHANDLER);
out:
	mp->hwhandler = STRDUP(mp->hwhandler);
	condlog(3, "%s: hardware_handler = \"%s\" %s", mp->alias, mp->hwhandler,
		origin);
	return 0;
}

extern int
select_checker(struct config *conf, struct path *pp)
{
	char *origin, *checker_name;
	struct checker * c = &pp->checker;

	do_set(checker_name, conf->overrides, checker_name, "(overrides setting)");
	do_set(checker_name, pp->hwe, checker_name, "(controller setting)");
	do_set(checker_name, conf, checker_name, "(config file setting)");
	do_default(checker_name, DEFAULT_CHECKER);
out:
	checker_get(conf->multipath_dir, c, checker_name);
	condlog(3, "%s: path_checker = %s %s", pp->dev, c->name, origin);
	if (conf->checker_timeout) {
		c->timeout = conf->checker_timeout;
		condlog(3, "%s: checker timeout = %u s (config file default)",
				pp->dev, c->timeout);
	}
	else if (sysfs_get_timeout(pp, &c->timeout) > 0)
		condlog(3, "%s: checker timeout = %u ms (sysfs setting)",
				pp->dev, c->timeout);
	else {
		c->timeout = DEF_TIMEOUT;
		condlog(3, "%s: checker timeout = %u ms (internal default)",
				pp->dev, c->timeout);
	}
	return 0;
}

extern int
select_getuid (struct config *conf, struct path * pp)
{
	char *origin;

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
	int ret;
	struct prio *p = &pp->prio;
	int tpgs = 0;
	unsigned int timeout = conf->checker_timeout;
	char buff[512];
	char *default_prio = PRIO_ALUA;

	if ((tpgs = get_target_port_group_support(pp->fd, timeout)) <= 0)
		return;
	pp->tpgs = tpgs;
	ret = get_target_port_group(pp, timeout);
	if (ret < 0)
		return;
	if (get_asymmetric_access_state(pp->fd, ret, timeout) < 0)
		return;
	if (sysfs_get_asymmetric_access_state(pp, buff, 512) >= 0)
		default_prio = PRIO_SYSFS;
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

extern int
select_prio (struct config *conf, struct path * pp)
{
	char *origin;
	struct mpentry * mpe;
	struct prio * p = &pp->prio;

	if (pp->detect_prio == DETECT_PRIO_ON) {
		detect_prio(conf, pp);
		if (prio_selected(p)) {
			origin = "(detected setting)";
			goto out;
		}
	}
	mpe = find_mpe(conf->mptable, pp->wwid);
	set_prio(conf->multipath_dir, mpe, "(LUN setting)");
	set_prio(conf->multipath_dir, conf->overrides, "(overrides setting)");
	set_prio(conf->multipath_dir, pp->hwe, "controller setting)");
	set_prio(conf->multipath_dir, conf, "(config file default)");
	prio_get(conf->multipath_dir, p, DEFAULT_PRIO, DEFAULT_PRIO_ARGS);
	origin = "(internal default)";
out:
	/*
	 * fetch tpgs mode for alua, if its not already obtained
	 */
	if (!strncmp(prio_name(p), PRIO_ALUA, PRIO_NAME_LEN)) {
		int tpgs = 0;
		unsigned int timeout = conf->checker_timeout;

		if(!pp->tpgs &&
		   (tpgs = get_target_port_group_support(pp->fd, timeout)) >= 0)
			pp->tpgs = tpgs;
	}
	condlog(3, "%s: prio = %s %s", pp->dev, prio_name(p), origin);
	condlog(3, "%s: prio args = \"%s\" %s", pp->dev, prio_args(p), origin);
	return 0;
}

extern int
select_no_path_retry(struct config *conf, struct multipath *mp)
{
	char *origin = NULL;
	char buff[12];

	if (mp->flush_on_last_del == FLUSH_IN_PROGRESS) {
		condlog(0, "flush_on_last_del in progress");
		mp->no_path_retry = NO_PATH_RETRY_FAIL;
		return 0;
	}
	mp_set_mpe(no_path_retry);
	mp_set_ovr(no_path_retry);
	mp_set_hwe(no_path_retry);
	mp_set_conf(no_path_retry);
out:
	print_no_path_retry(buff, 12, &mp->no_path_retry);
	if (origin)
		condlog(3, "%s: no_path_retry = %s %s", mp->alias, buff,
			origin);
	else if (mp->no_path_retry != NO_PATH_RETRY_UNDEF)
		condlog(3, "%s: no_path_retry = %s (inherited setting)",
			mp->alias, buff);
	else
		condlog(3, "%s: no_path_retry = undef (internal default)",
			mp->alias);
	return 0;
}

int
select_minio_rq (struct config *conf, struct multipath * mp)
{
	char *origin;

	do_set(minio_rq, mp->mpe, mp->minio, "(LUN setting)");
	do_set(minio_rq, conf->overrides, mp->minio, "(overrides setting)");
	do_set(minio_rq, mp->hwe, mp->minio, "(controller setting)");
	do_set(minio_rq, conf, mp->minio, "(config file setting)");
	do_default(mp->minio, DEFAULT_MINIO_RQ);
out:
	condlog(3, "%s: minio = %i %s", mp->alias, mp->minio, origin);
	return 0;
}

int
select_minio_bio (struct config *conf, struct multipath * mp)
{
	char *origin;

	mp_set_mpe(minio);
	mp_set_ovr(minio);
	mp_set_hwe(minio);
	mp_set_conf(minio);
	mp_set_default(minio, DEFAULT_MINIO);
out:
	condlog(3, "%s: minio = %i %s", mp->alias, mp->minio, origin);
	return 0;
}

extern int
select_minio (struct config *conf, struct multipath * mp)
{
	unsigned int minv_dmrq[3] = {1, 1, 0};

	if (VERSION_GE(conf->version, minv_dmrq))
		return select_minio_rq(conf, mp);
	else
		return select_minio_bio(conf, mp);
}

extern int
select_fast_io_fail(struct config *conf, struct multipath *mp)
{
	char *origin, buff[12];

	mp_set_ovr(fast_io_fail);
	mp_set_hwe(fast_io_fail);
	mp_set_conf(fast_io_fail);
	mp_set_default(fast_io_fail, DEFAULT_FAST_IO_FAIL);
out:
	print_fast_io_fail(buff, 12, &mp->fast_io_fail);
	condlog(3, "%s: fast_io_fail_tmo = %s %s", mp->alias, buff, origin);
	return 0;
}

extern int
select_dev_loss(struct config *conf, struct multipath *mp)
{
	char *origin, buff[12];

	mp_set_ovr(dev_loss);
	mp_set_hwe(dev_loss);
	mp_set_conf(dev_loss);
	mp->dev_loss = 0;
	return 0;
out:
	print_dev_loss(buff, 12, &mp->dev_loss);
	condlog(3, "%s: dev_loss_tmo = %s %s", mp->alias, buff, origin);
	return 0;
}

extern int
select_flush_on_last_del(struct config *conf, struct multipath *mp)
{
	char *origin;

	if (mp->flush_on_last_del == FLUSH_IN_PROGRESS)
		return 0;
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

extern int
select_reservation_key (struct config *conf, struct multipath * mp)
{
	char *origin, buff[12];

	mp_set_mpe(reservation_key);
	mp_set_conf(reservation_key);
	mp->reservation_key = NULL;
	return 0;
out:
	print_reservation_key(buff, 12, &mp->reservation_key);
	condlog(3, "%s: reservation_key = %s %s", mp->alias, buff, origin);
	return 0;
}

extern int
select_retain_hwhandler (struct config *conf, struct multipath * mp)
{
	char *origin;
	unsigned int minv_dm_retain[3] = {1, 5, 0};

	if (!VERSION_GE(conf->version, minv_dm_retain)) {
		mp->retain_hwhandler = RETAIN_HWHANDLER_OFF;
		origin = "(requires kernel version >= 1.5.0)";
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

extern int
select_detect_prio (struct config *conf, struct path * pp)
{
	char *origin;

	pp_set_ovr(detect_prio);
	pp_set_hwe(detect_prio);
	pp_set_conf(detect_prio);
	pp_set_default(detect_prio, DEFAULT_DETECT_PRIO);
out:
	condlog(3, "%s: detect_prio = %s %s", pp->dev,
		(pp->detect_prio == DETECT_PRIO_ON)? "yes" : "no", origin);
	return 0;
}

extern int
select_deferred_remove (struct config *conf, struct multipath *mp)
{
	char *origin;

#ifndef LIBDM_API_DEFERRED
	mp->deferred_remove = DEFERRED_REMOVE_OFF;
	origin = "(not compiled with support)";
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

extern int
select_delay_watch_checks(struct config *conf, struct multipath *mp)
{
	char *origin, buff[12];

	mp_set_mpe(delay_watch_checks);
	mp_set_ovr(delay_watch_checks);
	mp_set_hwe(delay_watch_checks);
	mp_set_conf(delay_watch_checks);
	mp_set_default(delay_watch_checks, DEFAULT_DELAY_CHECKS);
out:
	print_delay_checks(buff, 12, &mp->delay_watch_checks);
	condlog(3, "%s: delay_watch_checks = %s %s", mp->alias, buff, origin);
	return 0;
}

extern int
select_delay_wait_checks(struct config *conf, struct multipath *mp)
{
	char *origin, buff[12];

	mp_set_mpe(delay_wait_checks);
	mp_set_ovr(delay_wait_checks);
	mp_set_hwe(delay_wait_checks);
	mp_set_conf(delay_wait_checks);
	mp_set_default(delay_wait_checks, DEFAULT_DELAY_CHECKS);
out:
	print_delay_checks(buff, 12, &mp->delay_wait_checks);
	condlog(3, "%s: delay_wait_checks = %s %s", mp->alias, buff, origin);
	return 0;

}

extern int
select_skip_kpartx (struct config *conf, struct multipath * mp)
{
	char *origin;

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

extern int
select_max_sectors_kb (struct config *conf, struct multipath * mp)
{
	char *origin;

	mp_set_mpe(max_sectors_kb);
	mp_set_ovr(max_sectors_kb);
	mp_set_hwe(max_sectors_kb);
	mp_set_conf(max_sectors_kb);
	return 0;
out:
	condlog(3, "%s: max_sectors_kb = %i %s", mp->alias, mp->max_sectors_kb,
		origin);
	return 0;
}
