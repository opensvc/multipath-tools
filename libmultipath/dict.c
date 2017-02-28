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
#include <errno.h>
#include <inttypes.h>
#include "mpath_cmd.h"

static int
set_int(vector strvec, void *ptr)
{
	int *int_ptr = (int *)ptr;
	char * buff;

	buff = VECTOR_SLOT(strvec, 1);
	*int_ptr = atoi(buff);

	return 0;
}

static int
set_str(vector strvec, void *ptr)
{
	char **str_ptr = (char **)ptr;

	if (*str_ptr)
		FREE(*str_ptr);
	*str_ptr = set_value(strvec);

	if (!*str_ptr)
		return 1;

	return 0;
}

static int
set_yes_no(vector strvec, void *ptr)
{
	char * buff;
	int *int_ptr = (int *)ptr;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if (strcmp(buff, "yes") == 0 || strcmp(buff, "1") == 0)
		*int_ptr = YN_YES;
	else
		*int_ptr = YN_NO;

	FREE(buff);
	return 0;
}

static int
set_yes_no_undef(vector strvec, void *ptr)
{
	char * buff;
	int *int_ptr = (int *)ptr;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if (strcmp(buff, "no") == 0 || strcmp(buff, "0") == 0)
		*int_ptr = YNU_NO;
	else if (strcmp(buff, "yes") == 0 || strcmp(buff, "1") == 0)
		*int_ptr = YNU_YES;
	else
		*int_ptr = YNU_UNDEF;

	FREE(buff);
	return 0;
}

static int
print_int (char *buff, int len, void *ptr)
{
	int *int_ptr = (int *)ptr;
	return snprintf(buff, len, "%i", *int_ptr);
}

static int
print_nonzero (char *buff, int len, void *ptr)
{
	int *int_ptr = (int *)ptr;
	if (!*int_ptr)
		return 0;
	return snprintf(buff, len, "%i", *int_ptr);
}

static int
print_str (char *buff, int len, void *ptr)
{
	char **str_ptr = (char **)ptr;
	if (!*str_ptr)
		return 0;
	return snprintf(buff, len, "\"%s\"", *str_ptr);
}

static int
print_yes_no (char *buff, int len, void *ptr)
{
	int *int_ptr = (int *)ptr;
	return snprintf(buff, len, "\"%s\"",
			(*int_ptr == YN_NO)? "no" : "yes");
}

static int
print_yes_no_undef (char *buff, int len, void *ptr)
{
	int *int_ptr = (int *)ptr;
	if (!*int_ptr)
		return 0;
	return snprintf(buff, len, "\"%s\"",
			(*int_ptr == YNU_NO)? "no" : "yes");
}

#define declare_def_handler(option, function)				\
static int								\
def_ ## option ## _handler (struct config *conf, vector strvec)		\
{									\
	return function (strvec, &conf->option);			\
}

#define declare_def_snprint(option, function)				\
static int								\
snprint_def_ ## option (struct config *conf, char * buff, int len, void * data) \
{									\
	return function (buff, len, &conf->option);			\
}

#define declare_def_snprint_defint(option, function, value)		\
static int								\
snprint_def_ ## option (struct config *conf, char * buff, int len, void * data) \
{									\
	int i = value;							\
	if (!conf->option)						\
		return function (buff, len, &i);			\
	return function (buff, len, &conf->option);			\
}

#define declare_def_snprint_defstr(option, function, value)		\
static int								\
snprint_def_ ## option (struct config *conf, char * buff, int len, void * data) \
{									\
	char *s = value;						\
	if (!conf->option)						\
		return function (buff, len, &s);			\
	return function (buff, len, &conf->option);			\
}

#define declare_hw_handler(option, function)				\
static int								\
hw_ ## option ## _handler (struct config *conf, vector strvec)		\
{									\
	struct hwentry * hwe = VECTOR_LAST_SLOT(conf->hwtable);		\
	if (!hwe)							\
		return 1;						\
	return function (strvec, &hwe->option);				\
}

#define declare_hw_snprint(option, function)				\
static int								\
snprint_hw_ ## option (struct config *conf, char * buff, int len, void * data) \
{									\
	struct hwentry * hwe = (struct hwentry *)data;			\
	return function (buff, len, &hwe->option);			\
}

#define declare_ovr_handler(option, function)				\
static int								\
ovr_ ## option ## _handler (struct config *conf, vector strvec)		\
{									\
	if (!conf->overrides)						\
		return 1;						\
	return function (strvec, &conf->overrides->option);		\
}

#define declare_ovr_snprint(option, function)				\
static int								\
snprint_ovr_ ## option (struct config *conf, char * buff, int len, void * data) \
{									\
	return function (buff, len, &conf->overrides->option);		\
}

#define declare_mp_handler(option, function)				\
static int								\
mp_ ## option ## _handler (struct config *conf, vector strvec)		\
{									\
	struct mpentry * mpe = VECTOR_LAST_SLOT(conf->mptable);		\
	if (!mpe)							\
		return 1;						\
	return function (strvec, &mpe->option);				\
}

#define declare_mp_snprint(option, function)				\
static int								\
snprint_mp_ ## option (struct config *conf, char * buff, int len, void * data) \
{									\
	struct mpentry * mpe = (struct mpentry *)data;			\
	return function (buff, len, &mpe->option);			\
}

declare_def_handler(checkint, set_int)
declare_def_snprint(checkint, print_int)

declare_def_handler(max_checkint, set_int)
declare_def_snprint(max_checkint, print_int)

declare_def_handler(verbosity, set_int)
declare_def_snprint(verbosity, print_int)

declare_def_handler(reassign_maps, set_yes_no)
declare_def_snprint(reassign_maps, print_yes_no)

declare_def_handler(multipath_dir, set_str)
declare_def_snprint(multipath_dir, print_str)

declare_def_handler(partition_delim, set_str)
declare_def_snprint(partition_delim, print_str)

declare_def_handler(find_multipaths, set_yes_no)
declare_def_snprint(find_multipaths, print_yes_no)

declare_def_handler(selector, set_str)
declare_def_snprint_defstr(selector, print_str, DEFAULT_SELECTOR)
declare_hw_handler(selector, set_str)
declare_hw_snprint(selector, print_str)
declare_ovr_handler(selector, set_str)
declare_ovr_snprint(selector, print_str)
declare_mp_handler(selector, set_str)
declare_mp_snprint(selector, print_str)

declare_def_handler(uid_attrs, set_str)
declare_def_snprint(uid_attrs, print_str)
declare_def_handler(uid_attribute, set_str)
declare_def_snprint_defstr(uid_attribute, print_str, DEFAULT_UID_ATTRIBUTE)
declare_ovr_handler(uid_attribute, set_str)
declare_ovr_snprint(uid_attribute, print_str)
declare_hw_handler(uid_attribute, set_str)
declare_hw_snprint(uid_attribute, print_str)

declare_def_handler(getuid, set_str)
declare_def_snprint(getuid, print_str)
declare_ovr_handler(getuid, set_str)
declare_ovr_snprint(getuid, print_str)
declare_hw_handler(getuid, set_str)
declare_hw_snprint(getuid, print_str)

declare_def_handler(prio_name, set_str)
declare_def_snprint_defstr(prio_name, print_str, DEFAULT_PRIO)
declare_ovr_handler(prio_name, set_str)
declare_ovr_snprint(prio_name, print_str)
declare_hw_handler(prio_name, set_str)
declare_hw_snprint(prio_name, print_str)
declare_mp_handler(prio_name, set_str)
declare_mp_snprint(prio_name, print_str)

declare_def_handler(alias_prefix, set_str)
declare_def_snprint_defstr(alias_prefix, print_str, DEFAULT_ALIAS_PREFIX)
declare_ovr_handler(alias_prefix, set_str)
declare_ovr_snprint(alias_prefix, print_str)
declare_hw_handler(alias_prefix, set_str)
declare_hw_snprint(alias_prefix, print_str)

declare_def_handler(prio_args, set_str)
declare_def_snprint_defstr(prio_args, print_str, DEFAULT_PRIO_ARGS)
declare_ovr_handler(prio_args, set_str)
declare_ovr_snprint(prio_args, print_str)
declare_hw_handler(prio_args, set_str)
declare_hw_snprint(prio_args, print_str)
declare_mp_handler(prio_args, set_str)
declare_mp_snprint(prio_args, print_str)

declare_def_handler(features, set_str)
declare_def_snprint_defstr(features, print_str, DEFAULT_FEATURES)
declare_ovr_handler(features, set_str)
declare_ovr_snprint(features, print_str)
declare_hw_handler(features, set_str)
declare_hw_snprint(features, print_str)
declare_mp_handler(features, set_str)
declare_mp_snprint(features, print_str)

declare_def_handler(checker_name, set_str)
declare_def_snprint_defstr(checker_name, print_str, DEFAULT_CHECKER)
declare_ovr_handler(checker_name, set_str)
declare_ovr_snprint(checker_name, print_str)
declare_hw_handler(checker_name, set_str)
declare_hw_snprint(checker_name, print_str)

declare_def_handler(minio, set_int)
declare_def_snprint_defint(minio, print_int, DEFAULT_MINIO)
declare_ovr_handler(minio, set_int)
declare_ovr_snprint(minio, print_nonzero)
declare_hw_handler(minio, set_int)
declare_hw_snprint(minio, print_nonzero)
declare_mp_handler(minio, set_int)
declare_mp_snprint(minio, print_nonzero)

declare_def_handler(minio_rq, set_int)
declare_def_snprint_defint(minio_rq, print_int, DEFAULT_MINIO_RQ)
declare_ovr_handler(minio_rq, set_int)
declare_ovr_snprint(minio_rq, print_nonzero)
declare_hw_handler(minio_rq, set_int)
declare_hw_snprint(minio_rq, print_nonzero)
declare_mp_handler(minio_rq, set_int)
declare_mp_snprint(minio_rq, print_nonzero)

declare_def_handler(queue_without_daemon, set_yes_no)
static int
snprint_def_queue_without_daemon (struct config *conf,
				  char * buff, int len, void * data)
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

declare_def_handler(checker_timeout, set_int)
declare_def_snprint(checker_timeout, print_nonzero)

declare_def_handler(flush_on_last_del, set_yes_no_undef)
declare_def_snprint_defint(flush_on_last_del, print_yes_no_undef, YNU_NO)
declare_ovr_handler(flush_on_last_del, set_yes_no_undef)
declare_ovr_snprint(flush_on_last_del, print_yes_no_undef)
declare_hw_handler(flush_on_last_del, set_yes_no_undef)
declare_hw_snprint(flush_on_last_del, print_yes_no_undef)
declare_mp_handler(flush_on_last_del, set_yes_no_undef)
declare_mp_snprint(flush_on_last_del, print_yes_no_undef)

declare_def_handler(user_friendly_names, set_yes_no_undef)
declare_def_snprint_defint(user_friendly_names, print_yes_no_undef, YNU_NO)
declare_ovr_handler(user_friendly_names, set_yes_no_undef)
declare_ovr_snprint(user_friendly_names, print_yes_no_undef)
declare_hw_handler(user_friendly_names, set_yes_no_undef)
declare_hw_snprint(user_friendly_names, print_yes_no_undef)
declare_mp_handler(user_friendly_names, set_yes_no_undef)
declare_mp_snprint(user_friendly_names, print_yes_no_undef)

declare_def_handler(bindings_file, set_str)
declare_def_snprint(bindings_file, print_str)

declare_def_handler(wwids_file, set_str)
declare_def_snprint(wwids_file, print_str)

declare_def_handler(retain_hwhandler, set_yes_no_undef)
declare_def_snprint_defint(retain_hwhandler, print_yes_no_undef, YNU_NO)
declare_ovr_handler(retain_hwhandler, set_yes_no_undef)
declare_ovr_snprint(retain_hwhandler, print_yes_no_undef)
declare_hw_handler(retain_hwhandler, set_yes_no_undef)
declare_hw_snprint(retain_hwhandler, print_yes_no_undef)

declare_def_handler(detect_prio, set_yes_no_undef)
declare_def_snprint_defint(detect_prio, print_yes_no_undef, YNU_NO)
declare_ovr_handler(detect_prio, set_yes_no_undef)
declare_ovr_snprint(detect_prio, print_yes_no_undef)
declare_hw_handler(detect_prio, set_yes_no_undef)
declare_hw_snprint(detect_prio, print_yes_no_undef)

declare_def_handler(detect_checker, set_yes_no_undef)
declare_def_snprint_defint(detect_checker, print_yes_no_undef, YNU_NO)
declare_ovr_handler(detect_checker, set_yes_no_undef)
declare_ovr_snprint(detect_checker, print_yes_no_undef)
declare_hw_handler(detect_checker, set_yes_no_undef)
declare_hw_snprint(detect_checker, print_yes_no_undef)

declare_def_handler(force_sync, set_yes_no)
declare_def_snprint(force_sync, print_yes_no)

declare_def_handler(deferred_remove, set_yes_no_undef)
declare_def_snprint_defint(deferred_remove, print_yes_no_undef, YNU_NO)
declare_ovr_handler(deferred_remove, set_yes_no_undef)
declare_ovr_snprint(deferred_remove, print_yes_no_undef)
declare_hw_handler(deferred_remove, set_yes_no_undef)
declare_hw_snprint(deferred_remove, print_yes_no_undef)
declare_mp_handler(deferred_remove, set_yes_no_undef)
declare_mp_snprint(deferred_remove, print_yes_no_undef)

declare_def_handler(retrigger_tries, set_int)
declare_def_snprint(retrigger_tries, print_int)

declare_def_handler(retrigger_delay, set_int)
declare_def_snprint(retrigger_delay, print_int)

declare_def_handler(uev_wait_timeout, set_int)
declare_def_snprint(uev_wait_timeout, print_int)

declare_def_handler(strict_timing, set_yes_no)
declare_def_snprint(strict_timing, print_yes_no)

declare_def_handler(skip_kpartx, set_yes_no_undef)
declare_def_snprint_defint(skip_kpartx, print_yes_no_undef, YNU_NO)
declare_ovr_handler(skip_kpartx, set_yes_no_undef)
declare_ovr_snprint(skip_kpartx, print_yes_no_undef)
declare_hw_handler(skip_kpartx, set_yes_no_undef)
declare_hw_snprint(skip_kpartx, print_yes_no_undef)
declare_mp_handler(skip_kpartx, set_yes_no_undef)
declare_mp_snprint(skip_kpartx, print_yes_no_undef)

declare_def_handler(disable_changed_wwids, set_yes_no)
declare_def_snprint(disable_changed_wwids, print_yes_no)

declare_def_handler(remove_retries, set_int)
declare_def_snprint(remove_retries, print_int)

declare_def_handler(max_sectors_kb, set_int)
declare_def_snprint(max_sectors_kb, print_nonzero)
declare_ovr_handler(max_sectors_kb, set_int)
declare_ovr_snprint(max_sectors_kb, print_nonzero)
declare_hw_handler(max_sectors_kb, set_int)
declare_hw_snprint(max_sectors_kb, print_nonzero)
declare_mp_handler(max_sectors_kb, set_int)
declare_mp_snprint(max_sectors_kb, print_nonzero)

static int
def_config_dir_handler(struct config *conf, vector strvec)
{
	/* this is only valid in the main config file */
	if (conf->processed_main_config)
		return 0;
	return set_str(strvec, &conf->config_dir);
}
declare_def_snprint(config_dir, print_str)

#define declare_def_attr_handler(option, function)			\
static int								\
def_ ## option ## _handler (struct config *conf, vector strvec)		\
{									\
	return function (strvec, &conf->option, &conf->attribute_flags);\
}

#define declare_def_attr_snprint(option, function)			\
static int								\
snprint_def_ ## option (struct config *conf, char * buff, int len, void * data) \
{									\
	return function (buff, len, &conf->option,			\
			 &conf->attribute_flags);			\
}

#define declare_mp_attr_handler(option, function)			\
static int								\
mp_ ## option ## _handler (struct config *conf, vector strvec)		\
{									\
	struct mpentry * mpe = VECTOR_LAST_SLOT(conf->mptable);		\
	if (!mpe)							\
		return 1;						\
	return function (strvec, &mpe->option, &mpe->attribute_flags);	\
}

#define declare_mp_attr_snprint(option, function)			\
static int								\
snprint_mp_ ## option (struct config *conf, char * buff, int len, void * data) \
{									\
	struct mpentry * mpe = (struct mpentry *)data;			\
	return function (buff, len, &mpe->option,			\
			 &mpe->attribute_flags);			\
}

static int
set_mode(vector strvec, void *ptr, int *flags)
{
	mode_t mode;
	mode_t *mode_ptr = (mode_t *)ptr;
	char *buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	if (sscanf(buff, "%o", &mode) == 1 && mode <= 0777) {
		*flags |= (1 << ATTR_MODE);
		*mode_ptr = mode;
	}

	FREE(buff);
	return 0;
}

static int
set_uid(vector strvec, void *ptr, int *flags)
{
	uid_t uid;
	uid_t *uid_ptr = (uid_t *)ptr;
	char *buff;
	char passwd_buf[1024];
	struct passwd info, *found;

	buff = set_value(strvec);
	if (!buff)
		return 1;
	if (getpwnam_r(buff, &info, passwd_buf, 1024, &found) == 0 && found) {
		*flags |= (1 << ATTR_UID);
		*uid_ptr = info.pw_uid;
	}
	else if (sscanf(buff, "%u", &uid) == 1){
		*flags |= (1 << ATTR_UID);
		*uid_ptr = uid;
	}

	FREE(buff);
	return 0;
}

static int
set_gid(vector strvec, void *ptr, int *flags)
{
	gid_t gid;
	gid_t *gid_ptr = (gid_t *)ptr;
	char *buff;
	char passwd_buf[1024];
	struct passwd info, *found;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if (getpwnam_r(buff, &info, passwd_buf, 1024, &found) == 0 && found) {
		*flags |= (1 << ATTR_GID);
		*gid_ptr = info.pw_gid;
	}
	else if (sscanf(buff, "%u", &gid) == 1){
		*flags |= (1 << ATTR_GID);
		*gid_ptr = gid;
	}
	FREE(buff);
	return 0;
}

static int
print_mode(char * buff, int len, void *ptr, int *flags)
{
	mode_t *mode_ptr = (mode_t *)ptr;
	if ((*flags & (1 << ATTR_MODE)) == 0)
		return 0;
	return snprintf(buff, len, "0%o", *mode_ptr);
}

static int
print_uid(char * buff, int len, void *ptr, int *flags)
{
	uid_t *uid_ptr = (uid_t *)ptr;
	if ((*flags & (1 << ATTR_UID)) == 0)
		return 0;
	return snprintf(buff, len, "0%o", *uid_ptr);
}

static int
print_gid(char * buff, int len, void *ptr, int *flags)
{
	gid_t *gid_ptr = (gid_t *)ptr;
	if ((*flags & (1 << ATTR_GID)) == 0)
		return 0;
	return snprintf(buff, len, "0%o", *gid_ptr);
}

declare_def_attr_handler(mode, set_mode)
declare_def_attr_snprint(mode, print_mode)
declare_mp_attr_handler(mode, set_mode)
declare_mp_attr_snprint(mode, print_mode)

declare_def_attr_handler(uid, set_uid)
declare_def_attr_snprint(uid, print_uid)
declare_mp_attr_handler(uid, set_uid)
declare_mp_attr_snprint(uid, print_uid)

declare_def_attr_handler(gid, set_gid)
declare_def_attr_snprint(gid, print_gid)
declare_mp_attr_handler(gid, set_gid)
declare_mp_attr_snprint(gid, print_gid)

static int
set_fast_io_fail(vector strvec, void *ptr)
{
	char * buff;
	int *int_ptr = (int *)ptr;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if (strcmp(buff, "off") == 0)
		*int_ptr = MP_FAST_IO_FAIL_OFF;
	else if (sscanf(buff, "%d", int_ptr) != 1 ||
		 *int_ptr < MP_FAST_IO_FAIL_ZERO)
		*int_ptr = MP_FAST_IO_FAIL_UNSET;
	else if (*int_ptr == 0)
		*int_ptr = MP_FAST_IO_FAIL_ZERO;

	FREE(buff);
	return 0;
}

int
print_fast_io_fail(char * buff, int len, void *ptr)
{
	int *int_ptr = (int *)ptr;

	if (*int_ptr == MP_FAST_IO_FAIL_UNSET)
		return 0;
	if (*int_ptr == MP_FAST_IO_FAIL_OFF)
		return snprintf(buff, len, "\"off\"");
	if (*int_ptr == MP_FAST_IO_FAIL_ZERO)
		return snprintf(buff, len, "0");
	return snprintf(buff, len, "%d", *int_ptr);
}

declare_def_handler(fast_io_fail, set_fast_io_fail)
declare_def_snprint(fast_io_fail, print_fast_io_fail)
declare_ovr_handler(fast_io_fail, set_fast_io_fail)
declare_ovr_snprint(fast_io_fail, print_fast_io_fail)
declare_hw_handler(fast_io_fail, set_fast_io_fail)
declare_hw_snprint(fast_io_fail, print_fast_io_fail)

static int
set_dev_loss(vector strvec, void *ptr)
{
	char * buff;
	unsigned int *uint_ptr = (unsigned int *)ptr;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if (!strcmp(buff, "infinity"))
		*uint_ptr = MAX_DEV_LOSS_TMO;
	else if (sscanf(buff, "%u", uint_ptr) != 1)
		*uint_ptr = 0;

	FREE(buff);
	return 0;
}

int
print_dev_loss(char * buff, int len, void *ptr)
{
	unsigned int *uint_ptr = (unsigned int *)ptr;

	if (!*uint_ptr)
		return 0;
	if (*uint_ptr >= MAX_DEV_LOSS_TMO)
		return snprintf(buff, len, "\"infinity\"");
	return snprintf(buff, len, "%u", *uint_ptr);
}

declare_def_handler(dev_loss, set_dev_loss)
declare_def_snprint(dev_loss, print_dev_loss)
declare_ovr_handler(dev_loss, set_dev_loss)
declare_ovr_snprint(dev_loss, print_dev_loss)
declare_hw_handler(dev_loss, set_dev_loss)
declare_hw_snprint(dev_loss, print_dev_loss)

static int
set_pgpolicy(vector strvec, void *ptr)
{
	char * buff;
	int *int_ptr = (int *)ptr;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	*int_ptr = get_pgpolicy_id(buff);
	FREE(buff);

	return 0;
}

int
print_pgpolicy(char * buff, int len, void *ptr)
{
	char str[POLICY_NAME_SIZE];
	int pgpolicy = *(int *)ptr;

	if (!pgpolicy)
		return 0;

	get_pgpolicy_name(str, POLICY_NAME_SIZE, pgpolicy);

	return snprintf(buff, len, "\"%s\"", str);
}

declare_def_handler(pgpolicy, set_pgpolicy)
declare_def_snprint_defint(pgpolicy, print_pgpolicy, DEFAULT_PGPOLICY)
declare_ovr_handler(pgpolicy, set_pgpolicy)
declare_ovr_snprint(pgpolicy, print_pgpolicy)
declare_hw_handler(pgpolicy, set_pgpolicy)
declare_hw_snprint(pgpolicy, print_pgpolicy)
declare_mp_handler(pgpolicy, set_pgpolicy)
declare_mp_snprint(pgpolicy, print_pgpolicy)

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
max_fds_handler(struct config *conf, vector strvec)
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
snprint_max_fds (struct config *conf, char * buff, int len, void * data)
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
set_rr_weight(vector strvec, void *ptr)
{
	int *int_ptr = (int *)ptr;
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	if (!strcmp(buff, "priorities"))
		*int_ptr = RR_WEIGHT_PRIO;

	if (!strcmp(buff, "uniform"))
		*int_ptr = RR_WEIGHT_NONE;

	FREE(buff);

	return 0;
}

int
print_rr_weight (char * buff, int len, void *ptr)
{
	int *int_ptr = (int *)ptr;

	if (!*int_ptr)
		return 0;
	if (*int_ptr == RR_WEIGHT_PRIO)
		return snprintf(buff, len, "\"priorities\"");
	if (*int_ptr == RR_WEIGHT_NONE)
		return snprintf(buff, len, "\"uniform\"");

	return 0;
}

declare_def_handler(rr_weight, set_rr_weight)
declare_def_snprint_defint(rr_weight, print_rr_weight, RR_WEIGHT_NONE)
declare_ovr_handler(rr_weight, set_rr_weight)
declare_ovr_snprint(rr_weight, print_rr_weight)
declare_hw_handler(rr_weight, set_rr_weight)
declare_hw_snprint(rr_weight, print_rr_weight)
declare_mp_handler(rr_weight, set_rr_weight)
declare_mp_snprint(rr_weight, print_rr_weight)

static int
set_pgfailback(vector strvec, void *ptr)
{
	int *int_ptr = (int *)ptr;
	char * buff;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if (strlen(buff) == 6 && !strcmp(buff, "manual"))
		*int_ptr = -FAILBACK_MANUAL;
	else if (strlen(buff) == 9 && !strcmp(buff, "immediate"))
		*int_ptr = -FAILBACK_IMMEDIATE;
	else if (strlen(buff) == 10 && !strcmp(buff, "followover"))
		*int_ptr = -FAILBACK_FOLLOWOVER;
	else
		*int_ptr = atoi(buff);

	FREE(buff);

	return 0;
}

int
print_pgfailback (char * buff, int len, void *ptr)
{
	int *int_ptr = (int *)ptr;

	switch(*int_ptr) {
	case  FAILBACK_UNDEF:
		return 0;
	case -FAILBACK_MANUAL:
		return snprintf(buff, len, "\"manual\"");
	case -FAILBACK_IMMEDIATE:
		return snprintf(buff, len, "\"immediate\"");
	case -FAILBACK_FOLLOWOVER:
		return snprintf(buff, len, "\"followover\"");
	default:
		return snprintf(buff, len, "%i", *int_ptr);
	}
}

declare_def_handler(pgfailback, set_pgfailback)
declare_def_snprint_defint(pgfailback, print_pgfailback, DEFAULT_FAILBACK)
declare_ovr_handler(pgfailback, set_pgfailback)
declare_ovr_snprint(pgfailback, print_pgfailback)
declare_hw_handler(pgfailback, set_pgfailback)
declare_hw_snprint(pgfailback, print_pgfailback)
declare_mp_handler(pgfailback, set_pgfailback)
declare_mp_snprint(pgfailback, print_pgfailback)

static int
set_no_path_retry(vector strvec, void *ptr)
{
	int *int_ptr = (int *)ptr;
	char * buff;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if (!strcmp(buff, "fail") || !strcmp(buff, "0"))
		*int_ptr = NO_PATH_RETRY_FAIL;
	else if (!strcmp(buff, "queue"))
		*int_ptr = NO_PATH_RETRY_QUEUE;
	else if ((*int_ptr = atoi(buff)) < 1)
		*int_ptr = NO_PATH_RETRY_UNDEF;

	FREE(buff);
	return 0;
}

int
print_no_path_retry(char * buff, int len, void *ptr)
{
	int *int_ptr = (int *)ptr;

	switch(*int_ptr) {
	case NO_PATH_RETRY_UNDEF:
		return 0;
	case NO_PATH_RETRY_FAIL:
		return snprintf(buff, len, "\"fail\"");
	case NO_PATH_RETRY_QUEUE:
		return snprintf(buff, len, "\"queue\"");
	default:
		return snprintf(buff, len, "%i", *int_ptr);
	}
}

declare_def_handler(no_path_retry, set_no_path_retry)
declare_def_snprint(no_path_retry, print_no_path_retry)
declare_ovr_handler(no_path_retry, set_no_path_retry)
declare_ovr_snprint(no_path_retry, print_no_path_retry)
declare_hw_handler(no_path_retry, set_no_path_retry)
declare_hw_snprint(no_path_retry, print_no_path_retry)
declare_mp_handler(no_path_retry, set_no_path_retry)
declare_mp_snprint(no_path_retry, print_no_path_retry)

static int
def_log_checker_err_handler(struct config *conf, vector strvec)
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
snprint_def_log_checker_err (struct config *conf, char * buff, int len, void * data)
{
	if (conf->log_checker_err == LOG_CHKR_ERR_ONCE)
		return snprintf(buff, len, "once");
	return snprintf(buff, len, "always");
}

static int
set_reservation_key(vector strvec, void *ptr)
{
	unsigned char **uchar_ptr = (unsigned char **)ptr;
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

	if (!*uchar_ptr)
		*uchar_ptr = (unsigned char *) malloc(8);

	memset(*uchar_ptr, 0, 8);

	for (j = 7; j >= 0; --j) {
		(*uchar_ptr)[j] = (prkey & 0xff);
		prkey >>= 8;
	}

	FREE(tbuff);
	return 0;
}

int
print_reservation_key(char * buff, int len, void * ptr)
{
	unsigned char **uchar_ptr = (unsigned char **)ptr;
	int i;
	unsigned char *keyp;
	uint64_t prkey = 0;

	if (!*uchar_ptr)
		return 0;
	keyp = (unsigned char *)(*uchar_ptr);
	for (i = 0; i < 8; i++) {
		if (i > 0)
			prkey <<= 8;
		prkey |= *keyp;
		keyp++;
	}
	return snprintf(buff, len, "0x%" PRIx64, prkey);
}

declare_def_handler(reservation_key, set_reservation_key)
declare_def_snprint(reservation_key, print_reservation_key)
declare_mp_handler(reservation_key, set_reservation_key)
declare_mp_snprint(reservation_key, print_reservation_key)

static int
set_off_int_undef(vector strvec, void *ptr)
{
	int *int_ptr = (int *)ptr;
	char * buff;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if (!strcmp(buff, "no") || !strcmp(buff, "0"))
		*int_ptr = NU_NO;
	else if ((*int_ptr = atoi(buff)) < 1)
		*int_ptr = NU_UNDEF;

	FREE(buff);
	return 0;
}

int
print_off_int_undef(char * buff, int len, void *ptr)
{
	int *int_ptr = (int *)ptr;

	switch(*int_ptr) {
	case NU_UNDEF:
		return 0;
	case NU_NO:
		return snprintf(buff, len, "\"no\"");
	default:
		return snprintf(buff, len, "%i", *int_ptr);
	}
}

declare_def_handler(delay_watch_checks, set_off_int_undef)
declare_def_snprint(delay_watch_checks, print_off_int_undef)
declare_ovr_handler(delay_watch_checks, set_off_int_undef)
declare_ovr_snprint(delay_watch_checks, print_off_int_undef)
declare_hw_handler(delay_watch_checks, set_off_int_undef)
declare_hw_snprint(delay_watch_checks, print_off_int_undef)
declare_mp_handler(delay_watch_checks, set_off_int_undef)
declare_mp_snprint(delay_watch_checks, print_off_int_undef)
declare_def_handler(delay_wait_checks, set_off_int_undef)
declare_def_snprint(delay_wait_checks, print_off_int_undef)
declare_ovr_handler(delay_wait_checks, set_off_int_undef)
declare_ovr_snprint(delay_wait_checks, print_off_int_undef)
declare_hw_handler(delay_wait_checks, set_off_int_undef)
declare_hw_snprint(delay_wait_checks, print_off_int_undef)
declare_mp_handler(delay_wait_checks, set_off_int_undef)
declare_mp_snprint(delay_wait_checks, print_off_int_undef)
declare_def_handler(san_path_err_threshold, set_off_int_undef)
declare_def_snprint(san_path_err_threshold, print_off_int_undef)
declare_ovr_handler(san_path_err_threshold, set_off_int_undef)
declare_ovr_snprint(san_path_err_threshold, print_off_int_undef)
declare_hw_handler(san_path_err_threshold, set_off_int_undef)
declare_hw_snprint(san_path_err_threshold, print_off_int_undef)
declare_mp_handler(san_path_err_threshold, set_off_int_undef)
declare_mp_snprint(san_path_err_threshold, print_off_int_undef)
declare_def_handler(san_path_err_forget_rate, set_off_int_undef)
declare_def_snprint(san_path_err_forget_rate, print_off_int_undef)
declare_ovr_handler(san_path_err_forget_rate, set_off_int_undef)
declare_ovr_snprint(san_path_err_forget_rate, print_off_int_undef)
declare_hw_handler(san_path_err_forget_rate, set_off_int_undef)
declare_hw_snprint(san_path_err_forget_rate, print_off_int_undef)
declare_mp_handler(san_path_err_forget_rate, set_off_int_undef)
declare_mp_snprint(san_path_err_forget_rate, print_off_int_undef)
declare_def_handler(san_path_err_recovery_time, set_off_int_undef)
declare_def_snprint(san_path_err_recovery_time, print_off_int_undef)
declare_ovr_handler(san_path_err_recovery_time, set_off_int_undef)
declare_ovr_snprint(san_path_err_recovery_time, print_off_int_undef)
declare_hw_handler(san_path_err_recovery_time, set_off_int_undef)
declare_hw_snprint(san_path_err_recovery_time, print_off_int_undef)
declare_mp_handler(san_path_err_recovery_time, set_off_int_undef)
declare_mp_snprint(san_path_err_recovery_time, print_off_int_undef)
static int
def_uxsock_timeout_handler(struct config *conf, vector strvec)
{
	unsigned int uxsock_timeout;
	char *buff;

	buff = set_value(strvec);
	if (!buff)
		return 1;

	if (sscanf(buff, "%u", &uxsock_timeout) == 1 &&
	    uxsock_timeout > DEFAULT_REPLY_TIMEOUT)
		conf->uxsock_timeout = uxsock_timeout;
	else
		conf->uxsock_timeout = DEFAULT_REPLY_TIMEOUT;

	free(buff);
	return 0;
}

/*
 * blacklist block handlers
 */
static int
blacklist_handler(struct config *conf, vector strvec)
{
	if (!conf->blist_devnode)
		conf->blist_devnode = vector_alloc();
	if (!conf->blist_wwid)
		conf->blist_wwid = vector_alloc();
	if (!conf->blist_device)
		conf->blist_device = vector_alloc();
	if (!conf->blist_property)
		conf->blist_property = vector_alloc();

	if (!conf->blist_devnode || !conf->blist_wwid ||
	    !conf->blist_device || !conf->blist_property)
		return 1;

	return 0;
}

static int
blacklist_exceptions_handler(struct config *conf, vector strvec)
{
	if (!conf->elist_devnode)
		conf->elist_devnode = vector_alloc();
	if (!conf->elist_wwid)
		conf->elist_wwid = vector_alloc();
	if (!conf->elist_device)
		conf->elist_device = vector_alloc();
	if (!conf->elist_property)
		conf->elist_property = vector_alloc();

	if (!conf->elist_devnode || !conf->elist_wwid ||
	    !conf->elist_device || !conf->elist_property)
		return 1;

	return 0;
}

#define declare_ble_handler(option)					\
static int								\
ble_ ## option ## _handler (struct config *conf, vector strvec)		\
{									\
	char * buff;							\
									\
	if (!conf->option)						\
		return 1;						\
									\
	buff = set_value(strvec);					\
	if (!buff)							\
		return 1;						\
									\
	return store_ble(conf->option, buff, ORIGIN_CONFIG);		\
}

#define declare_ble_device_handler(name, option, vend, prod)		\
static int								\
ble_ ## option ## _ ## name ## _handler (struct config *conf, vector strvec) \
{									\
	char * buff;							\
									\
	if (!conf->option)						\
		return 1;						\
									\
	buff = set_value(strvec);					\
	if (!buff)							\
		return 1;						\
									\
	return set_ble_device(conf->option, vend, prod, ORIGIN_CONFIG);	\
}

declare_ble_handler(blist_devnode)
declare_ble_handler(elist_devnode)
declare_ble_handler(blist_wwid)
declare_ble_handler(elist_wwid)
declare_ble_handler(blist_property)
declare_ble_handler(elist_property)

static int
snprint_def_uxsock_timeout(struct config *conf, char * buff, int len, void * data)
{
	return snprintf(buff, len, "%u", conf->uxsock_timeout);
}

static int
snprint_ble_simple (struct config *conf, char * buff, int len, void * data)
{
	struct blentry * ble = (struct blentry *)data;

	return snprintf(buff, len, "\"%s\"", ble->str);
}

static int
ble_device_handler(struct config *conf, vector strvec)
{
	return alloc_ble_device(conf->blist_device);
}

static int
ble_except_device_handler(struct config *conf, vector strvec)
{
	return alloc_ble_device(conf->elist_device);
}

declare_ble_device_handler(vendor, blist_device, buff, NULL)
declare_ble_device_handler(vendor, elist_device, buff, NULL)
declare_ble_device_handler(product, blist_device, NULL, buff)
declare_ble_device_handler(product, elist_device, NULL, buff)

static int
snprint_bled_vendor (struct config *conf, char * buff, int len, void * data)
{
	struct blentry_device * bled = (struct blentry_device *)data;

	return snprintf(buff, len, "\"%s\"", bled->vendor);
}

static int
snprint_bled_product (struct config *conf, char * buff, int len, void * data)
{
	struct blentry_device * bled = (struct blentry_device *)data;

	return snprintf(buff, len, "\"%s\"", bled->product);
}

/*
 * devices block handlers
 */
static int
devices_handler(struct config *conf, vector strvec)
{
	if (!conf->hwtable)
		conf->hwtable = vector_alloc();

	if (!conf->hwtable)
		return 1;

	return 0;
}

static int
device_handler(struct config *conf, vector strvec)
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

declare_hw_handler(vendor, set_str)
declare_hw_snprint(vendor, print_str)

declare_hw_handler(product, set_str)
declare_hw_snprint(product, print_str)

declare_hw_handler(revision, set_str)
declare_hw_snprint(revision, print_str)

declare_hw_handler(bl_product, set_str)
declare_hw_snprint(bl_product, print_str)

declare_hw_handler(hwhandler, set_str)
declare_hw_snprint(hwhandler, print_str)

/*
 * overrides handlers
 */
static int
overrides_handler(struct config *conf, vector strvec)
{
	if (!conf->overrides)
		conf->overrides = alloc_hwe();

	if (!conf->overrides)
		return 1;

	return 0;
}



/*
 * multipaths block handlers
 */
static int
multipaths_handler(struct config *conf, vector strvec)
{
	if (!conf->mptable)
		conf->mptable = vector_alloc();

	if (!conf->mptable)
		return 1;

	return 0;
}

static int
multipath_handler(struct config *conf, vector strvec)
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

declare_mp_handler(wwid, set_str)
declare_mp_snprint(wwid, print_str)

declare_mp_handler(alias, set_str)
declare_mp_snprint(alias, print_str)

/*
 * deprecated handlers
 */

static int
deprecated_handler(struct config *conf, vector strvec)
{
	char * buff;

	buff = set_value(strvec);

	if (!buff)
		return 1;

	FREE(buff);
	return 0;
}

static int
snprint_deprecated (struct config *conf, char * buff, int len, void * data)
{
	return 0;
}

#define __deprecated

/*
 * If you add or remove a keyword also update multipath/multipath.conf.5
 */
void
init_keywords(vector keywords)
{
	install_keyword_root("defaults", NULL);
	install_keyword("verbosity", &def_verbosity_handler, &snprint_def_verbosity);
	install_keyword("polling_interval", &def_checkint_handler, &snprint_def_checkint);
	install_keyword("max_polling_interval", &def_max_checkint_handler, &snprint_def_max_checkint);
	install_keyword("reassign_maps", &def_reassign_maps_handler, &snprint_def_reassign_maps);
	install_keyword("multipath_dir", &def_multipath_dir_handler, &snprint_def_multipath_dir);
	install_keyword("path_selector", &def_selector_handler, &snprint_def_selector);
	install_keyword("path_grouping_policy", &def_pgpolicy_handler, &snprint_def_pgpolicy);
	install_keyword("uid_attrs", &def_uid_attrs_handler, &snprint_def_uid_attrs);
	install_keyword("uid_attribute", &def_uid_attribute_handler, &snprint_def_uid_attribute);
	install_keyword("getuid_callout", &def_getuid_handler, &snprint_def_getuid);
	install_keyword("prio", &def_prio_name_handler, &snprint_def_prio_name);
	install_keyword("prio_args", &def_prio_args_handler, &snprint_def_prio_args);
	install_keyword("features", &def_features_handler, &snprint_def_features);
	install_keyword("path_checker", &def_checker_name_handler, &snprint_def_checker_name);
	install_keyword("checker", &def_checker_name_handler, NULL);
	install_keyword("alias_prefix", &def_alias_prefix_handler, &snprint_def_alias_prefix);
	install_keyword("failback", &def_pgfailback_handler, &snprint_def_pgfailback);
	install_keyword("rr_min_io", &def_minio_handler, &snprint_def_minio);
	install_keyword("rr_min_io_rq", &def_minio_rq_handler, &snprint_def_minio_rq);
	install_keyword("max_fds", &max_fds_handler, &snprint_max_fds);
	install_keyword("rr_weight", &def_rr_weight_handler, &snprint_def_rr_weight);
	install_keyword("no_path_retry", &def_no_path_retry_handler, &snprint_def_no_path_retry);
	install_keyword("queue_without_daemon", &def_queue_without_daemon_handler, &snprint_def_queue_without_daemon);
	install_keyword("checker_timeout", &def_checker_timeout_handler, &snprint_def_checker_timeout);
	install_keyword("pg_timeout", &deprecated_handler, &snprint_deprecated);
	install_keyword("flush_on_last_del", &def_flush_on_last_del_handler, &snprint_def_flush_on_last_del);
	install_keyword("user_friendly_names", &def_user_friendly_names_handler, &snprint_def_user_friendly_names);
	install_keyword("mode", &def_mode_handler, &snprint_def_mode);
	install_keyword("uid", &def_uid_handler, &snprint_def_uid);
	install_keyword("gid", &def_gid_handler, &snprint_def_gid);
	install_keyword("fast_io_fail_tmo", &def_fast_io_fail_handler, &snprint_def_fast_io_fail);
	install_keyword("dev_loss_tmo", &def_dev_loss_handler, &snprint_def_dev_loss);
	install_keyword("bindings_file", &def_bindings_file_handler, &snprint_def_bindings_file);
	install_keyword("wwids_file", &def_wwids_file_handler, &snprint_def_wwids_file);
	install_keyword("log_checker_err", &def_log_checker_err_handler, &snprint_def_log_checker_err);
	install_keyword("reservation_key", &def_reservation_key_handler, &snprint_def_reservation_key);
	install_keyword("retain_attached_hw_handler", &def_retain_hwhandler_handler, &snprint_def_retain_hwhandler);
	install_keyword("detect_prio", &def_detect_prio_handler, &snprint_def_detect_prio);
	install_keyword("detect_checker", &def_detect_checker_handler, &snprint_def_detect_checker);
	install_keyword("force_sync", &def_force_sync_handler, &snprint_def_force_sync);
	install_keyword("strict_timing", &def_strict_timing_handler, &snprint_def_strict_timing);
	install_keyword("deferred_remove", &def_deferred_remove_handler, &snprint_def_deferred_remove);
	install_keyword("partition_delimiter", &def_partition_delim_handler, &snprint_def_partition_delim);
	install_keyword("config_dir", &def_config_dir_handler, &snprint_def_config_dir);
	install_keyword("delay_watch_checks", &def_delay_watch_checks_handler, &snprint_def_delay_watch_checks);
	install_keyword("delay_wait_checks", &def_delay_wait_checks_handler, &snprint_def_delay_wait_checks);
	install_keyword("san_path_err_threshold", &def_san_path_err_threshold_handler, &snprint_def_san_path_err_threshold);
	install_keyword("san_path_err_forget_rate", &def_san_path_err_forget_rate_handler, &snprint_def_san_path_err_forget_rate);
	install_keyword("san_path_err_recovery_time", &def_san_path_err_recovery_time_handler, &snprint_def_san_path_err_recovery_time);

	install_keyword("find_multipaths", &def_find_multipaths_handler, &snprint_def_find_multipaths);
	install_keyword("uxsock_timeout", &def_uxsock_timeout_handler, &snprint_def_uxsock_timeout);
	install_keyword("retrigger_tries", &def_retrigger_tries_handler, &snprint_def_retrigger_tries);
	install_keyword("retrigger_delay", &def_retrigger_delay_handler, &snprint_def_retrigger_delay);
	install_keyword("missing_uev_wait_timeout", &def_uev_wait_timeout_handler, &snprint_def_uev_wait_timeout);
	install_keyword("skip_kpartx", &def_skip_kpartx_handler, &snprint_def_skip_kpartx);
	install_keyword("disable_changed_wwids", &def_disable_changed_wwids_handler, &snprint_def_disable_changed_wwids);
	install_keyword("remove_retries", &def_remove_retries_handler, &snprint_def_remove_retries);
	install_keyword("max_sectors_kb", &def_max_sectors_kb_handler, &snprint_def_max_sectors_kb);
	__deprecated install_keyword("default_selector", &def_selector_handler, NULL);
	__deprecated install_keyword("default_path_grouping_policy", &def_pgpolicy_handler, NULL);
	__deprecated install_keyword("default_uid_attribute", &def_uid_attribute_handler, NULL);
	__deprecated install_keyword("default_getuid_callout", &def_getuid_handler, NULL);
	__deprecated install_keyword("default_features", &def_features_handler, NULL);
	__deprecated install_keyword("default_path_checker", &def_checker_name_handler, NULL);

	install_keyword_root("blacklist", &blacklist_handler);
	install_keyword_multi("devnode", &ble_blist_devnode_handler, &snprint_ble_simple);
	install_keyword_multi("wwid", &ble_blist_wwid_handler, &snprint_ble_simple);
	install_keyword_multi("property", &ble_blist_property_handler, &snprint_ble_simple);
	install_keyword_multi("device", &ble_device_handler, NULL);
	install_sublevel();
	install_keyword("vendor", &ble_blist_device_vendor_handler, &snprint_bled_vendor);
	install_keyword("product", &ble_blist_device_product_handler, &snprint_bled_product);
	install_sublevel_end();
	install_keyword_root("blacklist_exceptions", &blacklist_exceptions_handler);
	install_keyword_multi("devnode", &ble_elist_devnode_handler, &snprint_ble_simple);
	install_keyword_multi("wwid", &ble_elist_wwid_handler, &snprint_ble_simple);
	install_keyword_multi("property", &ble_elist_property_handler, &snprint_ble_simple);
	install_keyword_multi("device", &ble_except_device_handler, NULL);
	install_sublevel();
	install_keyword("vendor", &ble_elist_device_vendor_handler, &snprint_bled_vendor);
	install_keyword("product", &ble_elist_device_product_handler, &snprint_bled_product);
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
/*
 * If you add or remove a "device subsection" keyword also update
 * multipath/multipath.conf.5 and the TEMPLATE in libmultipath/hwtable.c
 */
	install_keyword_root("devices", &devices_handler);
	install_keyword_multi("device", &device_handler, NULL);
	install_sublevel();
	install_keyword("vendor", &hw_vendor_handler, &snprint_hw_vendor);
	install_keyword("product", &hw_product_handler, &snprint_hw_product);
	install_keyword("revision", &hw_revision_handler, &snprint_hw_revision);
	install_keyword("product_blacklist", &hw_bl_product_handler, &snprint_hw_bl_product);
	install_keyword("path_grouping_policy", &hw_pgpolicy_handler, &snprint_hw_pgpolicy);
	install_keyword("uid_attribute", &hw_uid_attribute_handler, &snprint_hw_uid_attribute);
	install_keyword("getuid_callout", &hw_getuid_handler, &snprint_hw_getuid);
	install_keyword("path_selector", &hw_selector_handler, &snprint_hw_selector);
	install_keyword("path_checker", &hw_checker_name_handler, &snprint_hw_checker_name);
	install_keyword("checker", &hw_checker_name_handler, NULL);
	install_keyword("alias_prefix", &hw_alias_prefix_handler, &snprint_hw_alias_prefix);
	install_keyword("features", &hw_features_handler, &snprint_hw_features);
	install_keyword("hardware_handler", &hw_hwhandler_handler, &snprint_hw_hwhandler);
	install_keyword("prio", &hw_prio_name_handler, &snprint_hw_prio_name);
	install_keyword("prio_args", &hw_prio_args_handler, &snprint_hw_prio_args);
	install_keyword("failback", &hw_pgfailback_handler, &snprint_hw_pgfailback);
	install_keyword("rr_weight", &hw_rr_weight_handler, &snprint_hw_rr_weight);
	install_keyword("no_path_retry", &hw_no_path_retry_handler, &snprint_hw_no_path_retry);
	install_keyword("rr_min_io", &hw_minio_handler, &snprint_hw_minio);
	install_keyword("rr_min_io_rq", &hw_minio_rq_handler, &snprint_hw_minio_rq);
	install_keyword("pg_timeout", &deprecated_handler, &snprint_deprecated);
	install_keyword("flush_on_last_del", &hw_flush_on_last_del_handler, &snprint_hw_flush_on_last_del);
	install_keyword("fast_io_fail_tmo", &hw_fast_io_fail_handler, &snprint_hw_fast_io_fail);
	install_keyword("dev_loss_tmo", &hw_dev_loss_handler, &snprint_hw_dev_loss);
	install_keyword("user_friendly_names", &hw_user_friendly_names_handler, &snprint_hw_user_friendly_names);
	install_keyword("retain_attached_hw_handler", &hw_retain_hwhandler_handler, &snprint_hw_retain_hwhandler);
	install_keyword("detect_prio", &hw_detect_prio_handler, &snprint_hw_detect_prio);
	install_keyword("detect_checker", &hw_detect_checker_handler, &snprint_hw_detect_checker);
	install_keyword("deferred_remove", &hw_deferred_remove_handler, &snprint_hw_deferred_remove);
	install_keyword("delay_watch_checks", &hw_delay_watch_checks_handler, &snprint_hw_delay_watch_checks);
	install_keyword("delay_wait_checks", &hw_delay_wait_checks_handler, &snprint_hw_delay_wait_checks);
	install_keyword("san_path_err_threshold", &hw_san_path_err_threshold_handler, &snprint_hw_san_path_err_threshold);
	install_keyword("san_path_err_forget_rate", &hw_san_path_err_forget_rate_handler, &snprint_hw_san_path_err_forget_rate);
	install_keyword("san_path_err_recovery_time", &hw_san_path_err_recovery_time_handler, &snprint_hw_san_path_err_recovery_time);
	install_keyword("skip_kpartx", &hw_skip_kpartx_handler, &snprint_hw_skip_kpartx);
	install_keyword("max_sectors_kb", &hw_max_sectors_kb_handler, &snprint_hw_max_sectors_kb);
	install_sublevel_end();

	install_keyword_root("overrides", &overrides_handler);
	install_keyword("path_grouping_policy", &ovr_pgpolicy_handler, &snprint_ovr_pgpolicy);
	install_keyword("uid_attribute", &ovr_uid_attribute_handler, &snprint_ovr_uid_attribute);
	install_keyword("getuid_callout", &ovr_getuid_handler, &snprint_ovr_getuid);
	install_keyword("path_selector", &ovr_selector_handler, &snprint_ovr_selector);
	install_keyword("path_checker", &ovr_checker_name_handler, &snprint_ovr_checker_name);
	install_keyword("checker", &ovr_checker_name_handler, NULL);
	install_keyword("alias_prefix", &ovr_alias_prefix_handler, &snprint_ovr_alias_prefix);
	install_keyword("features", &ovr_features_handler, &snprint_ovr_features);
	install_keyword("prio", &ovr_prio_name_handler, &snprint_ovr_prio_name);
	install_keyword("prio_args", &ovr_prio_args_handler, &snprint_ovr_prio_args);
	install_keyword("failback", &ovr_pgfailback_handler, &snprint_ovr_pgfailback);
	install_keyword("rr_weight", &ovr_rr_weight_handler, &snprint_ovr_rr_weight);
	install_keyword("no_path_retry", &ovr_no_path_retry_handler, &snprint_ovr_no_path_retry);
	install_keyword("rr_min_io", &ovr_minio_handler, &snprint_ovr_minio);
	install_keyword("rr_min_io_rq", &ovr_minio_rq_handler, &snprint_ovr_minio_rq);
	install_keyword("flush_on_last_del", &ovr_flush_on_last_del_handler, &snprint_ovr_flush_on_last_del);
	install_keyword("fast_io_fail_tmo", &ovr_fast_io_fail_handler, &snprint_ovr_fast_io_fail);
	install_keyword("dev_loss_tmo", &ovr_dev_loss_handler, &snprint_ovr_dev_loss);
	install_keyword("user_friendly_names", &ovr_user_friendly_names_handler, &snprint_ovr_user_friendly_names);
	install_keyword("retain_attached_hw_handler", &ovr_retain_hwhandler_handler, &snprint_ovr_retain_hwhandler);
	install_keyword("detect_prio", &ovr_detect_prio_handler, &snprint_ovr_detect_prio);
	install_keyword("detect_checker", &ovr_detect_checker_handler, &snprint_ovr_detect_checker);
	install_keyword("deferred_remove", &ovr_deferred_remove_handler, &snprint_ovr_deferred_remove);
	install_keyword("delay_watch_checks", &ovr_delay_watch_checks_handler, &snprint_ovr_delay_watch_checks);
	install_keyword("delay_wait_checks", &ovr_delay_wait_checks_handler, &snprint_ovr_delay_wait_checks);
	install_keyword("san_path_err_threshold", &ovr_san_path_err_threshold_handler, &snprint_ovr_san_path_err_threshold);
	install_keyword("san_path_err_forget_rate", &ovr_san_path_err_forget_rate_handler, &snprint_ovr_san_path_err_forget_rate);
	install_keyword("san_path_err_recovery_time", &ovr_san_path_err_recovery_time_handler, &snprint_ovr_san_path_err_recovery_time);

	install_keyword("skip_kpartx", &ovr_skip_kpartx_handler, &snprint_ovr_skip_kpartx);
	install_keyword("max_sectors_kb", &ovr_max_sectors_kb_handler, &snprint_ovr_max_sectors_kb);

	install_keyword_root("multipaths", &multipaths_handler);
	install_keyword_multi("multipath", &multipath_handler, NULL);
	install_sublevel();
	install_keyword("wwid", &mp_wwid_handler, &snprint_mp_wwid);
	install_keyword("alias", &mp_alias_handler, &snprint_mp_alias);
	install_keyword("path_grouping_policy", &mp_pgpolicy_handler, &snprint_mp_pgpolicy);
	install_keyword("path_selector", &mp_selector_handler, &snprint_mp_selector);
	install_keyword("prio", &mp_prio_name_handler, &snprint_mp_prio_name);
	install_keyword("prio_args", &mp_prio_args_handler, &snprint_mp_prio_args);
	install_keyword("failback", &mp_pgfailback_handler, &snprint_mp_pgfailback);
	install_keyword("rr_weight", &mp_rr_weight_handler, &snprint_mp_rr_weight);
	install_keyword("no_path_retry", &mp_no_path_retry_handler, &snprint_mp_no_path_retry);
	install_keyword("rr_min_io", &mp_minio_handler, &snprint_mp_minio);
	install_keyword("rr_min_io_rq", &mp_minio_rq_handler, &snprint_mp_minio_rq);
	install_keyword("pg_timeout", &deprecated_handler, &snprint_deprecated);
	install_keyword("flush_on_last_del", &mp_flush_on_last_del_handler, &snprint_mp_flush_on_last_del);
	install_keyword("features", &mp_features_handler, &snprint_mp_features);
	install_keyword("mode", &mp_mode_handler, &snprint_mp_mode);
	install_keyword("uid", &mp_uid_handler, &snprint_mp_uid);
	install_keyword("gid", &mp_gid_handler, &snprint_mp_gid);
	install_keyword("reservation_key", &mp_reservation_key_handler, &snprint_mp_reservation_key);
	install_keyword("user_friendly_names", &mp_user_friendly_names_handler, &snprint_mp_user_friendly_names);
	install_keyword("deferred_remove", &mp_deferred_remove_handler, &snprint_mp_deferred_remove);
	install_keyword("delay_watch_checks", &mp_delay_watch_checks_handler, &snprint_mp_delay_watch_checks);
	install_keyword("delay_wait_checks", &mp_delay_wait_checks_handler, &snprint_mp_delay_wait_checks);
	install_keyword("san_path_err_threshold", &mp_san_path_err_threshold_handler, &snprint_mp_san_path_err_threshold);
	install_keyword("san_path_err_forget_rate", &mp_san_path_err_forget_rate_handler, &snprint_mp_san_path_err_forget_rate);
	install_keyword("san_path_err_recovery_time", &mp_san_path_err_recovery_time_handler, &snprint_mp_san_path_err_recovery_time);
	install_keyword("skip_kpartx", &mp_skip_kpartx_handler, &snprint_mp_skip_kpartx);
	install_keyword("max_sectors_kb", &mp_max_sectors_kb_handler, &snprint_mp_max_sectors_kb);
	install_sublevel_end();
}
