/*
 * Based on Alexandre Cassen template for keepalived
 * Copyright (c) 2004, 2005, 2006  Christophe Varoqui
 * Copyright (c) 2005 Benjamin Marzinski, Redhat
 * Copyright (c) 2005 Kiyoshi Ueda, NEC
 */
#include "vector.h"
#include "hwtable.h"
#include "structs.h"
#include "parser.h"
#include "config.h"
#include "debug.h"
#include "memory.h"
#include "pgpolicies.h"
#include "blacklist.h"
#include "defaults.h"

#include "../libcheckers/checkers.h"

/*
 * default block handlers
 */
static int
polling_interval_handler(vector strvec)
{
	char * buff;

	buff = VECTOR_SLOT(strvec, 1);
	conf->checkint = atoi(buff);
	conf->max_checkint = MAX_CHECKINT(conf->checkint);

	return 0;
}

static int
udev_dir_handler(vector strvec)
{
	conf->udev_dir = set_value(strvec);

	if (!conf->udev_dir)
		return 1;

	return 0;
}

static int
def_selector_handler(vector strvec)
{
	conf->selector = set_value(strvec);

	if (!conf->selector)
		return 1;

	return 0;
}

static int
def_pgpolicy_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	conf->default_pgpolicy = get_pgpolicy_id(buff);
	FREE(buff);

	return 0;
}

static int
def_getuid_callout_handler(vector strvec)
{
	conf->default_getuid = set_value(strvec);

	if (!conf->default_getuid)
		return 1;
	
	return 0;
}

static int
def_prio_callout_handler(vector strvec)
{
	conf->default_getprio = set_value(strvec);

	if (!conf->default_getprio)
		return 1;
	
	if (strlen(conf->default_getprio) == 4 &&
	    !strcmp(conf->default_getprio, "none")) {
		FREE(conf->default_getprio);
		conf->default_getprio = NULL;
	}
		
	return 0;
}

static int
def_features_handler(vector strvec)
{
	conf->features = set_value(strvec);

	if (!conf->features)
		return 1;

	return 0;
}

static int
def_path_checker_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;
	
	conf->default_checker_index = get_checker_id(buff);
	FREE(buff);

	return 0;
}

static int
def_minio_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	conf->minio = atoi(buff);
	FREE(buff);

	return 0;
}

static int
def_weight_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	if (strlen(buff) == 10 &&
	    !strcmp(buff, "priorities"))
		conf->rr_weight = RR_WEIGHT_PRIO;

	FREE(buff);

	return 0;
}

static int
default_failback_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (strlen(buff) == 6 && !strcmp(buff, "manual"))
		conf->pgfailback = -FAILBACK_MANUAL;
	else if (strlen(buff) == 9 && !strcmp(buff, "immediate"))
		conf->pgfailback = -FAILBACK_IMMEDIATE;
	else
		conf->pgfailback = atoi(buff);

	FREE(buff);

	return 0;
}

static int
def_no_path_retry_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if ((strlen(buff) == 4 && !strcmp(buff, "fail")) ||
	    (strlen(buff) == 1 && !strcmp(buff, "0")))
		conf->no_path_retry = NO_PATH_RETRY_FAIL;
	else if (strlen(buff) == 5 && !strcmp(buff, "queue"))
		conf->no_path_retry = NO_PATH_RETRY_QUEUE;
	else if ((conf->no_path_retry = atoi(buff)) < 1)
		conf->no_path_retry = NO_PATH_RETRY_UNDEF;

	FREE(buff);
	return 0;
}

static int
names_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	if ((strlen(buff) == 2 && !strcmp(buff, "no")) ||
	    (strlen(buff) == 1 && !strcmp(buff, "0")))
		conf->user_friendly_names = 0;
	else if ((strlen(buff) == 3 && !strcmp(buff, "yes")) ||
		 (strlen(buff) == 1 && !strcmp(buff, "1")))
		conf->user_friendly_names = 1;

	FREE(buff);
	return 0;
}

/*
 * blacklist block handlers
 */
static int
blacklist_handler(vector strvec)
{
	conf->blist_devnode = vector_alloc();
	conf->blist_wwid = vector_alloc();
	conf->blist_device = vector_alloc();

	if (!conf->blist_devnode || !conf->blist_wwid || !conf->blist_device)
		return 1;

	return 0;
}

static int
ble_devnode_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	return store_ble(conf->blist_devnode, buff);
}

static int
ble_wwid_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	return store_ble(conf->blist_wwid, buff);
}

static int
ble_device_handler(vector strvec)
{
	return alloc_ble_device(conf->blist_device);
}

static int
ble_vendor_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	return set_ble_device(conf->blist_device, buff, NULL);
}

static int
ble_product_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	return set_ble_device(conf->blist_device, NULL, buff);
}

/*
 * devices block handlers
 */
static int
devices_handler(vector strvec)
{
	conf->hwtable = vector_alloc();

	if (!conf->hwtable)
		return 1;

	return 0;
}

static int
device_handler(vector strvec)
{
	struct hwentry * hwe;

	hwe = (struct hwentry *)MALLOC(sizeof(struct hwentry));

	if (!hwe)
		return 1;

	if (!vector_alloc_slot(conf->hwtable)) {
		FREE(hwe);
		return 1;
	}
	vector_set_slot(conf->hwtable, hwe);

	return 0;
}

static int
vendor_handler(vector strvec)
{
	struct hwentry * hwe = VECTOR_LAST_SLOT(conf->hwtable);

	if (!hwe)
		return 1;
	
	hwe->vendor = set_value(strvec);

	if (!hwe->vendor)
		return 1;

	return 0;
}

static int
product_handler(vector strvec)
{
	struct hwentry * hwe = VECTOR_LAST_SLOT(conf->hwtable);

	if (!hwe)
		return 1;
	
	hwe->product = set_value(strvec);

	if (!hwe->product)
		return 1;

	return 0;
}

static int
hw_pgpolicy_handler(vector strvec)
{
	char * buff;
	struct hwentry * hwe = VECTOR_LAST_SLOT(conf->hwtable);

	buff = set_value(strvec);

	if (!buff)
		return 1;

	hwe->pgpolicy = get_pgpolicy_id(buff);
	FREE(buff);

	return 0;
}

static int
hw_getuid_callout_handler(vector strvec)
{
	struct hwentry * hwe = VECTOR_LAST_SLOT(conf->hwtable);

	hwe->getuid = set_value(strvec);

	if (!hwe->getuid)
		return 1;

	return 0;
}

static int
hw_selector_handler(vector strvec)
{
	struct hwentry * hwe = VECTOR_LAST_SLOT(conf->hwtable);
	
	if (!hwe)
		return 1;

	hwe->selector = set_value(strvec);

	if (!hwe->selector)
		return 1;

	return 0;
}

static int
hw_path_checker_handler(vector strvec)
{
	char * buff;
	struct hwentry * hwe = VECTOR_LAST_SLOT(conf->hwtable);

	if (!hwe)
		return 1;

	buff = set_value(strvec);

	if (!buff)
		return 1;
	
	hwe->checker_index = get_checker_id(buff);
	FREE(buff);

	return 0;
}

static int
hw_features_handler(vector strvec)
{
	struct hwentry * hwe = VECTOR_LAST_SLOT(conf->hwtable);
	
	if (!hwe)
		return 1;

	hwe->features = set_value(strvec);

	if (!hwe->features)
		return 1;

	return 0;
}

static int
hw_handler_handler(vector strvec)
{
	struct hwentry * hwe = VECTOR_LAST_SLOT(conf->hwtable);
	
	if (!hwe)
		return 1;

	hwe->hwhandler = set_value(strvec);

	if (!hwe->hwhandler)
		return 1;

	return 0;
}

static int
prio_callout_handler(vector strvec)
{
	struct hwentry * hwe = VECTOR_LAST_SLOT(conf->hwtable);
	
	if (!hwe)
		return 1;

	hwe->getprio = set_value(strvec);

	if (!hwe->getprio)
		return 1;

	if (strlen(hwe->getprio) == 4 && !strcmp(hwe->getprio, "none")) {
		FREE(hwe->getprio);
		hwe->getprio = NULL;
	}

	return 0;
}

static int
hw_failback_handler(vector strvec)
{
	struct hwentry * hwe = VECTOR_LAST_SLOT(conf->hwtable);
	char * buff;

	if (!hwe)
		return 1;

	buff = set_value(strvec);

	if (strlen(buff) == 6 && !strcmp(buff, "manual"))
		hwe->pgfailback = -FAILBACK_MANUAL;
	else if (strlen(buff) == 9 && !strcmp(buff, "immediate"))
		hwe->pgfailback = -FAILBACK_IMMEDIATE;
	else
		hwe->pgfailback = atoi(buff);

	FREE(buff);

	return 0;
}

static int
hw_weight_handler(vector strvec)
{
	struct hwentry * hwe = VECTOR_LAST_SLOT(conf->hwtable);
	char * buff;

	if (!hwe)
		return 1;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	if (strlen(buff) == 10 &&
	    !strcmp(buff, "priorities"))
		hwe->rr_weight = RR_WEIGHT_PRIO;

	FREE(buff);

	return 0;
}

static int
hw_no_path_retry_handler(vector strvec)
{
	struct hwentry *hwe = VECTOR_LAST_SLOT(conf->hwtable);
	char *buff;

	if (!hwe)
		return 1;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if ((strlen(buff) == 4 && !strcmp(buff, "fail")) ||
	    (strlen(buff) == 1 && !strcmp(buff, "0")))
		hwe->no_path_retry = NO_PATH_RETRY_FAIL;
	else if (strlen(buff) == 5 && !strcmp(buff, "queue"))
		hwe->no_path_retry = NO_PATH_RETRY_QUEUE;
	else if ((hwe->no_path_retry = atoi(buff)) < 1)
		hwe->no_path_retry = NO_PATH_RETRY_UNDEF;

	FREE(buff);
	return 0;
}

static int
hw_minio_handler(vector strvec)
{
	struct hwentry *hwe = VECTOR_LAST_SLOT(conf->hwtable);
	char * buff;

	if (!hwe)
		return 1;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	hwe->minio = atoi(buff);
	FREE(buff);

	return 0;
}

/*
 * multipaths block handlers
 */
static int
multipaths_handler(vector strvec)
{
	conf->mptable = vector_alloc();

	if (!conf->mptable)
		return 1;

	return 0;
}

static int
multipath_handler(vector strvec)
{
	struct mpentry * mpe;

	mpe = (struct mpentry *)MALLOC(sizeof(struct mpentry));

	if (!mpe)
		return 1;

	if (!vector_alloc_slot(conf->mptable)) {
		FREE(mpe);
		return 1;
	}
	vector_set_slot(conf->mptable, mpe);

	return 0;
}

static int
wwid_handler(vector strvec)
{
	struct mpentry * mpe = VECTOR_LAST_SLOT(conf->mptable);

	if (!mpe)
		return 1;

	mpe->wwid = set_value(strvec);

	if (!mpe->wwid)
		return 1;

	return 0;
}

static int
alias_handler(vector strvec)
{
	struct mpentry * mpe = VECTOR_LAST_SLOT(conf->mptable);

	if (!mpe)
		return 1;

        mpe->alias = set_value(strvec);

	if (!mpe->alias)
		return 1;

	return 0;
}

static int
mp_pgpolicy_handler(vector strvec)
{
	char * buff;
	struct mpentry * mpe = VECTOR_LAST_SLOT(conf->mptable);

	if (!mpe)
		return 1;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	mpe->pgpolicy = get_pgpolicy_id(buff);
	FREE(buff);

	return 0;
}

static int
mp_selector_handler(vector strvec)
{
	struct mpentry * mpe = VECTOR_LAST_SLOT(conf->mptable);
	
	if (!mpe)
		return 1;

	mpe->selector = set_value(strvec);

	if (!mpe->selector)
		return 1;

	return 0;
}

static int
mp_failback_handler(vector strvec)
{
	struct mpentry * mpe = VECTOR_LAST_SLOT(conf->mptable);
	char * buff;

	if (!mpe)
		return 1;

	buff = set_value(strvec);

	if (strlen(buff) == 6 && !strcmp(buff, "manual"))
		mpe->pgfailback = -FAILBACK_MANUAL;
	else if (strlen(buff) == 9 && !strcmp(buff, "immediate"))
		mpe->pgfailback = -FAILBACK_IMMEDIATE;
	else
		mpe->pgfailback = atoi(buff);

	FREE(buff);

	return 0;
}

static int
mp_weight_handler(vector strvec)
{
	struct mpentry * mpe = VECTOR_LAST_SLOT(conf->mptable);
	char * buff;

	if (!mpe)
		return 1;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	if (strlen(buff) == 10 &&
	    !strcmp(buff, "priorities"))
		mpe->rr_weight = RR_WEIGHT_PRIO;

	FREE(buff);

	return 0;
}

static int
mp_no_path_retry_handler(vector strvec)
{
	struct mpentry *mpe = VECTOR_LAST_SLOT(conf->mptable);
	char *buff;

	if (!mpe)
		return 1;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if ((strlen(buff) == 4 && !strcmp(buff, "fail")) ||
	    (strlen(buff) == 1 && !strcmp(buff, "0")))
		mpe->no_path_retry = NO_PATH_RETRY_FAIL;
	else if (strlen(buff) == 5 && !strcmp(buff, "queue"))
		mpe->no_path_retry = NO_PATH_RETRY_QUEUE;
	else if ((mpe->no_path_retry = atoi(buff)) < 1)
		mpe->no_path_retry = NO_PATH_RETRY_UNDEF;

	FREE(buff);
	return 0;
}

static int
mp_minio_handler(vector strvec)
{
	struct mpentry *mpe = VECTOR_LAST_SLOT(conf->mptable);
	char * buff;

	if (!mpe)
		return 1;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	mpe->minio = atoi(buff);
	FREE(buff);

	return 0;
}

/*
 * config file keywords printing
 */
static int
snprint_mp_wwid (char * buff, int len, void * data)
{
	struct mpentry * mpe = (struct mpentry *)data;

	return snprintf(buff, len, "%s", mpe->wwid);
}

static int
snprint_mp_alias (char * buff, int len, void * data)
{
	struct mpentry * mpe = (struct mpentry *)data;

	if (!mpe->alias)
		return 0;

	if (conf->user_friendly_names &&
	    (strlen(mpe->alias) == strlen("mpath")) &&
	    !strcmp(mpe->alias, "mpath"))
		return 0;

	return snprintf(buff, len, "%s", mpe->alias);
}

static int
snprint_mp_path_grouping_policy (char * buff, int len, void * data)
{
	struct mpentry * mpe = (struct mpentry *)data;
	char str[POLICY_NAME_SIZE];

	if (!mpe->pgpolicy)
		return 0;
	get_pgpolicy_name(str, POLICY_NAME_SIZE, mpe->pgpolicy);
	
	return snprintf(buff, len, "%s", str);
}

static int
snprint_mp_selector (char * buff, int len, void * data)
{
	struct mpentry * mpe = (struct mpentry *)data;

	if (!mpe->selector)
		return 0;

	return snprintf(buff, len, "%s", mpe->selector);
}

static int
snprint_mp_failback (char * buff, int len, void * data)
{
	struct mpentry * mpe = (struct mpentry *)data;

	if (!mpe->pgfailback)
		return 0;

	switch(mpe->pgfailback) {
	case  FAILBACK_UNDEF:
		break;
	case -FAILBACK_MANUAL:
		return snprintf(buff, len, "manual");
	case -FAILBACK_IMMEDIATE:
		return snprintf(buff, len, "immediate");
	default:
		return snprintf(buff, len, "%i", mpe->pgfailback);
	}
	return 0;
}

static int
snprint_mp_rr_weight (char * buff, int len, void * data)
{
	struct mpentry * mpe = (struct mpentry *)data;

	if (!mpe->rr_weight)
		return 0;
	if (mpe->rr_weight == RR_WEIGHT_PRIO)
		return snprintf(buff, len, "priorities");

	return 0;
}

static int
snprint_mp_no_path_retry (char * buff, int len, void * data)
{
	struct mpentry * mpe = (struct mpentry *)data;

	if (!mpe->no_path_retry)
		return 0;

	switch(mpe->no_path_retry) {
	case NO_PATH_RETRY_UNDEF:
		break;
	case NO_PATH_RETRY_FAIL:
		return snprintf(buff, len, "fail");
	case NO_PATH_RETRY_QUEUE:
		return snprintf(buff, len, "queue");
	default:
		return snprintf(buff, len, "%i",
				mpe->no_path_retry);
	}
	return 0;
}

static int
snprint_mp_rr_min_io (char * buff, int len, void * data)
{
	struct mpentry * mpe = (struct mpentry *)data;

	if (!mpe->minio)
		return 0;

	return snprintf(buff, len, "%u", mpe->minio);
}

static int
snprint_hw_vendor (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->vendor)
		return 0;

	return snprintf(buff, len, "%s", hwe->vendor);
}

static int
snprint_hw_product (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->product)
		return 0;

	return snprintf(buff, len, "%s", hwe->product);
}

static int
snprint_hw_getuid_callout (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->getuid)
		return 0;
	if (strlen(hwe->getuid) == strlen(conf->default_getuid) &&
	    !strcmp(hwe->getuid, conf->default_getuid))
		return 0;

	return snprintf(buff, len, "%s", hwe->getuid);
}

static int
snprint_hw_prio_callout (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->getprio)
		return 0;
	if (strlen(hwe->getprio) == strlen(conf->default_getprio) &&
	    !strcmp(hwe->getprio, conf->default_getprio))
		return 0;

	return snprintf(buff, len, "%s", hwe->getprio);
}

static int
snprint_hw_features (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->features)
		return 0;
	if (strlen(hwe->features) == strlen(conf->features) &&
	    !strcmp(hwe->features, conf->features))
		return 0;

	return snprintf(buff, len, "%s", hwe->features);
}

static int
snprint_hw_hardware_handler (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->hwhandler)
		return 0;
	if (strlen(hwe->hwhandler) == strlen(conf->default_hwhandler) &&
	    !strcmp(hwe->hwhandler, conf->default_hwhandler))
		return 0;

	return snprintf(buff, len, "%s", hwe->hwhandler);
}

static int
snprint_hw_selector (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->selector)
		return 0;
	if (strlen(hwe->selector) == strlen(conf->selector) &&
	    !strcmp(hwe->selector, conf->selector))
		return 0;

	return snprintf(buff, len, "%s", hwe->selector);
}

static int
snprint_hw_path_grouping_policy (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	char str[POLICY_NAME_SIZE];

	if (!hwe->pgpolicy)
		return 0;
	if (hwe->pgpolicy == conf->default_pgpolicy)
		return 0;

	get_pgpolicy_name(str, POLICY_NAME_SIZE, hwe->pgpolicy);
	
	return snprintf(buff, len, "%s", str);
}

static int
snprint_hw_failback (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->pgfailback)
		return 0;
	if (hwe->pgfailback == conf->pgfailback)
		return 0;

	switch(hwe->pgfailback) {
	case  FAILBACK_UNDEF:
		break;
	case -FAILBACK_MANUAL:
		return snprintf(buff, len, "manual");
	case -FAILBACK_IMMEDIATE:
		return snprintf(buff, len, "immediate");
	default:
		return snprintf(buff, len, "%i", hwe->pgfailback);
	}
	return 0;
}

static int
snprint_hw_rr_weight (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->rr_weight)
		return 0;
	if (hwe->rr_weight == conf->rr_weight)
		return 0;
	if (hwe->rr_weight == RR_WEIGHT_PRIO)
		return snprintf(buff, len, "priorities");

	return 0;
}

static int
snprint_hw_no_path_retry (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->no_path_retry)
		return 0;
	if (hwe->no_path_retry == conf->no_path_retry)
		return 0;

	switch(hwe->no_path_retry) {
	case NO_PATH_RETRY_UNDEF:
		break;
	case NO_PATH_RETRY_FAIL:
		return snprintf(buff, len, "fail");
	case NO_PATH_RETRY_QUEUE:
		return snprintf(buff, len, "queue");
	default:
		return snprintf(buff, len, "%i",
				hwe->no_path_retry);
	}
	return 0;
}

static int
snprint_hw_rr_min_io (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->minio)
		return 0;
	if (hwe->minio == conf->minio)
		return 0;

	return snprintf(buff, len, "%u", hwe->minio);
}

static int
snprint_hw_path_checker (char * buff, int len, void * data)
{
	char str[CHECKER_NAME_SIZE];
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->checker_index)
		return 0;
	if (hwe->checker_index == conf->default_checker_index)
		return 0;
	get_checker_name(str, CHECKER_NAME_SIZE, hwe->checker_index);
	
	return snprintf(buff, len, "%s", str);
}

static int
snprint_def_polling_interval (char * buff, int len, void * data)
{
	if (conf->checkint == DEFAULT_CHECKINT)
		return 0;
	return snprintf(buff, len, "%i", conf->checkint);
}

static int
snprint_def_udev_dir (char * buff, int len, void * data)
{
	if (!conf->udev_dir)
		return 0;
	if (strlen(DEFAULT_UDEVDIR) == strlen(conf->udev_dir) &&
	    !strcmp(conf->udev_dir, DEFAULT_UDEVDIR))
		return 0;

	return snprintf(buff, len, "%s", conf->udev_dir);
}

static int
snprint_def_selector (char * buff, int len, void * data)
{
	if (!conf->selector)
		return 0;
	if (strlen(conf->selector) == strlen(DEFAULT_SELECTOR) &&
	    !strcmp(conf->selector, DEFAULT_SELECTOR))
		return 0;

	return snprintf(buff, len, "%s", conf->selector);
}

static int
snprint_def_path_grouping_policy (char * buff, int len, void * data)
{
	char str[POLICY_NAME_SIZE];

	if (!conf->default_pgpolicy)
		return 0;
	if (conf->default_pgpolicy == DEFAULT_PGPOLICY)
		return 0;

	get_pgpolicy_name(str, POLICY_NAME_SIZE, conf->default_pgpolicy);
	
	return snprintf(buff, len, "%s", str);
}

static int
snprint_def_getuid_callout (char * buff, int len, void * data)
{
	if (!conf->default_getuid)
		return 0;
	if (strlen(conf->default_getuid) == strlen(DEFAULT_GETUID) &&
	    !strcmp(conf->default_getuid, DEFAULT_GETUID))
		return 0;

	return snprintf(buff, len, "%s", conf->default_getuid);
}

static int
snprint_def_getprio_callout (char * buff, int len, void * data)
{
	if (!conf->default_getprio)
		return 0;
#if 0 /* default is NULL */
	if (strlen(conf->default_getprio) == strlen(DEFAULT_GETPRIO) &&
	    !strcmp(conf->default_getprio, DEFAULT_GETPRIO))
		return 0;
#endif

	return snprintf(buff, len, "%s", conf->default_getprio);
}

static int
snprint_def_features (char * buff, int len, void * data)
{
	if (!conf->features)
		return 0;
	if (strlen(conf->features) == strlen(DEFAULT_FEATURES) &&
	    !strcmp(conf->features, DEFAULT_FEATURES))
		return 0;

	return snprintf(buff, len, "%s", conf->features);
}

static int
snprint_def_path_checker (char * buff, int len, void * data)
{
	char str[CHECKER_NAME_SIZE];

	if (!conf->default_checker_index)
		return 0;
	if (conf->default_checker_index == DEFAULT_CHECKER_ID)
		return 0;
	get_checker_name(str, CHECKER_NAME_SIZE, conf->default_checker_index);
	
	return snprintf(buff, len, "%s", str);
}

static int
snprint_def_failback (char * buff, int len, void * data)
{
	if (!conf->pgfailback)
		return 0;
	if (conf->pgfailback == DEFAULT_FAILBACK)
		return 0;

	switch(conf->pgfailback) {
	case  FAILBACK_UNDEF:
		break;
	case -FAILBACK_MANUAL:
		return snprintf(buff, len, "manual");
	case -FAILBACK_IMMEDIATE:
		return snprintf(buff, len, "immediate");
	default:
		return snprintf(buff, len, "%i", conf->pgfailback);
	}
	return 0;
}

static int
snprint_def_rr_min_io (char * buff, int len, void * data)
{
	if (!conf->minio)
		return 0;
	if (conf->minio == DEFAULT_MINIO)
		return 0;

	return snprintf(buff, len, "%u", conf->minio);
}

static int
snprint_def_rr_weight (char * buff, int len, void * data)
{
	if (!conf->rr_weight)
		return 0;
	if (conf->rr_weight == DEFAULT_RR_WEIGHT)
		return 0;
	if (conf->rr_weight == RR_WEIGHT_PRIO)
		return snprintf(buff, len, "priorities");

	return 0;
}

static int
snprint_def_no_path_retry (char * buff, int len, void * data)
{
	if (conf->no_path_retry == DEFAULT_NO_PATH_RETRY)
		return 0;

	switch(conf->no_path_retry) {
	case NO_PATH_RETRY_UNDEF:
		break;
	case NO_PATH_RETRY_FAIL:
		return snprintf(buff, len, "fail");
	case NO_PATH_RETRY_QUEUE:
		return snprintf(buff, len, "queue");
	default:
		return snprintf(buff, len, "%i",
				conf->no_path_retry);
	}
	return 0;
}

static int
snprint_def_user_friendly_names (char * buff, int len, void * data)
{
	if (conf->user_friendly_names == DEFAULT_USER_FRIENDLY_NAMES)
		return 0;
	if (!conf->user_friendly_names)
		return snprintf(buff, len, "no");

	return snprintf(buff, len, "yes");
}

#define __deprecated

void
init_keywords(void)
{
	install_keyword_root("defaults", NULL);
	install_keyword("polling_interval", &polling_interval_handler, &snprint_def_polling_interval);
	install_keyword("udev_dir", &udev_dir_handler, &snprint_def_udev_dir);
	install_keyword("selector", &def_selector_handler, &snprint_def_selector);
	install_keyword("path_grouping_policy", &def_pgpolicy_handler, &snprint_def_path_grouping_policy);
	install_keyword("getuid_callout", &def_getuid_callout_handler, &snprint_def_getuid_callout);
	install_keyword("prio_callout", &def_prio_callout_handler, &snprint_def_getprio_callout);
	install_keyword("features", &def_features_handler, &snprint_def_features);
	install_keyword("path_checker", &def_path_checker_handler, &snprint_def_path_checker);
	install_keyword("failback", &default_failback_handler, &snprint_def_failback);
	install_keyword("rr_min_io", &def_minio_handler, &snprint_def_rr_min_io);
	install_keyword("rr_weight", &def_weight_handler, &snprint_def_rr_weight);
	install_keyword("no_path_retry", &def_no_path_retry_handler, &snprint_def_no_path_retry);
	install_keyword("user_friendly_names", &names_handler, &snprint_def_user_friendly_names);
	__deprecated install_keyword("default_selector", &def_selector_handler, NULL);
	__deprecated install_keyword("default_path_grouping_policy", &def_pgpolicy_handler, NULL);
	__deprecated install_keyword("default_getuid_callout", &def_getuid_callout_handler, NULL);
	__deprecated install_keyword("default_prio_callout", &def_prio_callout_handler, NULL);
	__deprecated install_keyword("default_features", &def_features_handler, NULL);
	__deprecated install_keyword("default_path_checker", &def_path_checker_handler, NULL);

	install_keyword_root("devnode", &blacklist_handler);
	install_keyword("devnode", &ble_devnode_handler, NULL);
	install_keyword("wwid", &ble_wwid_handler, NULL);
	install_keyword("device", &ble_device_handler, NULL);
	install_sublevel();
	install_keyword("vendor", &ble_vendor_handler, NULL);
	install_keyword("product", &ble_product_handler, NULL);
	install_sublevel_end();

	__deprecated install_keyword_root("devnode_blacklist", &blacklist_handler);
	__deprecated install_keyword("devnode", &ble_devnode_handler, NULL);
	__deprecated install_keyword("wwid", &ble_wwid_handler, NULL);
	__deprecated install_keyword("device", &ble_device_handler, NULL);
	__deprecated install_sublevel();
	__deprecated install_keyword("vendor", &ble_vendor_handler, NULL);
	__deprecated install_keyword("product", &ble_product_handler, NULL);
	__deprecated install_sublevel_end();

	install_keyword_root("devices", &devices_handler);
	install_keyword("device", &device_handler, NULL);
	install_sublevel();
	install_keyword("vendor", &vendor_handler, &snprint_hw_vendor);
	install_keyword("product", &product_handler, &snprint_hw_product);
	install_keyword("path_grouping_policy", &hw_pgpolicy_handler, &snprint_hw_path_grouping_policy);
	install_keyword("getuid_callout", &hw_getuid_callout_handler, &snprint_hw_getuid_callout);
	install_keyword("path_selector", &hw_selector_handler, &snprint_hw_selector);
	install_keyword("path_checker", &hw_path_checker_handler, &snprint_hw_path_checker);
	install_keyword("features", &hw_features_handler, &snprint_hw_features);
	install_keyword("hardware_handler", &hw_handler_handler, &snprint_hw_hardware_handler);
	install_keyword("prio_callout", &prio_callout_handler, &snprint_hw_prio_callout);
	install_keyword("failback", &hw_failback_handler, &snprint_hw_failback);
	install_keyword("rr_weight", &hw_weight_handler, &snprint_hw_rr_weight);
	install_keyword("no_path_retry", &hw_no_path_retry_handler, &snprint_hw_no_path_retry);
	install_keyword("rr_min_io", &hw_minio_handler, &snprint_hw_rr_min_io);
	install_sublevel_end();

	install_keyword_root("multipaths", &multipaths_handler);
	install_keyword("multipath", &multipath_handler, NULL);
	install_sublevel();
	install_keyword("wwid", &wwid_handler, &snprint_mp_wwid);
	install_keyword("alias", &alias_handler, &snprint_mp_alias);
	install_keyword("path_grouping_policy", &mp_pgpolicy_handler, &snprint_mp_path_grouping_policy);
	install_keyword("path_selector", &mp_selector_handler, &snprint_mp_selector);
	install_keyword("failback", &mp_failback_handler, &snprint_mp_failback);
	install_keyword("rr_weight", &mp_weight_handler, &snprint_mp_rr_weight);
	install_keyword("no_path_retry", &mp_no_path_retry_handler, &snprint_mp_no_path_retry);
	install_keyword("rr_min_io", &mp_minio_handler, &snprint_mp_rr_min_io);
	install_sublevel_end();
}
