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

pgpolicyfn *pgpolicies[] = {
	NULL,
	one_path_per_group,
	one_group,
	group_by_serial,
	group_by_prio,
	group_by_node_name
};

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
	mp->selector = conf->selector;
	condlog(3, "%s: selector = %s (internal default)",
		mp->alias, mp->selector);
	return 0;
}

extern int
select_alias (struct multipath * mp)
{
	if (mp->mpe && mp->mpe->alias)
		mp->alias = mp->mpe->alias;
	else {
		mp->alias = NULL;
		if (conf->user_friendly_names)
			mp->alias = get_user_friendly_alias(mp->wwid,
					conf->bindings_file);
		if (mp->alias == NULL){
			char *alias;
			if ((alias = MALLOC(WWID_SIZE)) != NULL){
				if (dm_get_name(mp->wwid, DEFAULT_TARGET,
						alias) == 1)
					mp->alias = alias;
				else
					FREE(alias);
			}
		}
		if (mp->alias == NULL)
			mp->alias = mp->wwid;
	}

	return 0;
}

extern int
select_features (struct multipath * mp)
{
	if (mp->hwe && mp->hwe->features) {
		mp->features = mp->hwe->features;
		condlog(3, "%s: features = %s (controller setting)",
			mp->alias, mp->features);
		return 0;
	}
	mp->features = conf->features;
	condlog(3, "%s: features = %s (internal default)",
		mp->alias, mp->features);
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
	mp->hwhandler = conf->hwhandler;
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
		return 0;
	}
	if (conf->checker_name) {
		checker_get(c, conf->checker_name);
		condlog(3, "%s: path checker = %s (config file default)",
			pp->dev, checker_name(c));
		return 0;
	}
	checker_get(c, DEFAULT_CHECKER);
	condlog(3, "%s: path checker = %s (internal default)",
		pp->dev, checker_name(c));
	return 0;
}

extern int
select_getuid (struct path * pp)
{
	if (pp->hwe && pp->hwe->getuid) {
		pp->getuid = pp->hwe->getuid;
		condlog(3, "%s: getuid = %s (controller setting)",
			pp->dev, pp->getuid);
		return 0;
	}
	if (conf->getuid) {
		pp->getuid = conf->getuid;
		condlog(3, "%s: getuid = %s (config file default)",
			pp->dev, pp->getuid);
		return 0;
	}
	pp->getuid = STRDUP(DEFAULT_GETUID);
	condlog(3, "%s: getuid = %s (internal default)",
		pp->dev, pp->getuid);
	return 0;
}

extern int
select_prio (struct path * pp)
{
	if (pp->hwe && pp->hwe->prio_name) {
		pp->prio = prio_lookup(pp->hwe->prio_name);
		condlog(3, "%s: prio = %s (controller setting)",
			pp->dev, pp->hwe->prio_name);
		return 0;
	}
	if (conf->prio_name) {
		pp->prio = prio_lookup(conf->prio_name);
		condlog(3, "%s: prio = %s (config file default)",
			pp->dev, conf->prio_name);
		return 0;
	}
	pp->prio = prio_lookup(DEFAULT_PRIO);
	condlog(3, "%s: prio = %s (internal default)",
		pp->dev, DEFAULT_PRIO);
	return 0;
}

extern int
select_no_path_retry(struct multipath *mp)
{
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
	mp->no_path_retry = NO_PATH_RETRY_UNDEF;
	condlog(3, "%s: no_path_retry = NONE (internal default)",
		mp->alias);
	return 0;
}

extern int
select_minio (struct multipath * mp)
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
select_pg_timeout(struct multipath *mp)
{
	if (mp->mpe && mp->mpe->pg_timeout != PGTIMEOUT_UNDEF) {
		mp->pg_timeout = mp->mpe->pg_timeout;
		if (mp->pg_timeout > 0)
			condlog(3, "%s: pg_timeout = %d (multipath setting)",
				mp->alias, mp->pg_timeout);
		else
			condlog(3, "%s: pg_timeout = NONE (multipath setting)",
				mp->alias);
		return 0;
	}
	if (mp->hwe && mp->hwe->pg_timeout != PGTIMEOUT_UNDEF) {
		mp->pg_timeout = mp->hwe->pg_timeout;
		if (mp->pg_timeout > 0)
			condlog(3, "%s: pg_timeout = %d (controller setting)",
				mp->alias, mp->pg_timeout);
		else
			condlog(3, "%s: pg_timeout = NONE (controller setting)",
				mp->alias);
		return 0;
	}
	if (conf->pg_timeout != PGTIMEOUT_UNDEF) {
		mp->pg_timeout = conf->pg_timeout;
		if (mp->pg_timeout > 0)
			condlog(3, "%s: pg_timeout = %d (config file default)",
				mp->alias, mp->pg_timeout);
		else
			condlog(3,
				"%s: pg_timeout = NONE (config file default)",
				mp->alias);
		return 0;
	}
	mp->pg_timeout = PGTIMEOUT_UNDEF;
	condlog(3, "pg_timeout = NONE (internal default)");
	return 0;
}
