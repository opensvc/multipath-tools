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

extern int
select_mode (struct multipath *mp)
{
	if (mp->mpe && (mp->mpe->attribute_flags & (1 << ATTR_MODE))) {
		mp->attribute_flags |= (1 << ATTR_MODE);
		mp->mode = mp->mpe->mode;
		condlog(3, "mode = 0%o (LUN setting)", mp->mode);
	}
	else if (conf->attribute_flags & (1 << ATTR_MODE)) {
		mp->attribute_flags |= (1 << ATTR_MODE);
		mp->mode = conf->mode;
		condlog(3, "mode = 0%o (config file default)", mp->mode);
	}
	else
		mp->attribute_flags &= ~(1 << ATTR_MODE);
	return 0;
}

extern int
select_uid (struct multipath *mp)
{
	if (mp->mpe && (mp->mpe->attribute_flags & (1 << ATTR_UID))) {
		mp->attribute_flags |= (1 << ATTR_UID);
		mp->uid = mp->mpe->uid;
		condlog(3, "uid = %u (LUN setting)", mp->uid);
	}
	else if (conf->attribute_flags & (1 << ATTR_UID)) {
		mp->attribute_flags |= (1 << ATTR_UID);
		mp->uid = conf->uid;
		condlog(3, "uid = %u (config file default)", mp->uid);
	}
	else
		mp->attribute_flags &= ~(1 << ATTR_UID);
	return 0;
}

extern int
select_gid (struct multipath *mp)
{
	if (mp->mpe && (mp->mpe->attribute_flags & (1 << ATTR_GID))) {
		mp->attribute_flags |= (1 << ATTR_GID);
		mp->gid = mp->mpe->gid;
		condlog(3, "gid = %u (LUN setting)", mp->gid);
	}
	else if (conf->attribute_flags & (1 << ATTR_GID)) {
		mp->attribute_flags |= (1 << ATTR_GID);
		mp->gid = conf->gid;
		condlog(3, "gid = %u (config file default)", mp->gid);
	}
	else
		mp->attribute_flags &= ~(1 << ATTR_GID);
	return 0;
}

/*
 * selectors :
 * traverse the configuration layers from most specific to most generic
 * stop at first explicit setting found
 */
extern int
select_rr_weight (struct multipath * mp)
{
	if (mp->mpe && mp->mpe->rr_weight) {
		mp->rr_weight = mp->mpe->rr_weight;
		condlog(3, "%s: rr_weight = %i (LUN setting)",
			mp->alias, mp->rr_weight);
		return 0;
	}
	if (mp->hwe && mp->hwe->rr_weight) {
		mp->rr_weight = mp->hwe->rr_weight;
		condlog(3, "%s: rr_weight = %i (controller setting)",
			mp->alias, mp->rr_weight);
		return 0;
	}
	if (conf->rr_weight) {
		mp->rr_weight = conf->rr_weight;
		condlog(3, "%s: rr_weight = %i (config file default)",
			mp->alias, mp->rr_weight);
		return 0;
	}
	mp->rr_weight = RR_WEIGHT_NONE;
	condlog(3, "%s: rr_weight = %i (internal default)",
		mp->alias, mp->rr_weight);
	return 0;
}

extern int
select_pgfailback (struct multipath * mp)
{
	if (mp->mpe && mp->mpe->pgfailback != FAILBACK_UNDEF) {
		mp->pgfailback = mp->mpe->pgfailback;
		condlog(3, "%s: pgfailback = %i (LUN setting)",
			mp->alias, mp->pgfailback);
		return 0;
	}
	if (mp->hwe && mp->hwe->pgfailback != FAILBACK_UNDEF) {
		mp->pgfailback = mp->hwe->pgfailback;
		condlog(3, "%s: pgfailback = %i (controller setting)",
			mp->alias, mp->pgfailback);
		return 0;
	}
	if (conf->pgfailback != FAILBACK_UNDEF) {
		mp->pgfailback = conf->pgfailback;
		condlog(3, "%s: pgfailback = %i (config file default)",
			mp->alias, mp->pgfailback);
		return 0;
	}
	mp->pgfailback = DEFAULT_FAILBACK;
	condlog(3, "%s: pgfailover = %i (internal default)",
		mp->alias, mp->pgfailback);
	return 0;
}

extern int
select_pgpolicy (struct multipath * mp)
{
	char pgpolicy_name[POLICY_NAME_SIZE];

	if (conf->pgpolicy_flag > 0) {
		mp->pgpolicy = conf->pgpolicy_flag;
		mp->pgpolicyfn = pgpolicies[mp->pgpolicy];
		get_pgpolicy_name(pgpolicy_name, POLICY_NAME_SIZE,
				  mp->pgpolicy);
		condlog(3, "%s: pgpolicy = %s (cmd line flag)",
			mp->alias, pgpolicy_name);
		return 0;
	}
	if (mp->mpe && mp->mpe->pgpolicy > 0) {
		mp->pgpolicy = mp->mpe->pgpolicy;
		mp->pgpolicyfn = pgpolicies[mp->pgpolicy];
		get_pgpolicy_name(pgpolicy_name, POLICY_NAME_SIZE,
				  mp->pgpolicy);
		condlog(3, "%s: pgpolicy = %s (LUN setting)",
			mp->alias, pgpolicy_name);
		return 0;
	}
	if (mp->hwe && mp->hwe->pgpolicy > 0) {
		mp->pgpolicy = mp->hwe->pgpolicy;
		mp->pgpolicyfn = pgpolicies[mp->pgpolicy];
		get_pgpolicy_name(pgpolicy_name, POLICY_NAME_SIZE,
				  mp->pgpolicy);
		condlog(3, "%s: pgpolicy = %s (controller setting)",
			mp->alias, pgpolicy_name);
		return 0;
	}
	if (conf->pgpolicy > 0) {
		mp->pgpolicy = conf->pgpolicy;
		mp->pgpolicyfn = pgpolicies[mp->pgpolicy];
		get_pgpolicy_name(pgpolicy_name, POLICY_NAME_SIZE,
				  mp->pgpolicy);
		condlog(3, "%s: pgpolicy = %s (config file default)",
			mp->alias, pgpolicy_name);
		return 0;
	}
	mp->pgpolicy = DEFAULT_PGPOLICY;
	mp->pgpolicyfn = pgpolicies[mp->pgpolicy];
	get_pgpolicy_name(pgpolicy_name, POLICY_NAME_SIZE, mp->pgpolicy);
	condlog(3, "%s: pgpolicy = %s (internal default)",
		mp->alias, pgpolicy_name);
	return 0;
}

extern int
select_selector (struct multipath * mp)
{
	if (mp->mpe && mp->mpe->selector) {
		mp->selector = mp->mpe->selector;
		condlog(3, "%s: selector = %s (LUN setting)",
			mp->alias, mp->selector);
		return 0;
	}
	if (mp->hwe && mp->hwe->selector) {
		mp->selector = mp->hwe->selector;
		condlog(3, "%s: selector = %s (controller setting)",
			mp->alias, mp->selector);
		return 0;
	}
	if (conf->selector) {
		mp->selector = conf->selector;
		condlog(3, "%s: selector = %s (config file default)",
			mp->alias, mp->selector);
		return 0;
	}
	mp->selector = set_default(DEFAULT_SELECTOR);
	condlog(3, "%s: selector = %s (internal default)",
		mp->alias, mp->selector);
	return 0;
}

static void
select_alias_prefix (struct multipath * mp)
{
	if (mp->hwe && mp->hwe->alias_prefix) {
		mp->alias_prefix = mp->hwe->alias_prefix;
		condlog(3, "%s: alias_prefix = %s (controller setting)",
			mp->wwid, mp->alias_prefix);
		return;
	}
	if (conf->alias_prefix) {
		mp->alias_prefix = conf->alias_prefix;
		condlog(3, "%s: alias_prefix = %s (config file default)",
			mp->wwid, mp->alias_prefix);
		return;
	}
	mp->alias_prefix = set_default(DEFAULT_ALIAS_PREFIX);
	condlog(3, "%s: alias_prefix = %s (internal default)",
		mp->wwid, mp->alias_prefix);
}

static int
want_user_friendly_names(struct multipath * mp)
{
	if (mp->mpe &&
	    mp->mpe->user_friendly_names != USER_FRIENDLY_NAMES_UNDEF)
		return (mp->mpe->user_friendly_names == USER_FRIENDLY_NAMES_ON);
	if (mp->hwe &&
	    mp->hwe->user_friendly_names != USER_FRIENDLY_NAMES_UNDEF)
		return (mp->hwe->user_friendly_names == USER_FRIENDLY_NAMES_ON);
	return (conf->user_friendly_names  == USER_FRIENDLY_NAMES_ON);
}

extern int
select_alias (struct multipath * mp)
{
	if (mp->mpe && mp->mpe->alias)
		mp->alias = STRDUP(mp->mpe->alias);
	else {
		mp->alias = NULL;
		if (want_user_friendly_names(mp)) {
			select_alias_prefix(mp);
			mp->alias = get_user_friendly_alias(mp->wwid,
					conf->bindings_file, mp->alias_prefix, conf->bindings_read_only);
		}
		if (mp->alias == NULL)
			mp->alias = STRDUP(mp->wwid);
	}

	return mp->alias ? 0 : 1;
}

extern int
select_features (struct multipath * mp)
{
	struct mpentry * mpe;
	char *origin;

	if ((mpe = find_mpe(mp->wwid)) && mpe->features) {
		mp->features = STRDUP(mpe->features);
		origin = "LUN setting";
	} else if (mp->hwe && mp->hwe->features) {
		mp->features = STRDUP(mp->hwe->features);
		origin = "controller setting";
	} else if (conf->features) {
		mp->features = STRDUP(conf->features);
		origin = "config file default";
	} else {
		mp->features = set_default(DEFAULT_FEATURES);
		origin = "internal default";
	}
	condlog(3, "%s: features = %s (%s)",
		mp->alias, mp->features, origin);
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
select_hwhandler (struct multipath * mp)
{
	if (mp->hwe && mp->hwe->hwhandler) {
		mp->hwhandler = mp->hwe->hwhandler;
		condlog(3, "%s: hwhandler = %s (controller setting)",
			mp->alias, mp->hwhandler);
		return 0;
	}
	if (conf->hwhandler) {
		mp->hwhandler = conf->hwhandler;
		condlog(3, "%s: hwhandler = %s (config file default)",
			mp->alias, mp->hwhandler);
		return 0;
	}
	mp->hwhandler = set_default(DEFAULT_HWHANDLER);
	condlog(3, "%s: hwhandler = %s (internal default)",
		mp->alias, mp->hwhandler);
	return 0;
}

extern int
select_checker(struct path *pp)
{
	struct checker * c = &pp->checker;

	if (pp->hwe && pp->hwe->checker_name) {
		checker_get(c, pp->hwe->checker_name);
		condlog(3, "%s: path checker = %s (controller setting)",
			pp->dev, checker_name(c));
		goto out;
	}
	if (conf->checker_name) {
		checker_get(c, conf->checker_name);
		condlog(3, "%s: path checker = %s (config file default)",
			pp->dev, checker_name(c));
		goto out;
	}
	checker_get(c, DEFAULT_CHECKER);
	condlog(3, "%s: path checker = %s (internal default)",
		pp->dev, checker_name(c));
out:
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
select_getuid (struct path * pp)
{
	if (pp->hwe && pp->hwe->uid_attribute) {
		pp->uid_attribute = pp->hwe->uid_attribute;
		condlog(3, "%s: uid_attribute = %s (controller setting)",
			pp->dev, pp->uid_attribute);
		return 0;
	}
	if (pp->hwe && pp->hwe->getuid) {
		pp->getuid = pp->hwe->getuid;
		condlog(3, "%s: getuid = %s (deprecated) (controller setting)",
			pp->dev, pp->getuid);
		return 0;
	}
	if (conf->uid_attribute) {
		pp->uid_attribute = conf->uid_attribute;
		condlog(3, "%s: uid_attribute = %s (config file default)",
			pp->dev, pp->uid_attribute);
		return 0;
	}
	if (conf->getuid) {
		pp->getuid = conf->getuid;
		condlog(3, "%s: getuid = %s (deprecated) (config file default)",
			pp->dev, pp->getuid);
		return 0;
	}
	pp->uid_attribute = STRDUP(DEFAULT_UID_ATTRIBUTE);
	condlog(3, "%s: uid_attribute = %s (internal default)",
		pp->dev, pp->uid_attribute);
	return 0;
}

void
detect_prio(struct path * pp)
{
	int ret;
	struct prio *p = &pp->prio;

	if (get_target_port_group_support(pp->fd) <= 0)
		return;
	ret = get_target_port_group(pp->fd);
	if (ret < 0)
		return;
	if (get_asymmetric_access_state(pp->fd, ret) < 0)
		return;
	prio_get(p, PRIO_ALUA, DEFAULT_PRIO_ARGS);
}

extern int
select_prio (struct path * pp)
{
	struct mpentry * mpe;
	struct prio * p = &pp->prio;

	if (pp->detect_prio == DETECT_PRIO_ON) {
		detect_prio(pp);
		if (prio_selected(p)) {
			condlog(3, "%s: prio = %s (detected setting)",
				pp->dev, prio_name(p));
			return 0;
		}
	}

	if ((mpe = find_mpe(pp->wwid))) {
		if (mpe->prio_name) {
			prio_get(p, mpe->prio_name, mpe->prio_args);
			condlog(3, "%s: prio = %s (LUN setting)",
				pp->dev, prio_name(p));
			return 0;
		}
	}

	if (pp->hwe && pp->hwe->prio_name) {
		prio_get(p, pp->hwe->prio_name, pp->hwe->prio_args);
		condlog(3, "%s: prio = %s (controller setting)",
			pp->dev, pp->hwe->prio_name);
		condlog(3, "%s: prio args = %s (controller setting)",
			pp->dev, pp->hwe->prio_args);
		return 0;
	}
	if (conf->prio_name) {
		prio_get(p, conf->prio_name, conf->prio_args);
		condlog(3, "%s: prio = %s (config file default)",
			pp->dev, conf->prio_name);
		condlog(3, "%s: prio args = %s (config file default)",
			pp->dev, conf->prio_args);
		return 0;
	}
	prio_get(p, DEFAULT_PRIO, DEFAULT_PRIO_ARGS);
	condlog(3, "%s: prio = %s (internal default)",
		pp->dev, DEFAULT_PRIO);
	condlog(3, "%s: prio args = %s (internal default)",
		pp->dev, DEFAULT_PRIO_ARGS);
	return 0;
}

extern int
select_no_path_retry(struct multipath *mp)
{
	if (mp->flush_on_last_del == FLUSH_IN_PROGRESS) {
		condlog(0, "flush_on_last_del in progress");
		mp->no_path_retry = NO_PATH_RETRY_FAIL;
		return 0;
	}
	if (mp->mpe && mp->mpe->no_path_retry != NO_PATH_RETRY_UNDEF) {
		mp->no_path_retry = mp->mpe->no_path_retry;
		condlog(3, "%s: no_path_retry = %i (multipath setting)",
			mp->alias, mp->no_path_retry);
		return 0;
	}
	if (mp->hwe && mp->hwe->no_path_retry != NO_PATH_RETRY_UNDEF) {
		mp->no_path_retry = mp->hwe->no_path_retry;
		condlog(3, "%s: no_path_retry = %i (controller setting)",
			mp->alias, mp->no_path_retry);
		return 0;
	}
	if (conf->no_path_retry != NO_PATH_RETRY_UNDEF) {
		mp->no_path_retry = conf->no_path_retry;
		condlog(3, "%s: no_path_retry = %i (config file default)",
			mp->alias, mp->no_path_retry);
		return 0;
	}
	if (mp->no_path_retry != NO_PATH_RETRY_UNDEF)
		condlog(3, "%s: no_path_retry = %i (inherited setting)",
			mp->alias, mp->no_path_retry);
	else
		condlog(3, "%s: no_path_retry = %i (internal default)",
			mp->alias, mp->no_path_retry);
	return 0;
}

int
select_minio_rq (struct multipath * mp)
{
	if (mp->mpe && mp->mpe->minio_rq) {
		mp->minio = mp->mpe->minio_rq;
		condlog(3, "%s: minio = %i rq (LUN setting)",
			mp->alias, mp->minio);
		return 0;
	}
	if (mp->hwe && mp->hwe->minio_rq) {
		mp->minio = mp->hwe->minio_rq;
		condlog(3, "%s: minio = %i rq (controller setting)",
			mp->alias, mp->minio);
		return 0;
	}
	if (conf->minio) {
		mp->minio = conf->minio_rq;
		condlog(3, "%s: minio = %i rq (config file default)",
			mp->alias, mp->minio);
		return 0;
	}
	mp->minio = DEFAULT_MINIO_RQ;
	condlog(3, "%s: minio = %i rq (internal default)",
		mp->alias, mp->minio);
	return 0;
}

int
select_minio_bio (struct multipath * mp)
{
	if (mp->mpe && mp->mpe->minio) {
		mp->minio = mp->mpe->minio;
		condlog(3, "%s: minio = %i (LUN setting)",
			mp->alias, mp->minio);
		return 0;
	}
	if (mp->hwe && mp->hwe->minio) {
		mp->minio = mp->hwe->minio;
		condlog(3, "%s: minio = %i (controller setting)",
			mp->alias, mp->minio);
		return 0;
	}
	if (conf->minio) {
		mp->minio = conf->minio;
		condlog(3, "%s: minio = %i (config file default)",
			mp->alias, mp->minio);
		return 0;
	}
	mp->minio = DEFAULT_MINIO;
	condlog(3, "%s: minio = %i (internal default)",
		mp->alias, mp->minio);
	return 0;
}

extern int
select_minio (struct multipath * mp)
{
	unsigned int minv_dmrq[3] = {1, 1, 0};

	if (VERSION_GE(conf->version, minv_dmrq))
		return select_minio_rq(mp);
	else
		return select_minio_bio(mp);
}

extern int
select_fast_io_fail(struct multipath *mp)
{
	if (mp->hwe && mp->hwe->fast_io_fail != MP_FAST_IO_FAIL_UNSET) {
		mp->fast_io_fail = mp->hwe->fast_io_fail;
		if (mp->fast_io_fail == MP_FAST_IO_FAIL_OFF)
			condlog(3, "%s: fast_io_fail_tmo = off "
				"(controller setting)", mp->alias);
		else
			condlog(3, "%s: fast_io_fail_tmo = %d "
				"(controller setting)", mp->alias,
				mp->fast_io_fail == MP_FAST_IO_FAIL_ZERO ? 0 : mp->fast_io_fail);
		return 0;
	}
	if (conf->fast_io_fail != MP_FAST_IO_FAIL_UNSET) {
		mp->fast_io_fail = conf->fast_io_fail;
		if (mp->fast_io_fail == MP_FAST_IO_FAIL_OFF)
			condlog(3, "%s: fast_io_fail_tmo = off "
				"(config file default)", mp->alias);
		else
			condlog(3, "%s: fast_io_fail_tmo = %d "
				"(config file default)", mp->alias,
				mp->fast_io_fail == MP_FAST_IO_FAIL_ZERO ? 0 : mp->fast_io_fail);
		return 0;
	}
	mp->fast_io_fail = MP_FAST_IO_FAIL_UNSET;
	return 0;
}

extern int
select_dev_loss(struct multipath *mp)
{
	if (mp->hwe && mp->hwe->dev_loss) {
		mp->dev_loss = mp->hwe->dev_loss;
		condlog(3, "%s: dev_loss_tmo = %u (controller default)",
			mp->alias, mp->dev_loss);
		return 0;
	}
	if (conf->dev_loss) {
		mp->dev_loss = conf->dev_loss;
		condlog(3, "%s: dev_loss_tmo = %u (config file default)",
			mp->alias, mp->dev_loss);
		return 0;
	}
	mp->dev_loss = 0;
	return 0;
}

extern int
select_flush_on_last_del(struct multipath *mp)
{
	if (mp->flush_on_last_del == FLUSH_IN_PROGRESS)
		return 0;
	if (mp->mpe && mp->mpe->flush_on_last_del != FLUSH_UNDEF) {
		mp->flush_on_last_del = mp->mpe->flush_on_last_del;
		condlog(3, "%s: flush_on_last_del = %i (multipath setting)",
			mp->alias, mp->flush_on_last_del);
		return 0;
	}
	if (mp->hwe && mp->hwe->flush_on_last_del != FLUSH_UNDEF) {
		mp->flush_on_last_del = mp->hwe->flush_on_last_del;
		condlog(3, "%s: flush_on_last_del = %i (controler setting)",
			mp->alias, mp->flush_on_last_del);
		return 0;
	}
	if (conf->flush_on_last_del != FLUSH_UNDEF) {
		mp->flush_on_last_del = conf->flush_on_last_del;
		condlog(3, "%s: flush_on_last_del = %i (config file default)",
			mp->alias, mp->flush_on_last_del);
		return 0;
	}
	mp->flush_on_last_del = FLUSH_UNDEF;
	condlog(3, "%s: flush_on_last_del = DISABLED (internal default)",
		mp->alias);
	return 0;
}

extern int
select_reservation_key (struct multipath * mp)
{
	int j;
	unsigned char *keyp;
	uint64_t prkey = 0;

	mp->reservation_key = NULL;

	if (mp->mpe && mp->mpe->reservation_key) {
		keyp =  mp->mpe->reservation_key;
		for (j = 0; j < 8; ++j) {
			if (j > 0)
				prkey <<= 8;
			prkey |= *keyp;
			++keyp;
		}

		condlog(3, "%s: reservation_key = 0x%" PRIx64 " "
				"(multipath setting)",  mp->alias, prkey);

		mp->reservation_key = mp->mpe->reservation_key;
		return 0;
	}

	if (conf->reservation_key) {
		keyp = conf->reservation_key;
		for (j = 0; j < 8; ++j) {
			if (j > 0)
				prkey <<= 8;
			prkey |= *keyp;
			++keyp;
		}

		condlog(3, "%s: reservation_key  = 0x%" PRIx64
				" (config file default)", mp->alias, prkey);

		mp->reservation_key = conf->reservation_key;
		return 0;
	}

	return 0;
}

extern int
select_retain_hwhandler (struct multipath * mp)
{
	unsigned int minv_dm_retain[3] = {1, 5, 0};

	if (!VERSION_GE(conf->version, minv_dm_retain)) {
		mp->retain_hwhandler = RETAIN_HWHANDLER_OFF;
		condlog(3, "%s: retain_attached_hw_handler disabled (requires kernel version >= 1.5.0)", mp->alias);
		return 0;
	}

	if (mp->hwe && mp->hwe->retain_hwhandler) {
		mp->retain_hwhandler = mp->hwe->retain_hwhandler;
		condlog(3, "%s: retain_attached_hw_handler = %d (controller default)", mp->alias, mp->retain_hwhandler);
		return 0;
	}
	if (conf->retain_hwhandler) {
		mp->retain_hwhandler = conf->retain_hwhandler;
		condlog(3, "%s: retain_attached_hw_handler = %d (config file default)", mp->alias, mp->retain_hwhandler);
		return 0;
	}
	mp->retain_hwhandler = 0;
	condlog(3, "%s: retain_attached_hw_handler = %d (compiled in default)", mp->alias, mp->retain_hwhandler);
	return 0;
}

extern int
select_detect_prio (struct path * pp)
{
	if (pp->hwe && pp->hwe->detect_prio) {
		pp->detect_prio = pp->hwe->detect_prio;
		condlog(3, "%s: detect_prio = %d (controller default)", pp->dev, pp->detect_prio);
		return 0;
	}
	if (conf->detect_prio) {
		pp->detect_prio = conf->detect_prio;
		condlog(3, "%s: detect_prio = %d (config file default)", pp->dev, pp->detect_prio);
		return 0;
	}
	pp->detect_prio = 0;
	condlog(3, "%s: detect_prio = %d (compiled in default)", pp->dev, pp->detect_prio);
	return 0;
}
