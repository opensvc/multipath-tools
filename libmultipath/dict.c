/*
 * Based on Alexandre Cassen template for keepalived
 * Copyright (c) 2004, 2005, 2006  Christophe Varoqui
 * Copyright (c) 2005 Benjamin Marzinski, Redhat
 * Copyright (c) 2005 Kiyoshi Ueda, NEC
 */
#include <sys/types.h>
#include <pwd.h>
#include <string.h>
#include "checkers.h"
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
#include "prio.h"
#include "errno.h"
#include <inttypes.h>

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
def_fast_io_fail_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);
	if (strlen(buff) == 3 && !strcmp(buff, "off"))
		conf->fast_io_fail = MP_FAST_IO_FAIL_OFF;
	else if (sscanf(buff, "%d", &conf->fast_io_fail) != 1 ||
		 conf->fast_io_fail < MP_FAST_IO_FAIL_ZERO)
		conf->fast_io_fail = MP_FAST_IO_FAIL_UNSET;
	else if (conf->fast_io_fail == 0)
		conf->fast_io_fail = MP_FAST_IO_FAIL_ZERO;

	FREE(buff);
	return 0;
}

static int
def_dev_loss_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if (strlen(buff) == 8 && !strcmp(buff, "infinity"))
		conf->dev_loss = MAX_DEV_LOSS_TMO;
	else if (sscanf(buff, "%u", &conf->dev_loss) != 1)
		conf->dev_loss = 0;

	FREE(buff);
	return 0;
}

static int
verbosity_handler(vector strvec)
{
	char * buff;

	buff = VECTOR_SLOT(strvec, 1);
	conf->verbosity = atoi(buff);

	return 0;
}

static int
max_polling_interval_handler(vector strvec)
{
	char *buff;

	buff = VECTOR_SLOT(strvec, 1);
	conf->max_checkint = atoi(buff);

	return 0;
}

static int
reassign_maps_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);
	if (!strcmp(buff, "yes"))
		conf->reassign_maps = 1;
	else if (!strcmp(buff, "no"))
		conf->reassign_maps = 0;
	else
		return 1;

	return 0;
}

static int
multipath_dir_handler(vector strvec)
{
	conf->multipath_dir = set_value(strvec);

	if (!conf->multipath_dir)
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

	conf->pgpolicy = get_pgpolicy_id(buff);
	FREE(buff);

	return 0;
}

static int
def_uid_attribute_handler(vector strvec)
{
	conf->uid_attribute = set_value(strvec);

	if (!conf->uid_attribute)
		return 1;

	return 0;
}

static int
def_getuid_callout_handler(vector strvec)
{
	conf->getuid = set_value(strvec);

	if (!conf->getuid)
		return 1;

	return 0;
}

static int
def_prio_handler(vector strvec)
{
	conf->prio_name = set_value(strvec);

	if (!conf->prio_name)
		return 1;

	return 0;
}

static int
def_alias_prefix_handler(vector strvec)
{
	conf->alias_prefix = set_value(strvec);

	if (!conf->alias_prefix)
		return 1;

	return 0;
}

static int
def_prio_args_handler(vector strvec)
{
	conf->prio_args = set_value(strvec);

	if (!conf->prio_args)
		return 1;

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
	conf->checker_name = set_value(strvec);

	if (!conf->checker_name)
		return 1;

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
def_minio_rq_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	conf->minio_rq = atoi(buff);
	FREE(buff);

	return 0;
}

int
get_sys_max_fds(int *max_fds)
{
	FILE *file;
	int nr_open;
	int ret = 1;

	file = fopen("/proc/sys/fs/nr_open", "r");
	if (!file) {
		fprintf(stderr, "Cannot open /proc/sys/fs/nr_open : %s\n",
			strerror(errno));
		return 1;
	}
	if (fscanf(file, "%d", &nr_open) != 1) {
		fprintf(stderr, "Cannot read max open fds from /proc/sys/fs/nr_open");
		if (ferror(file))
			fprintf(stderr, " : %s\n", strerror(errno));
		else
			fprintf(stderr, "\n");
	} else {
		*max_fds = nr_open;
		ret = 0;
	}
	fclose(file);
	return ret;
}


static int
max_fds_handler(vector strvec)
{
	char * buff;
	int r = 0, max_fds;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	r = get_sys_max_fds(&max_fds);
	if (r) {
		/* Assume safe limit */
		max_fds = 4096;
	}
	if (strlen(buff) == 3 &&
	    !strcmp(buff, "max"))
		conf->max_fds = max_fds;
	else
		conf->max_fds = atoi(buff);

	if (conf->max_fds > max_fds)
		conf->max_fds = max_fds;

	FREE(buff);

	return r;
}

static int
def_mode_handler(vector strvec)
{
	mode_t mode;
	char *buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	if (sscanf(buff, "%o", &mode) == 1 && mode <= 0777) {
		conf->attribute_flags |= (1 << ATTR_MODE);
		conf->mode = mode;
	}

	FREE(buff);
	return 0;
}

static int
def_uid_handler(vector strvec)
{
	uid_t uid;
	char *buff;
	char passwd_buf[1024];
	struct passwd info, *found;

	buff = set_value(strvec);
	if (!buff)
		return 1;
	if (getpwnam_r(buff, &info, passwd_buf, 1024, &found) == 0 && found) {
		conf->attribute_flags |= (1 << ATTR_UID);
		conf->uid = info.pw_uid;
	}
	else if (sscanf(buff, "%u", &uid) == 1){
		conf->attribute_flags |= (1 << ATTR_UID);
		conf->uid = uid;
	}

	FREE(buff);
	return 0;
}

static int
def_gid_handler(vector strvec)
{
	gid_t gid;
	char *buff;
	char passwd_buf[1024];
	struct passwd info, *found;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if (getpwnam_r(buff, &info, passwd_buf, 1024, &found) == 0 && found) {
		conf->attribute_flags |= (1 << ATTR_GID);
		conf->gid = info.pw_gid;
	}
	else if (sscanf(buff, "%u", &gid) == 1){
		conf->attribute_flags |= (1 << ATTR_GID);
		conf->gid = gid;
	}
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

	if (strlen(buff) == strlen("uniform") &&
	    !strcmp(buff, "uniform"))
		conf->rr_weight = RR_WEIGHT_NONE;

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
	else if (strlen(buff) == 10 && !strcmp(buff, "followover"))
		conf->pgfailback = -FAILBACK_FOLLOWOVER;
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
def_queue_without_daemon(vector strvec)
{
	char * buff;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if (!strncmp(buff, "on", 2) || !strncmp(buff, "yes", 3) ||
		 !strncmp(buff, "1", 1))
		conf->queue_without_daemon = QUE_NO_DAEMON_ON;
	else
		conf->queue_without_daemon = QUE_NO_DAEMON_OFF;

	free(buff);
	return 0;
}

static int
def_checker_timeout_handler(vector strvec)
{
	unsigned int checker_timeout;
	char *buff;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if (sscanf(buff, "%u", &checker_timeout) == 1)
		conf->checker_timeout = checker_timeout;
	else
		conf->checker_timeout = 0;

	free(buff);
	return 0;
}

static int
def_pg_timeout_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	/* Deprecated; device-mapper support has been removed */

	FREE(buff);
	return 0;
}

static int
def_flush_on_last_del_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if ((strlen(buff) == 2 && strcmp(buff, "no") == 0) ||
	    (strlen(buff) == 1 && strcmp(buff, "0") == 0))
		conf->flush_on_last_del = FLUSH_DISABLED;
	else if ((strlen(buff) == 3 && strcmp(buff, "yes") == 0) ||
	    (strlen(buff) == 1 && strcmp(buff, "1") == 0))
		conf->flush_on_last_del = FLUSH_ENABLED;
	else
		conf->flush_on_last_del = FLUSH_UNDEF;

	FREE(buff);
	return 0;
}

static int
def_log_checker_err_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	if (strlen(buff) == 4 && !strcmp(buff, "once"))
		conf->log_checker_err = LOG_CHKR_ERR_ONCE;
	else if (strlen(buff) == 6 && !strcmp(buff, "always"))
		conf->log_checker_err = LOG_CHKR_ERR_ALWAYS;

	free(buff);
	return 0;
}

static int
def_reservation_key_handler(vector strvec)
{
	char *buff;
	char *tbuff;
	int j, k;
	int len;
	uint64_t prkey;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	tbuff = buff;

	if (!memcmp("0x",buff, 2))
		buff = buff + 2;

	len = strlen(buff);

	k = strspn(buff, "0123456789aAbBcCdDeEfF");

	if (len != k) {
		FREE(tbuff);
		return 1;
	}

	if (1 != sscanf (buff, "%" SCNx64 "", &prkey))
	{
		FREE(tbuff);
		return 1;
	}

	if (!conf->reservation_key)
		conf->reservation_key = (unsigned char *) malloc(8);

	memset(conf->reservation_key, 0, 8);

	for (j = 7; j >= 0; --j) {
		conf->reservation_key[j] = (prkey & 0xff);
		prkey >>= 8;
	}

	FREE(tbuff);
	return 0;
}

static int
def_names_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	if ((strlen(buff) == 2 && !strcmp(buff, "no")) ||
	    (strlen(buff) == 1 && !strcmp(buff, "0")))
		conf->user_friendly_names = USER_FRIENDLY_NAMES_OFF;
	else if ((strlen(buff) == 3 && !strcmp(buff, "yes")) ||
		 (strlen(buff) == 1 && !strcmp(buff, "1")))
		conf->user_friendly_names = USER_FRIENDLY_NAMES_ON;
	else
		conf->user_friendly_names = USER_FRIENDLY_NAMES_UNDEF;

	FREE(buff);
	return 0;
}

static int
bindings_file_handler(vector strvec)
{
	conf->bindings_file = set_value(strvec);

	if (!conf->bindings_file)
		return 1;

	return 0;
}

static int
wwids_file_handler(vector strvec)
{
	conf->wwids_file = set_value(strvec);

	if (!conf->wwids_file)
		return 1;

	return 0;
}

static int
def_retain_hwhandler_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	if ((strlen(buff) == 2 && !strcmp(buff, "no")) ||
	    (strlen(buff) == 1 && !strcmp(buff, "0")))
		conf->retain_hwhandler = RETAIN_HWHANDLER_OFF;
	else if ((strlen(buff) == 3 && !strcmp(buff, "yes")) ||
		 (strlen(buff) == 1 && !strcmp(buff, "1")))
		conf->retain_hwhandler = RETAIN_HWHANDLER_ON;
	else
		conf->retain_hwhandler = RETAIN_HWHANDLER_UNDEF;

	FREE(buff);
	return 0;
}

static int
def_detect_prio_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	if ((strlen(buff) == 2 && !strcmp(buff, "no")) ||
	    (strlen(buff) == 1 && !strcmp(buff, "0")))
		conf->detect_prio = DETECT_PRIO_OFF;
	else if ((strlen(buff) == 3 && !strcmp(buff, "yes")) ||
		 (strlen(buff) == 1 && !strcmp(buff, "1")))
		conf->detect_prio = DETECT_PRIO_ON;
	else
		conf->detect_prio = DETECT_PRIO_UNDEF;

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
	conf->blist_property = vector_alloc();

	if (!conf->blist_devnode || !conf->blist_wwid ||
	    !conf->blist_device || !conf->blist_property)
		return 1;

	return 0;
}

static int
blacklist_exceptions_handler(vector strvec)
{
	conf->elist_devnode = vector_alloc();
	conf->elist_wwid = vector_alloc();
	conf->elist_device = vector_alloc();
	conf->elist_property = vector_alloc();

	if (!conf->elist_devnode || !conf->elist_wwid ||
	    !conf->elist_device || !conf->elist_property)
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

	return store_ble(conf->blist_devnode, buff, ORIGIN_CONFIG);
}

static int
ble_except_devnode_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	return store_ble(conf->elist_devnode, buff, ORIGIN_CONFIG);
}

static int
ble_wwid_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	return store_ble(conf->blist_wwid, buff, ORIGIN_CONFIG);
}

static int
ble_except_wwid_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	return store_ble(conf->elist_wwid, buff, ORIGIN_CONFIG);
}

static int
ble_property_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	return store_ble(conf->blist_property, buff, ORIGIN_CONFIG);
}

static int
ble_except_property_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	return store_ble(conf->elist_property, buff, ORIGIN_CONFIG);
}

static int
ble_device_handler(vector strvec)
{
	return alloc_ble_device(conf->blist_device);
}

static int
ble_except_device_handler(vector strvec)
{
	return alloc_ble_device(conf->elist_device);
}

static int
ble_vendor_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	return set_ble_device(conf->blist_device, buff, NULL, ORIGIN_CONFIG);
}

static int
ble_except_vendor_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	return set_ble_device(conf->elist_device, buff, NULL, ORIGIN_CONFIG);
}

static int
ble_product_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	return set_ble_device(conf->blist_device, NULL, buff, ORIGIN_CONFIG);
}

static int
ble_except_product_handler(vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	return set_ble_device(conf->elist_device, NULL, buff, ORIGIN_CONFIG);
}

/*
 * devices block handlers
 */
static int
devices_handler(vector strvec)
{
	if (!conf->hwtable)
		conf->hwtable = vector_alloc();

	if (!conf->hwtable)
		return 1;

	return 0;
}

static int
device_handler(vector strvec)
{
	struct hwentry * hwe;

	hwe = alloc_hwe();

	if (!hwe)
		return 1;

	if (!vector_alloc_slot(conf->hwtable)) {
		free_hwe(hwe);
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
revision_handler(vector strvec)
{
	struct hwentry * hwe = VECTOR_LAST_SLOT(conf->hwtable);

	if (!hwe)
		return 1;

	hwe->revision = set_value(strvec);

	if (!hwe->revision)
		return 1;

	return 0;
}

static int
bl_product_handler(vector strvec)
{
	struct hwentry * hwe = VECTOR_LAST_SLOT(conf->hwtable);

	if (!hwe)
		return 1;

	hwe->bl_product = set_value(strvec);
	if (!hwe->bl_product)
		return 1;

	return 0;
}

static int
hw_fast_io_fail_handler(vector strvec)
{
	char * buff;
	struct hwentry * hwe = VECTOR_LAST_SLOT(conf->hwtable);

	buff = set_value(strvec);
	if (strlen(buff) == 3 && !strcmp(buff, "off"))
		hwe->fast_io_fail = MP_FAST_IO_FAIL_OFF;
	else if (sscanf(buff, "%d", &hwe->fast_io_fail) != 1 ||
		 hwe->fast_io_fail < MP_FAST_IO_FAIL_ZERO)
		hwe->fast_io_fail = MP_FAST_IO_FAIL_UNSET;
	else if (hwe->fast_io_fail == 0)
		hwe->fast_io_fail = MP_FAST_IO_FAIL_ZERO;

	FREE(buff);
	return 0;
}

static int
hw_dev_loss_handler(vector strvec)
{
	char * buff;
	struct hwentry * hwe = VECTOR_LAST_SLOT(conf->hwtable);

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if (strlen(buff) == 8 && !strcmp(buff, "infinity"))
		hwe->dev_loss = MAX_DEV_LOSS_TMO;
	else if (sscanf(buff, "%u", &hwe->dev_loss) != 1)
		hwe->dev_loss = 0;

	FREE(buff);
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
hw_uid_attribute_handler(vector strvec)
{
	struct hwentry * hwe = VECTOR_LAST_SLOT(conf->hwtable);

	hwe->uid_attribute = set_value(strvec);

	if (!hwe->uid_attribute)
		return 1;

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
	struct hwentry * hwe = VECTOR_LAST_SLOT(conf->hwtable);

	if (!hwe)
		return 1;

	hwe->checker_name = set_value(strvec);

	if (!hwe->checker_name)
		return 1;

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
hw_prio_handler(vector strvec)
{
	struct hwentry * hwe = VECTOR_LAST_SLOT(conf->hwtable);

	if (!hwe)
		return 1;

	hwe->prio_name = set_value(strvec);

	if (!hwe->prio_name)
		return 1;

	return 0;
}

static int
hw_alias_prefix_handler(vector strvec)
{
	struct hwentry * hwe = VECTOR_LAST_SLOT(conf->hwtable);

	if (!hwe)
		return 1;

	hwe->alias_prefix = set_value(strvec);

	if (!hwe->alias_prefix)
		return 1;

	return 0;
}

static int
hw_prio_args_handler(vector strvec)
{
	struct hwentry * hwe = VECTOR_LAST_SLOT(conf->hwtable);

	if (!hwe)
		return 1;

	hwe->prio_args = set_value(strvec);

	if (!hwe->prio_args)
		return 1;

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
	else if (strlen(buff) == 10 && !strcmp(buff, "followover"))
		hwe->pgfailback = -FAILBACK_FOLLOWOVER;
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

	if (strlen(buff) == strlen("uniform") &&
	    !strcmp(buff, "uniform"))
		hwe->rr_weight = RR_WEIGHT_NONE;

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

static int
hw_minio_rq_handler(vector strvec)
{
	struct hwentry *hwe = VECTOR_LAST_SLOT(conf->hwtable);
	char * buff;

	if (!hwe)
		return 1;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	hwe->minio_rq = atoi(buff);
	FREE(buff);

	return 0;
}

static int
hw_pg_timeout_handler(vector strvec)
{
	char *buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	/* Deprecated; device-mapper support has been removed */

	FREE(buff);
	return 0;
}

static int
hw_flush_on_last_del_handler(vector strvec)
{
	struct hwentry *hwe = VECTOR_LAST_SLOT(conf->hwtable);
	char * buff;

	if (!hwe)
		return 1;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if ((strlen(buff) == 2 && strcmp(buff, "no") == 0) ||
	    (strlen(buff) == 1 && strcmp(buff, "0") == 0))
		hwe->flush_on_last_del = FLUSH_DISABLED;
	else if ((strlen(buff) == 3 && strcmp(buff, "yes") == 0) ||
	    (strlen(buff) == 1 && strcmp(buff, "1") == 0))
		hwe->flush_on_last_del = FLUSH_ENABLED;
	else
		hwe->flush_on_last_del = FLUSH_UNDEF;

	FREE(buff);
	return 0;
}

static int
hw_names_handler(vector strvec)
{
	struct hwentry *hwe = VECTOR_LAST_SLOT(conf->hwtable);
	char * buff;

	if (!hwe)
		return 1;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if ((strlen(buff) == 2 && strcmp(buff, "no") == 0) ||
	    (strlen(buff) == 1 && strcmp(buff, "0") == 0))
		hwe->user_friendly_names = USER_FRIENDLY_NAMES_OFF;
	else if ((strlen(buff) == 3 && strcmp(buff, "yes") == 0) ||
		 (strlen(buff) == 1 && strcmp(buff, "1") == 0))
		hwe->user_friendly_names = USER_FRIENDLY_NAMES_ON;
	else
		hwe->user_friendly_names = USER_FRIENDLY_NAMES_UNDEF;

	FREE(buff);
	return 0;
}

static int
hw_retain_hwhandler_handler(vector strvec)
{
	struct hwentry *hwe = VECTOR_LAST_SLOT(conf->hwtable);
	char * buff;

	if (!hwe)
		return 1;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	if ((strlen(buff) == 2 && !strcmp(buff, "no")) ||
	    (strlen(buff) == 1 && !strcmp(buff, "0")))
		hwe->retain_hwhandler = RETAIN_HWHANDLER_OFF;
	else if ((strlen(buff) == 3 && !strcmp(buff, "yes")) ||
		 (strlen(buff) == 1 && !strcmp(buff, "1")))
		hwe->retain_hwhandler = RETAIN_HWHANDLER_ON;
	else
		hwe->user_friendly_names = RETAIN_HWHANDLER_UNDEF;

	FREE(buff);
	return 0;
}

static int
hw_detect_prio_handler(vector strvec)
{
	struct hwentry *hwe = VECTOR_LAST_SLOT(conf->hwtable);
	char * buff;

	if (!hwe)
		return 1;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	if ((strlen(buff) == 2 && !strcmp(buff, "no")) ||
	    (strlen(buff) == 1 && !strcmp(buff, "0")))
		hwe->detect_prio = DETECT_PRIO_OFF;
	else if ((strlen(buff) == 3 && !strcmp(buff, "yes")) ||
		 (strlen(buff) == 1 && !strcmp(buff, "1")))
		hwe->detect_prio = DETECT_PRIO_ON;
	else
		hwe->detect_prio = DETECT_PRIO_UNDEF;

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

	mpe = alloc_mpe();

	if (!mpe)
		return 1;

	if (!vector_alloc_slot(conf->mptable)) {
		free_mpe(mpe);
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
	else if (strlen(buff) == 10 && !strcmp(buff, "followover"))
		mpe->pgfailback = -FAILBACK_FOLLOWOVER;
	else
		mpe->pgfailback = atoi(buff);

	FREE(buff);

	return 0;
}

static int
mp_mode_handler(vector strvec)
{
	mode_t mode;
	struct mpentry *mpe = VECTOR_LAST_SLOT(conf->mptable);
	char *buff;

	if (!mpe)
		return 1;

	buff = set_value(strvec);
	if (!buff)
		return 1;
	if (sscanf(buff, "%o", &mode) == 1 && mode <= 0777) {
		mpe->attribute_flags |= (1 << ATTR_MODE);
		mpe->mode = mode;
	}

	FREE(buff);
	return 0;
}

static int
mp_uid_handler(vector strvec)
{
	uid_t uid;
	char *buff;
	char passwd_buf[1024];
	struct passwd info, *found;

	struct mpentry *mpe = VECTOR_LAST_SLOT(conf->mptable);

	if (!mpe)
		return 1;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if (getpwnam_r(buff, &info, passwd_buf, 1024, &found) == 0 && found) {
		mpe->attribute_flags |= (1 << ATTR_UID);
		mpe->uid = info.pw_uid;
	}
	else if (sscanf(buff, "%u", &uid) == 1){
		mpe->attribute_flags |= (1 << ATTR_UID);
		mpe->uid = uid;
	}
	FREE(buff);
	return 0;
}

static int
mp_gid_handler(vector strvec)
{
	gid_t gid;
	char *buff;
	char passwd_buf[1024];
	struct passwd info, *found;

	struct mpentry *mpe = VECTOR_LAST_SLOT(conf->mptable);

	if (!mpe)
		return 1;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if (getpwnam_r(buff, &info, passwd_buf, 1024, &found) == 0 && found) {
		mpe->attribute_flags |= (1 << ATTR_GID);
		mpe->gid = info.pw_gid;
	}
	else if (sscanf(buff, "%u", &gid) == 1) {
		mpe->attribute_flags |= (1 << ATTR_GID);
		mpe->gid = gid;
	}
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

	if (strlen(buff) == strlen("uniform") &&
	    !strcmp(buff, "uniform"))
		mpe->rr_weight = RR_WEIGHT_NONE;

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

static int
mp_minio_rq_handler(vector strvec)
{
	struct mpentry *mpe = VECTOR_LAST_SLOT(conf->mptable);
	char * buff;

	if (!mpe)
		return 1;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	mpe->minio_rq = atoi(buff);
	FREE(buff);

	return 0;
}

static int
mp_pg_timeout_handler(vector strvec)
{
	char *buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	/* Deprecated; device-mapper support has been removed */

	FREE(buff);
	return 0;
}

static int
mp_features_handler(vector strvec)
{
	struct mpentry * mpe = VECTOR_LAST_SLOT(conf->mptable);

	if (!mpe)
		return 1;

	mpe->features = set_value(strvec);

	if (!mpe->features)
		return 1;

	return 0;
}

static int
mp_flush_on_last_del_handler(vector strvec)
{
	struct mpentry *mpe = VECTOR_LAST_SLOT(conf->mptable);
	char * buff;

	if (!mpe)
		return 1;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if ((strlen(buff) == 2 && strcmp(buff, "no") == 0) ||
	    (strlen(buff) == 1 && strcmp(buff, "0") == 0))
		mpe->flush_on_last_del = FLUSH_DISABLED;
	else if ((strlen(buff) == 3 && strcmp(buff, "yes") == 0) ||
	    (strlen(buff) == 1 && strcmp(buff, "1") == 0))
		mpe->flush_on_last_del = FLUSH_ENABLED;
	else
		mpe->flush_on_last_del = FLUSH_UNDEF;

	FREE(buff);
	return 0;
}

static int
mp_prio_handler(vector strvec)
{
	struct mpentry * mpe = VECTOR_LAST_SLOT(conf->mptable);

	if (!mpe)
		return 1;

	mpe->prio_name = set_value(strvec);

	if (!mpe->prio_name)
		return 1;

	return 0;
}

static int
mp_prio_args_handler (vector strvec)
{
	struct mpentry *mpe = VECTOR_LAST_SLOT(conf->mptable);

	if (!mpe)
		return 1;

	mpe->prio_args = set_value(strvec);
	if (!mpe->prio_args)
		return 1;

	return 0;
}

static int
mp_reservation_key_handler (vector strvec)
{
	char *buff;
	char *tbuff;
	struct mpentry *mpe = VECTOR_LAST_SLOT(conf->mptable);

	int j, k, len;
	uint64_t prkey;

	if (!mpe)
		return 1;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	tbuff = buff;
	if (!memcmp(buff, "0x", 2))
		buff = buff + 2;

	len = strlen(buff);

	k = strspn(buff, "0123456789aAbBcCdDeEfF");
	if (len != k) {
		FREE(tbuff);
		return 1;
	}

	if (1 != sscanf (buff, "%" SCNx64 "", &prkey))
	{
		FREE(tbuff);
		return 1;
	}

	if (!mpe->reservation_key)
		mpe->reservation_key = (unsigned char *) malloc(8);

	memset(mpe->reservation_key, 0, 8);

	for (j = 7; j >= 0; --j) {
		mpe->reservation_key[j] = (prkey & 0xff);
		prkey >>= 8;
	}

	FREE(tbuff);
	return 0;
}

static int
mp_names_handler(vector strvec)
{
	struct mpentry *mpe = VECTOR_LAST_SLOT(conf->mptable);
	char * buff;

	if (!mpe)
		return 1;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if ((strlen(buff) == 2 && strcmp(buff, "no") == 0) ||
	    (strlen(buff) == 1 && strcmp(buff, "0") == 0))
		mpe->user_friendly_names = USER_FRIENDLY_NAMES_OFF;
	else if ((strlen(buff) == 3 && strcmp(buff, "yes") == 0) ||
		 (strlen(buff) == 1 && strcmp(buff, "1") == 0))
		mpe->user_friendly_names = USER_FRIENDLY_NAMES_ON;
	else
		mpe->user_friendly_names = USER_FRIENDLY_NAMES_UNDEF;

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

	return snprintf(buff, len, "\"%s\"", str);
}

static int
snprint_mp_selector (char * buff, int len, void * data)
{
	struct mpentry * mpe = (struct mpentry *)data;

	if (!mpe->selector)
		return 0;

	return snprintf(buff, len, "\"%s\"", mpe->selector);
}

static int
snprint_mp_failback (char * buff, int len, void * data)
{
	struct mpentry * mpe = (struct mpentry *)data;

	if (mpe->pgfailback == FAILBACK_UNDEF ||
	    mpe->pgfailback == DEFAULT_FAILBACK)
		return 0;

	switch(mpe->pgfailback) {
	case -FAILBACK_MANUAL:
		return snprintf(buff, len, "\"manual\"");
	case -FAILBACK_IMMEDIATE:
		return snprintf(buff, len, "\"immediate\"");
	case -FAILBACK_FOLLOWOVER:
		return snprintf(buff, len, "\"followover\"");
	default:
		return snprintf(buff, len, "%i", mpe->pgfailback);
	}
	return 0;
}

static int
snprint_mp_mode(char * buff, int len, void * data)
{
	struct mpentry * mpe = (struct mpentry *)data;

	if ((mpe->attribute_flags & (1 << ATTR_MODE)) == 0)
		return 0;
	return snprintf(buff, len, "0%o", mpe->mode);
}

static int
snprint_mp_uid(char * buff, int len, void * data)
{
	struct mpentry * mpe = (struct mpentry *)data;

	if ((mpe->attribute_flags & (1 << ATTR_UID)) == 0)
		return 0;
	return snprintf(buff, len, "0%o", mpe->uid);
}

static int
snprint_mp_gid(char * buff, int len, void * data)
{
	struct mpentry * mpe = (struct mpentry *)data;

	if ((mpe->attribute_flags & (1 << ATTR_GID)) == 0)
		return 0;
	return snprintf(buff, len, "0%o", mpe->gid);
}

static int
snprint_mp_rr_weight (char * buff, int len, void * data)
{
	struct mpentry * mpe = (struct mpentry *)data;

	if (!mpe->rr_weight)
		return 0;
	if (mpe->rr_weight == RR_WEIGHT_PRIO)
		return snprintf(buff, len, "\"priorities\"");
	if (mpe->rr_weight == RR_WEIGHT_NONE)
		return snprintf(buff, len, "\"uniform\"");

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
		return snprintf(buff, len, "\"fail\"");
	case NO_PATH_RETRY_QUEUE:
		return snprintf(buff, len, "\"queue\"");
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
snprint_mp_rr_min_io_rq (char * buff, int len, void * data)
{
	struct mpentry * mpe = (struct mpentry *)data;

	if (!mpe->minio_rq)
		return 0;

	return snprintf(buff, len, "%u", mpe->minio_rq);
}

static int
snprint_mp_pg_timeout (char * buff, int len, void * data)
{
	return 0;
}

static int
snprint_mp_features (char * buff, int len, void * data)
{
	struct mpentry * mpe = (struct mpentry *)data;

	if (!mpe->features)
		return 0;
	if (strlen(mpe->features) == strlen(conf->features) &&
	    !strcmp(mpe->features, conf->features))
		return 0;

	return snprintf(buff, len, "\"%s\"", mpe->features);
}

static int
snprint_mp_flush_on_last_del (char * buff, int len, void * data)
{
	struct mpentry * mpe = (struct mpentry *)data;

	switch (mpe->flush_on_last_del) {
	case FLUSH_DISABLED:
		return snprintf(buff, len, "\"no\"");
	case FLUSH_ENABLED:
		return snprintf(buff, len, "\"yes\"");
	}
	return 0;
}

static int
snprint_mp_prio(char * buff, int len, void * data)
{
	struct mpentry * mpe = (struct mpentry *)data;

	if (!mpe->prio_name)
		return 0;

	return snprintf(buff, len, "\"%s\"", mpe->prio_name);
}

static int
snprint_mp_prio_args(char * buff, int len, void * data)
{
	struct mpentry * mpe = (struct mpentry *)data;

	if (!mpe->prio_args)
		return 0;

	return snprintf(buff, len, "\"%s\"", mpe->prio_args);
}

static int
snprint_mp_reservation_key (char * buff, int len, void * data)
{
	int i;
	unsigned char *keyp;
	uint64_t prkey = 0;
	struct mpentry * mpe = (struct mpentry *)data;

	if (!mpe->reservation_key)
		return 0;
	keyp = (unsigned char *)mpe->reservation_key;
	for (i = 0; i < 8; i++) {
		if (i > 0)
			prkey <<= 8;
		prkey |= *keyp;
		keyp++;
	}

	return snprintf(buff, len, "0x%" PRIx64, prkey);
}

	static int
snprint_mp_user_friendly_names (char * buff, int len, void * data)
{
	struct mpentry * mpe = (struct mpentry *)data;

	if (mpe->user_friendly_names == USER_FRIENDLY_NAMES_UNDEF)
		return 0;
	else if (mpe->user_friendly_names == USER_FRIENDLY_NAMES_OFF)
		return snprintf(buff, len, "\"no\"");
	else
		return snprintf(buff, len, "\"yes\"");
}

static int
snprint_hw_fast_io_fail(char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;
	if (hwe->fast_io_fail == MP_FAST_IO_FAIL_UNSET)
		return 0;
	if (hwe->fast_io_fail == conf->fast_io_fail)
		return 0;
	if (hwe->fast_io_fail == MP_FAST_IO_FAIL_OFF)
		return snprintf(buff, len, "\"off\"");
	if (hwe->fast_io_fail == MP_FAST_IO_FAIL_ZERO)
		return snprintf(buff, len, "0");
	return snprintf(buff, len, "%d", hwe->fast_io_fail);
}

static int
snprint_hw_dev_loss(char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;
	if (!hwe->dev_loss)
		return 0;
	if (hwe->dev_loss == conf->dev_loss)
		return 0;
	if (hwe->dev_loss >= MAX_DEV_LOSS_TMO)
		return snprintf(buff, len, "\"infinity\"");

	return snprintf(buff, len, "%u", hwe->dev_loss);
}

static int
snprint_hw_vendor (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->vendor)
		return 0;

	return snprintf(buff, len, "\"%s\"", hwe->vendor);
}

static int
snprint_hw_product (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->product)
		return 0;

	return snprintf(buff, len, "\"%s\"", hwe->product);
}

static int
snprint_hw_revision (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->revision)
		return 0;

	return snprintf(buff, len, "\"%s\"", hwe->revision);
}

static int
snprint_hw_bl_product (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->bl_product)
		return 0;

	return snprintf(buff, len, "\"%s\"", hwe->bl_product);
}

static int
snprint_hw_uid_attribute (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->uid_attribute)
		return 0;

	return snprintf(buff, len, "\"%s\"", hwe->uid_attribute);
}

static int
snprint_hw_getuid_callout (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->getuid)
		return 0;

	return snprintf(buff, len, "\"%s\"", hwe->getuid);
}

static int
snprint_hw_prio (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->prio_name)
		return 0;

	return snprintf(buff, len, "\"%s\"", hwe->prio_name);
}

static int
snprint_hw_alias_prefix (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->alias_prefix)
		return 0;

	return snprintf(buff, len, "\"%s\"", hwe->alias_prefix);
}

static int
snprint_hw_prio_args (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->prio_args)
		return 0;

	return snprintf(buff, len, "\"%s\"", hwe->prio_args);
}

static int
snprint_hw_features (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->features)
		return 0;

	return snprintf(buff, len, "\"%s\"", hwe->features);
}

static int
snprint_hw_hardware_handler (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->hwhandler)
		return 0;

	return snprintf(buff, len, "\"%s\"", hwe->hwhandler);
}

static int
snprint_hw_selector (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->selector)
		return 0;

	return snprintf(buff, len, "\"%s\"", hwe->selector);
}

static int
snprint_hw_path_grouping_policy (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	char str[POLICY_NAME_SIZE];

	if (!hwe->pgpolicy)
		return 0;

	get_pgpolicy_name(str, POLICY_NAME_SIZE, hwe->pgpolicy);

	return snprintf(buff, len, "\"%s\"", str);
}

static int
snprint_hw_failback (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (hwe->pgfailback == FAILBACK_UNDEF ||
	    hwe->pgfailback == DEFAULT_FAILBACK)
		return 0;

	switch(hwe->pgfailback) {
	case -FAILBACK_MANUAL:
		return snprintf(buff, len, "\"manual\"");
	case -FAILBACK_IMMEDIATE:
		return snprintf(buff, len, "\"immediate\"");
	case -FAILBACK_FOLLOWOVER:
		return snprintf(buff, len, "\"followover\"");
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
	if (hwe->rr_weight == RR_WEIGHT_PRIO)
		return snprintf(buff, len, "\"priorities\"");
	if (hwe->rr_weight == RR_WEIGHT_NONE)
		return snprintf(buff, len, "\"uniform\"");

	return 0;
}

static int
snprint_hw_no_path_retry (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->no_path_retry)
		return 0;

	switch(hwe->no_path_retry) {
	case NO_PATH_RETRY_UNDEF:
		break;
	case NO_PATH_RETRY_FAIL:
		return snprintf(buff, len, "\"fail\"");
	case NO_PATH_RETRY_QUEUE:
		return snprintf(buff, len, "\"queue\"");
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

	return snprintf(buff, len, "%u", hwe->minio);
}

static int
snprint_hw_rr_min_io_rq (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->minio_rq)
		return 0;

	return snprintf(buff, len, "%u", hwe->minio_rq);
}

static int
snprint_hw_pg_timeout (char * buff, int len, void * data)
{
	return 0;
}

static int
snprint_hw_flush_on_last_del (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	switch (hwe->flush_on_last_del) {
	case FLUSH_DISABLED:
		return snprintf(buff, len, "\"no\"");
	case FLUSH_ENABLED:
		return snprintf(buff, len, "\"yes\"");
	}
	return 0;
}

static int
snprint_hw_path_checker (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (!hwe->checker_name)
		return 0;

	return snprintf(buff, len, "\"%s\"", hwe->checker_name);
}

	static int
snprint_hw_user_friendly_names (char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (hwe->user_friendly_names == USER_FRIENDLY_NAMES_UNDEF)
		return 0;
	else if (hwe->user_friendly_names == USER_FRIENDLY_NAMES_OFF)
		return snprintf(buff, len, "\"no\"");
	else
		return snprintf(buff, len, "\"yes\"");
}

static int
snprint_hw_retain_hwhandler_handler(char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (hwe->retain_hwhandler == RETAIN_HWHANDLER_ON)
		return snprintf(buff, len, "\"yes\"");
	else if (hwe->retain_hwhandler == RETAIN_HWHANDLER_OFF)
		return snprintf(buff, len, "\"no\"");
	else
		return 0;
}

static int
snprint_detect_prio(char * buff, int len, void * data)
{
	struct hwentry * hwe = (struct hwentry *)data;

	if (hwe->detect_prio == DETECT_PRIO_ON)
		return snprintf(buff, len, "\"yes\"");
	else if (hwe->detect_prio == DETECT_PRIO_OFF)
		return snprintf(buff, len, "\"no\"");
	else
		return 0;
}

static int
snprint_def_polling_interval (char * buff, int len, void * data)
{
	return snprintf(buff, len, "%i", conf->checkint);
}

static int
snprint_def_fast_io_fail(char * buff, int len, void * data)
{
	if (conf->fast_io_fail == MP_FAST_IO_FAIL_UNSET)
		return 0;
	if (conf->fast_io_fail == MP_FAST_IO_FAIL_OFF)
		return snprintf(buff, len, "\"off\"");
	if (conf->fast_io_fail == MP_FAST_IO_FAIL_ZERO)
		return snprintf(buff, len, "0");
	return snprintf(buff, len, "%d", conf->fast_io_fail);
}

static int
snprint_def_dev_loss(char * buff, int len, void * data)
{
	if (!conf->dev_loss)
		return 0;
	if (conf->dev_loss >= MAX_DEV_LOSS_TMO)
		return snprintf(buff, len, "\"infinity\"");
	return snprintf(buff, len, "%u", conf->dev_loss);
}

static int
snprint_def_verbosity (char * buff, int len, void * data)
{
	return snprintf(buff, len, "%i", conf->verbosity);
}

static int
snprint_def_max_polling_interval (char * buff, int len, void * data)
{
	return snprintf(buff, len, "%i", conf->max_checkint);
}

static int
snprint_reassign_maps (char * buff, int len, void * data)
{
	return snprintf(buff, len, "\"%s\"",
			conf->reassign_maps?"yes":"no");
}

static int
snprint_def_multipath_dir (char * buff, int len, void * data)
{
	if (!conf->multipath_dir)
		return 0;

	return snprintf(buff, len, "\"%s\"", conf->multipath_dir);
}

static int
snprint_def_selector (char * buff, int len, void * data)
{
	if (!conf->selector)
		return snprintf(buff, len, "\"%s\"", DEFAULT_SELECTOR);

	return snprintf(buff, len, "\"%s\"", conf->selector);
}

static int
snprint_def_path_grouping_policy (char * buff, int len, void * data)
{
	char str[POLICY_NAME_SIZE];
	int pgpolicy = conf->pgpolicy;

	if (!pgpolicy)
		pgpolicy = DEFAULT_PGPOLICY;

	get_pgpolicy_name(str, POLICY_NAME_SIZE, pgpolicy);

	return snprintf(buff, len, "\"%s\"", str);
}

static int
snprint_def_uid_attribute (char * buff, int len, void * data)
{
	if (!conf->uid_attribute)
		return snprintf(buff, len, "\"%s\"", DEFAULT_UID_ATTRIBUTE);

	return snprintf(buff, len, "\"%s\"", conf->uid_attribute);
}

static int
snprint_def_getuid_callout (char * buff, int len, void * data)
{
	if (!conf->getuid)
		return 0;

	return snprintf(buff, len, "\"%s\"", conf->getuid);
}

static int
snprint_def_prio (char * buff, int len, void * data)
{
	if (!conf->prio_name)
		return snprintf(buff, len, "\"%s\"", DEFAULT_PRIO);

	return snprintf(buff, len, "\"%s\"", conf->prio_name);
}

static int
snprint_def_prio_args (char * buff, int len, void * data)
{
	if (!conf->prio_args)
		return snprintf(buff, len, "\"%s\"", DEFAULT_PRIO_ARGS);

	return snprintf(buff, len, "\"%s\"", conf->prio_args);
}

static int
snprint_def_features (char * buff, int len, void * data)
{
	if (!conf->features)
		return snprintf(buff, len, "\"%s\"", DEFAULT_FEATURES);

	return snprintf(buff, len, "\"%s\"", conf->features);
}

static int
snprint_def_path_checker (char * buff, int len, void * data)
{
	if (!conf->checker_name)
		return snprintf(buff, len, "\"%s\"", DEFAULT_CHECKER);

	return snprintf(buff, len, "\"%s\"", conf->checker_name);
}

static int
snprint_def_failback (char * buff, int len, void * data)
{
	switch(conf->pgfailback) {
	case  FAILBACK_UNDEF:
		return snprintf(buff, len, "\"undef\"");
	case -FAILBACK_MANUAL:
		return snprintf(buff, len, "\"manual\"");
	case -FAILBACK_IMMEDIATE:
		return snprintf(buff, len, "\"immediate\"");
	case -FAILBACK_FOLLOWOVER:
		return snprintf(buff, len, "\"followover\"");
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

	return snprintf(buff, len, "%u", conf->minio);
}

static int
snprint_def_rr_min_io_rq (char * buff, int len, void * data)
{
	if (!conf->minio_rq)
		return 0;

	return snprintf(buff, len, "%u", conf->minio_rq);
}

static int
snprint_max_fds (char * buff, int len, void * data)
{
	int r = 0, max_fds;

	if (!conf->max_fds)
		return 0;

	r = get_sys_max_fds(&max_fds);
	if (!r && conf->max_fds >= max_fds)
		return snprintf(buff, len, "\"max\"");
	else
		return snprintf(buff, len, "%d", conf->max_fds);
}

static int
snprint_def_mode(char * buff, int len, void * data)
{
	if ((conf->attribute_flags & (1 << ATTR_MODE)) == 0)
		return 0;
	return snprintf(buff, len, "0%o", conf->mode);
}

static int
snprint_def_uid(char * buff, int len, void * data)
{
	if ((conf->attribute_flags & (1 << ATTR_UID)) == 0)
		return 0;
	return snprintf(buff, len, "0%o", conf->uid);
}

static int
snprint_def_gid(char * buff, int len, void * data)
{
	if ((conf->attribute_flags & (1 << ATTR_GID)) == 0)
		return 0;
	return snprintf(buff, len, "0%o", conf->gid);
}

static int
snprint_def_rr_weight (char * buff, int len, void * data)
{
	if (!conf->rr_weight || conf->rr_weight == RR_WEIGHT_NONE)
		return snprintf(buff, len, "\"uniform\"");
	if (conf->rr_weight == RR_WEIGHT_PRIO)
		return snprintf(buff, len, "\"priorities\"");

	return 0;
}

static int
snprint_def_no_path_retry (char * buff, int len, void * data)
{
	switch(conf->no_path_retry) {
	case NO_PATH_RETRY_UNDEF:
		break;
	case NO_PATH_RETRY_FAIL:
		return snprintf(buff, len, "\"fail\"");
	case NO_PATH_RETRY_QUEUE:
		return snprintf(buff, len, "\"queue\"");
	default:
		return snprintf(buff, len, "%i",
				conf->no_path_retry);
	}
	return 0;
}

static int
snprint_def_queue_without_daemon (char * buff, int len, void * data)
{
	switch (conf->queue_without_daemon) {
	case QUE_NO_DAEMON_OFF:
		return snprintf(buff, len, "\"no\"");
	case QUE_NO_DAEMON_ON:
		return snprintf(buff, len, "\"yes\"");
	case QUE_NO_DAEMON_FORCE:
		return snprintf(buff, len, "\"forced\"");
	}
	return 0;
}

static int
snprint_def_checker_timeout (char *buff, int len, void *data)
{
	if (!conf->checker_timeout)
		return 0;

	return snprintf(buff, len, "%u", conf->checker_timeout);
}

static int
snprint_def_pg_timeout (char * buff, int len, void * data)
{
	return 0;
}

static int
snprint_def_flush_on_last_del (char * buff, int len, void * data)
{
	switch (conf->flush_on_last_del) {
	case FLUSH_UNDEF:
	case FLUSH_DISABLED:
		return snprintf(buff, len, "\"no\"");
	case FLUSH_ENABLED:
	case FLUSH_IN_PROGRESS:
		return snprintf(buff, len, "\"yes\"");
	}
	return 0;
}

static int
snprint_def_log_checker_err (char * buff, int len, void * data)
{
	if (conf->log_checker_err == LOG_CHKR_ERR_ONCE)
		return snprintf(buff, len, "once");
	return snprintf(buff, len, "always");
}

static int
snprint_def_user_friendly_names (char * buff, int len, void * data)
{
	if (conf->user_friendly_names  == USER_FRIENDLY_NAMES_ON)
		return snprintf(buff, len, "\"yes\"");
	else
		return snprintf(buff, len, "\"no\"");
}

static int
snprint_def_alias_prefix (char * buff, int len, void * data)
{
	if (!conf->alias_prefix)
		return snprintf(buff, len, "\"%s\"", DEFAULT_ALIAS_PREFIX);
	return snprintf(buff, len, "\"%s\"", conf->alias_prefix);
}

static int
snprint_def_bindings_file (char * buff, int len, void * data)
{
	if (conf->bindings_file == NULL)
		return 0;
	return snprintf(buff, len, "\"%s\"", conf->bindings_file);
}

static int
snprint_def_wwids_file (char * buff, int len, void * data)
{
	if (conf->wwids_file == NULL)
		return 0;
	return snprintf(buff, len, "%s", conf->wwids_file);
}

static int
snprint_def_reservation_key(char * buff, int len, void * data)
{
	int i;
	unsigned char *keyp;
	uint64_t prkey = 0;

	if (!conf->reservation_key)
		return 0;
	keyp = (unsigned char *)conf->reservation_key;
	for (i = 0; i < 8; i++) {
		if (i > 0)
			prkey <<= 8;
		prkey |= *keyp;
		keyp++;
	}
	return snprintf(buff, len, "0x%" PRIx64, prkey);
}

static int
snprint_def_retain_hwhandler_handler(char * buff, int len, void * data)
{
	if (conf->retain_hwhandler == RETAIN_HWHANDLER_ON)
		return snprintf(buff, len, "yes");
	else
		return snprintf(buff, len, "no");
}

static int
snprint_def_detect_prio(char * buff, int len, void * data)
{
	if (conf->detect_prio == DETECT_PRIO_ON)
		return snprintf(buff, len, "yes");
	else
		return snprintf(buff, len, "no");
}

static int
snprint_ble_simple (char * buff, int len, void * data)
{
	struct blentry * ble = (struct blentry *)data;

	return snprintf(buff, len, "\"%s\"", ble->str);
}

static int
snprint_bled_vendor (char * buff, int len, void * data)
{
	struct blentry_device * bled = (struct blentry_device *)data;

	return snprintf(buff, len, "\"%s\"", bled->vendor);
}

static int
snprint_bled_product (char * buff, int len, void * data)
{
	struct blentry_device * bled = (struct blentry_device *)data;

	return snprintf(buff, len, "\"%s\"", bled->product);
}

#define __deprecated

void
init_keywords(void)
{
	install_keyword_root("defaults", NULL);
	install_keyword("verbosity", &verbosity_handler, &snprint_def_verbosity);
	install_keyword("polling_interval", &polling_interval_handler, &snprint_def_polling_interval);
	install_keyword("max_polling_interval", &max_polling_interval_handler, &snprint_def_max_polling_interval);
	install_keyword("reassign_maps", &reassign_maps_handler, &snprint_reassign_maps);
	install_keyword("multipath_dir", &multipath_dir_handler, &snprint_def_multipath_dir);
	install_keyword("path_selector", &def_selector_handler, &snprint_def_selector);
	install_keyword("path_grouping_policy", &def_pgpolicy_handler, &snprint_def_path_grouping_policy);
	install_keyword("uid_attribute", &def_uid_attribute_handler, &snprint_def_uid_attribute);
	install_keyword("getuid_callout", &def_getuid_callout_handler, &snprint_def_getuid_callout);
	install_keyword("prio", &def_prio_handler, &snprint_def_prio);
	install_keyword("prio_args", &def_prio_args_handler, &snprint_def_prio_args);
	install_keyword("features", &def_features_handler, &snprint_def_features);
	install_keyword("path_checker", &def_path_checker_handler, &snprint_def_path_checker);
	install_keyword("checker", &def_path_checker_handler, NULL);
	install_keyword("alias_prefix", &def_alias_prefix_handler, &snprint_def_alias_prefix);
	install_keyword("failback", &default_failback_handler, &snprint_def_failback);
	install_keyword("rr_min_io", &def_minio_handler, &snprint_def_rr_min_io);
	install_keyword("rr_min_io_rq", &def_minio_rq_handler, &snprint_def_rr_min_io_rq);
	install_keyword("max_fds", &max_fds_handler, &snprint_max_fds);
	install_keyword("rr_weight", &def_weight_handler, &snprint_def_rr_weight);
	install_keyword("no_path_retry", &def_no_path_retry_handler, &snprint_def_no_path_retry);
	install_keyword("queue_without_daemon", &def_queue_without_daemon, &snprint_def_queue_without_daemon);
	install_keyword("checker_timeout", &def_checker_timeout_handler, &snprint_def_checker_timeout);
	install_keyword("pg_timeout", &def_pg_timeout_handler, &snprint_def_pg_timeout);
	install_keyword("flush_on_last_del", &def_flush_on_last_del_handler, &snprint_def_flush_on_last_del);
	install_keyword("user_friendly_names", &def_names_handler, &snprint_def_user_friendly_names);
	install_keyword("mode", &def_mode_handler, &snprint_def_mode);
	install_keyword("uid", &def_uid_handler, &snprint_def_uid);
	install_keyword("gid", &def_gid_handler, &snprint_def_gid);
	install_keyword("fast_io_fail_tmo", &def_fast_io_fail_handler, &snprint_def_fast_io_fail);
	install_keyword("dev_loss_tmo", &def_dev_loss_handler, &snprint_def_dev_loss);
	install_keyword("bindings_file", &bindings_file_handler, &snprint_def_bindings_file);
	install_keyword("wwids_file", &wwids_file_handler, &snprint_def_wwids_file);
	install_keyword("log_checker_err", &def_log_checker_err_handler, &snprint_def_log_checker_err);
	install_keyword("reservation_key", &def_reservation_key_handler, &snprint_def_reservation_key);
	install_keyword("retain_attached_hw_handler", &def_retain_hwhandler_handler, &snprint_def_retain_hwhandler_handler);
	install_keyword("detect_prio", &def_detect_prio_handler, &snprint_def_detect_prio);
	__deprecated install_keyword("default_selector", &def_selector_handler, NULL);
	__deprecated install_keyword("default_path_grouping_policy", &def_pgpolicy_handler, NULL);
	__deprecated install_keyword("default_uid_attribute", &def_uid_attribute_handler, NULL);
	__deprecated install_keyword("default_getuid_callout", &def_getuid_callout_handler, NULL);
	__deprecated install_keyword("default_features", &def_features_handler, NULL);
	__deprecated install_keyword("default_path_checker", &def_path_checker_handler, NULL);

	install_keyword_root("blacklist", &blacklist_handler);
	install_keyword_multi("devnode", &ble_devnode_handler, &snprint_ble_simple);
	install_keyword_multi("wwid", &ble_wwid_handler, &snprint_ble_simple);
	install_keyword_multi("property", &ble_property_handler, &snprint_ble_simple);
	install_keyword_multi("device", &ble_device_handler, NULL);
	install_sublevel();
	install_keyword("vendor", &ble_vendor_handler, &snprint_bled_vendor);
	install_keyword("product", &ble_product_handler, &snprint_bled_product);
	install_sublevel_end();
	install_keyword_root("blacklist_exceptions", &blacklist_exceptions_handler);
	install_keyword_multi("devnode", &ble_except_devnode_handler, &snprint_ble_simple);
	install_keyword_multi("wwid", &ble_except_wwid_handler, &snprint_ble_simple);
	install_keyword_multi("property", &ble_except_property_handler, &snprint_ble_simple);
	install_keyword_multi("device", &ble_except_device_handler, NULL);
	install_sublevel();
	install_keyword("vendor", &ble_except_vendor_handler, &snprint_bled_vendor);
	install_keyword("product", &ble_except_product_handler, &snprint_bled_product);
	install_sublevel_end();

#if 0
	__deprecated install_keyword_root("devnode_blacklist", &blacklist_handler);
	__deprecated install_keyword("devnode", &ble_devnode_handler, &snprint_ble_simple);
	__deprecated install_keyword("wwid", &ble_wwid_handler, &snprint_ble_simple);
	__deprecated install_keyword("device", &ble_device_handler, NULL);
	__deprecated install_sublevel();
	__deprecated install_keyword("vendor", &ble_vendor_handler, &snprint_bled_vendor);
	__deprecated install_keyword("product", &ble_product_handler, &snprint_bled_product);
	__deprecated install_sublevel_end();
#endif

	install_keyword_root("devices", &devices_handler);
	install_keyword_multi("device", &device_handler, NULL);
	install_sublevel();
	install_keyword("vendor", &vendor_handler, &snprint_hw_vendor);
	install_keyword("product", &product_handler, &snprint_hw_product);
	install_keyword("revision", &revision_handler, &snprint_hw_revision);
	install_keyword("product_blacklist", &bl_product_handler, &snprint_hw_bl_product);
	install_keyword("path_grouping_policy", &hw_pgpolicy_handler, &snprint_hw_path_grouping_policy);
	install_keyword("uid_attribute", &hw_uid_attribute_handler, &snprint_hw_uid_attribute);
	install_keyword("getuid_callout", &hw_getuid_callout_handler, &snprint_hw_getuid_callout);
	install_keyword("path_selector", &hw_selector_handler, &snprint_hw_selector);
	install_keyword("path_checker", &hw_path_checker_handler, &snprint_hw_path_checker);
	install_keyword("checker", &hw_path_checker_handler, NULL);
	install_keyword("alias_prefix", &hw_alias_prefix_handler, &snprint_hw_alias_prefix);
	install_keyword("features", &hw_features_handler, &snprint_hw_features);
	install_keyword("hardware_handler", &hw_handler_handler, &snprint_hw_hardware_handler);
	install_keyword("prio", &hw_prio_handler, &snprint_hw_prio);
	install_keyword("prio_args", &hw_prio_args_handler, &snprint_hw_prio_args);
	install_keyword("failback", &hw_failback_handler, &snprint_hw_failback);
	install_keyword("rr_weight", &hw_weight_handler, &snprint_hw_rr_weight);
	install_keyword("no_path_retry", &hw_no_path_retry_handler, &snprint_hw_no_path_retry);
	install_keyword("rr_min_io", &hw_minio_handler, &snprint_hw_rr_min_io);
	install_keyword("rr_min_io_rq", &hw_minio_rq_handler, &snprint_hw_rr_min_io_rq);
	install_keyword("pg_timeout", &hw_pg_timeout_handler, &snprint_hw_pg_timeout);
	install_keyword("flush_on_last_del", &hw_flush_on_last_del_handler, &snprint_hw_flush_on_last_del);
	install_keyword("fast_io_fail_tmo", &hw_fast_io_fail_handler, &snprint_hw_fast_io_fail);
	install_keyword("dev_loss_tmo", &hw_dev_loss_handler, &snprint_hw_dev_loss);
	install_keyword("user_friendly_names", &hw_names_handler, &snprint_hw_user_friendly_names);
	install_keyword("retain_attached_hw_handler", &hw_retain_hwhandler_handler, &snprint_hw_retain_hwhandler_handler);
	install_keyword("detect_prio", &hw_detect_prio_handler, &snprint_detect_prio);
	install_sublevel_end();

	install_keyword_root("multipaths", &multipaths_handler);
	install_keyword_multi("multipath", &multipath_handler, NULL);
	install_sublevel();
	install_keyword("wwid", &wwid_handler, &snprint_mp_wwid);
	install_keyword("alias", &alias_handler, &snprint_mp_alias);
	install_keyword("path_grouping_policy", &mp_pgpolicy_handler, &snprint_mp_path_grouping_policy);
	install_keyword("path_selector", &mp_selector_handler, &snprint_mp_selector);
	install_keyword("prio", &mp_prio_handler, &snprint_mp_prio);
	install_keyword("prio_args", &mp_prio_args_handler, &snprint_mp_prio_args);
	install_keyword("failback", &mp_failback_handler, &snprint_mp_failback);
	install_keyword("rr_weight", &mp_weight_handler, &snprint_mp_rr_weight);
	install_keyword("no_path_retry", &mp_no_path_retry_handler, &snprint_mp_no_path_retry);
	install_keyword("rr_min_io", &mp_minio_handler, &snprint_mp_rr_min_io);
	install_keyword("rr_min_io_rq", &mp_minio_rq_handler, &snprint_mp_rr_min_io_rq);
	install_keyword("pg_timeout", &mp_pg_timeout_handler, &snprint_mp_pg_timeout);
	install_keyword("flush_on_last_del", &mp_flush_on_last_del_handler, &snprint_mp_flush_on_last_del);
	install_keyword("features", &mp_features_handler, &snprint_mp_features);
	install_keyword("mode", &mp_mode_handler, &snprint_mp_mode);
	install_keyword("uid", &mp_uid_handler, &snprint_mp_uid);
	install_keyword("gid", &mp_gid_handler, &snprint_mp_gid);
	install_keyword("reservation_key", &mp_reservation_key_handler, &snprint_mp_reservation_key);
	install_keyword("user_friendly_names", &mp_names_handler, &snprint_mp_user_friendly_names);
	install_sublevel_end();
}
