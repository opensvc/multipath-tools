#include "vector.h"
#include "hwtable.h"
#include "structs.h"
#include "parser.h"
#include "config.h"
#include "debug.h"
#include "memory.h"
#include "pgpolicies.h"
#include "blacklist.h"

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
	conf->max_checkint = conf->checkint << 2;

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
	
	if (!strncmp(conf->default_getprio, "none", 4)) {
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

	if (!strncmp(buff, "manual", 6))
		conf->pgfailback = -FAILBACK_MANUAL;
	else if (!strncmp(buff, "immediate", 9))
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

	if (!strncmp(buff, "fail", 4) || !strncmp(buff, "0", 1))
		conf->no_path_retry = NO_PATH_RETRY_FAIL;
	else if (!strncmp(buff, "queue", 5))
		conf->no_path_retry = NO_PATH_RETRY_QUEUE;
	else if ((conf->no_path_retry = atoi(buff)) < 1)
		conf->no_path_retry = NO_PATH_RETRY_UNDEF;

	FREE(buff);
	return 0;
}

/*
 * blacklist block handlers
 */
static int
blacklist_handler(vector strvec)
{
	conf->blist = vector_alloc();

	if (!conf->blist)
		return 1;

	return 0;
}

static int
ble_handler(vector strvec)
{
	char * buff;
	int ret;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	ret = store_regex(conf->blist, buff);
	FREE(buff);

	return ret;
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

	if (!strncmp(hwe->getprio, "none", 4)) {
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

	if (!strncmp(buff, "manual", 6))
		hwe->pgfailback = -FAILBACK_MANUAL;
	else if (!strncmp(buff, "immediate", 9))
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

	if (!strncmp(buff, "fail", 4) || !strncmp(buff, "0", 1))
		hwe->no_path_retry = NO_PATH_RETRY_FAIL;
	else if (!strncmp(buff, "queue", 5))
		hwe->no_path_retry = NO_PATH_RETRY_QUEUE;
	else if ((hwe->no_path_retry = atoi(buff)) < 1)
		hwe->no_path_retry = NO_PATH_RETRY_UNDEF;

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

	if (!strncmp(buff, "manual", 6))
		mpe->pgfailback = -FAILBACK_MANUAL;
	else if (!strncmp(buff, "immediate", 9))
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

	if (!strncmp(buff, "fail", 4) || !strncmp(buff, "0", 1))
		mpe->no_path_retry = NO_PATH_RETRY_FAIL;
	else if (!strncmp(buff, "queue", 5))
		mpe->no_path_retry = NO_PATH_RETRY_QUEUE;
	else if ((mpe->no_path_retry = atoi(buff)) < 1)
		mpe->no_path_retry = NO_PATH_RETRY_UNDEF;

	FREE(buff);
	return 0;
}

vector
init_keywords(void)
{
	keywords = vector_alloc();

	install_keyword_root("defaults", NULL);
	install_keyword("polling_interval", &polling_interval_handler);
	install_keyword("udev_dir", &udev_dir_handler);
	install_keyword("selector", &def_selector_handler);
	install_keyword("path_grouping_policy", &def_pgpolicy_handler);
	install_keyword("getuid_callout", &def_getuid_callout_handler);
	install_keyword("prio_callout", &def_prio_callout_handler);
	install_keyword("features", &def_features_handler);
	install_keyword("path_checker", &def_path_checker_handler);
	install_keyword("failback", &default_failback_handler);
	install_keyword("rr_min_io", &def_minio_handler);
	install_keyword("rr_weight", &def_weight_handler);
	install_keyword("no_path_retry", &def_no_path_retry_handler);

	/*
	 * deprecated synonyms
	 */
	install_keyword("default_selector", &def_selector_handler);
	install_keyword("default_path_grouping_policy", &def_pgpolicy_handler);
	install_keyword("default_getuid_callout", &def_getuid_callout_handler);
	install_keyword("default_prio_callout", &def_prio_callout_handler);
	install_keyword("default_features", &def_features_handler);
	install_keyword("default_path_checker", &def_path_checker_handler);

	install_keyword_root("devnode_blacklist", &blacklist_handler);
	install_keyword("devnode", &ble_handler);
	install_keyword("wwid", &ble_handler);

	install_keyword_root("devices", &devices_handler);
	install_keyword("device", &device_handler);
	install_sublevel();
	install_keyword("vendor", &vendor_handler);
	install_keyword("product", &product_handler);
	install_keyword("path_grouping_policy", &hw_pgpolicy_handler);
	install_keyword("getuid_callout", &hw_getuid_callout_handler);
	install_keyword("path_selector", &hw_selector_handler);
	install_keyword("path_checker", &hw_path_checker_handler);
	install_keyword("features", &hw_features_handler);
	install_keyword("hardware_handler", &hw_handler_handler);
	install_keyword("prio_callout", &prio_callout_handler);
	install_keyword("failback", &hw_failback_handler);
	install_keyword("rr_weight", &hw_weight_handler);
	install_keyword("no_path_retry", &hw_no_path_retry_handler);
	install_sublevel_end();

	install_keyword_root("multipaths", &multipaths_handler);
	install_keyword("multipath", &multipath_handler);
	install_sublevel();
	install_keyword("wwid", &wwid_handler);
	install_keyword("alias", &alias_handler);
	install_keyword("path_grouping_policy", &mp_pgpolicy_handler);
	install_keyword("path_selector", &mp_selector_handler);
	install_keyword("failback", &mp_failback_handler);
	install_keyword("rr_weight", &mp_weight_handler);
	install_keyword("no_path_retry", &mp_no_path_retry_handler);
	install_sublevel_end();

	return keywords;
}
