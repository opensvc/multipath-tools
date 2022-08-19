/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 * Copyright (c) 2005 Benjamin Marzinski, Redhat
 * Copyright (c) 2005 Edward Goggin, EMC
 */
#include <stdio.h>
#include <string.h>
#include <libudev.h>
#include <dirent.h>
#include <limits.h>
#include <errno.h>

#include "checkers.h"
#include "util.h"
#include "debug.h"
#include "parser.h"
#include "dict.h"
#include "hwtable.h"
#include "vector.h"
#include "structs.h"
#include "config.h"
#include "blacklist.h"
#include "defaults.h"
#include "prio.h"
#include "devmapper.h"
#include "mpath_cmd.h"
#include "propsel.h"
#include "foreign.h"

/*
 * We don't support re-initialization after
 * libmultipath_exit().
 */
static bool libmultipath_exit_called;
static pthread_once_t _init_once = PTHREAD_ONCE_INIT;
static pthread_once_t _exit_once = PTHREAD_ONCE_INIT;
struct udev *udev;

static void _udev_init(void)
{
	if (udev)
		udev_ref(udev);
	else
		udev = udev_new();
	if (!udev)
		condlog(0, "%s: failed to initialize udev", __func__);
}

static bool _is_libmultipath_initialized(void)
{
	return !libmultipath_exit_called && !!udev;
}

int libmultipath_init(void)
{
	pthread_once(&_init_once, _udev_init);
	return !_is_libmultipath_initialized();
}

static void _libmultipath_exit(void)
{
	libmultipath_exit_called = true;
	cleanup_foreign();
	cleanup_checkers();
	cleanup_prio();
	libmp_dm_exit();
	udev_unref(udev);
}

void libmultipath_exit(void)
{
	pthread_once(&_exit_once, _libmultipath_exit);
}

static struct config __internal_config;
struct config *libmp_get_multipath_config(void)
{
	if (!__internal_config.hwtable)
		/* not initialized */
		return NULL;
	return &__internal_config;
}

struct config *get_multipath_config(void)
	__attribute__((alias("libmp_get_multipath_config")));

void libmp_put_multipath_config(void *conf __attribute__((unused)))
{
	/* empty */
}

void put_multipath_config(void *conf)
	__attribute__((alias("libmp_put_multipath_config")));

static int
hwe_strmatch (const struct hwentry *hwe1, const struct hwentry *hwe2)
{
	if ((hwe2->vendor && !hwe1->vendor) ||
	    (hwe1->vendor && (!hwe2->vendor ||
			      strcmp(hwe1->vendor, hwe2->vendor))))
		return 1;

	if ((hwe2->product && !hwe1->product) ||
	    (hwe1->product && (!hwe2->product ||
			      strcmp(hwe1->product, hwe2->product))))
		return 1;

	if ((hwe2->revision && !hwe1->revision) ||
	    (hwe1->revision && (!hwe2->revision ||
			      strcmp(hwe1->revision, hwe2->revision))))
		return 1;

	return 0;
}

static struct hwentry *
find_hwe_strmatch (const struct _vector *hwtable, const struct hwentry *hwe)
{
	int i;
	struct hwentry *tmp, *ret = NULL;

	vector_foreach_slot (hwtable, tmp, i) {
		if (hwe_strmatch(tmp, hwe))
			continue;
		ret = tmp;
		break;
	}
	return ret;
}

static int
hwe_regmatch (const struct hwentry *hwe1, const char *vendor,
	      const char *product, const char *revision)
{
	regex_t vre, pre, rre;
	int retval = 1;

	if (hwe1->vendor &&
	    regcomp(&vre, hwe1->vendor, REG_EXTENDED|REG_NOSUB))
		goto out;

	if (hwe1->product &&
	    regcomp(&pre, hwe1->product, REG_EXTENDED|REG_NOSUB))
		goto out_vre;

	if (hwe1->revision &&
	    regcomp(&rre, hwe1->revision, REG_EXTENDED|REG_NOSUB))
		goto out_pre;

	if ((vendor || product || revision) &&
	    (!hwe1->vendor || !vendor ||
	     !regexec(&vre, vendor, 0, NULL, 0)) &&
	    (!hwe1->product || !product ||
	     !regexec(&pre, product, 0, NULL, 0)) &&
	    (!hwe1->revision || !revision ||
	     !regexec(&rre, revision, 0, NULL, 0)))
		retval = 0;

	if (hwe1->revision)
		regfree(&rre);
out_pre:
	if (hwe1->product)
		regfree(&pre);
out_vre:
	if (hwe1->vendor)
		regfree(&vre);
out:
	return retval;
}

static void _log_match(const char *fn, const struct hwentry *h,
		       const char *vendor, const char *product,
		       const char *revision)
{
	condlog(4, "%s: found match /%s:%s:%s/ for '%s:%s:%s'", fn,
		h->vendor, h->product, h->revision,
		vendor, product, revision);
}
#define log_match(h, v, p, r) _log_match(__func__, (h), (v), (p), (r))

int
find_hwe (const struct _vector *hwtable,
	  const char * vendor, const char * product, const char * revision,
	  vector result)
{
	int i, n = 0;
	struct hwentry *tmp;

	/*
	 * Search backwards here, and add forward.
	 * User modified entries are attached at the end of
	 * the list, so we have to check them first before
	 * continuing to the generic entries
	 */
	vector_reset(result);
	vector_foreach_slot_backwards (hwtable, tmp, i) {
		if (hwe_regmatch(tmp, vendor, product, revision))
			continue;
		if (vector_alloc_slot(result)) {
			vector_set_slot(result, tmp);
			n++;
		}
		log_match(tmp, vendor, product, revision);
	}
	condlog(n > 1 ? 3 : 4, "%s: found %d hwtable matches for %s:%s:%s",
		__func__, n, vendor, product, revision);
	return n;
}

struct mpentry *find_mpe(vector mptable, char *wwid)
{
	int i;
	struct mpentry * mpe;

	if (!wwid || !*wwid)
		return NULL;

	vector_foreach_slot (mptable, mpe, i)
		if (mpe->wwid && !strcmp(mpe->wwid, wwid))
			return mpe;

	return NULL;
}

const char *get_mpe_wwid(const struct _vector *mptable, const char *alias)
{
	int i;
	struct mpentry * mpe;

	if (!alias)
		return NULL;

	vector_foreach_slot (mptable, mpe, i)
		if (mpe->alias && strcmp(mpe->alias, alias) == 0)
			return mpe->wwid;

	return NULL;
}

static void
free_pctable (vector pctable)
{
	int i;
	struct pcentry *pce;

	vector_foreach_slot(pctable, pce, i)
		free(pce);

	vector_free(pctable);
}

void
free_hwe (struct hwentry * hwe)
{
	if (!hwe)
		return;

	if (hwe->vendor)
		free(hwe->vendor);

	if (hwe->product)
		free(hwe->product);

	if (hwe->revision)
		free(hwe->revision);

	if (hwe->uid_attribute)
		free(hwe->uid_attribute);

	if (hwe->features)
		free(hwe->features);

	if (hwe->hwhandler)
		free(hwe->hwhandler);

	if (hwe->selector)
		free(hwe->selector);

	if (hwe->checker_name)
		free(hwe->checker_name);

	if (hwe->prio_name)
		free(hwe->prio_name);

	if (hwe->prio_args)
		free(hwe->prio_args);

	if (hwe->alias_prefix)
		free(hwe->alias_prefix);

	if (hwe->bl_product)
		free(hwe->bl_product);

	if (hwe->pctable)
		free_pctable(hwe->pctable);

	free(hwe);
}

void
free_hwtable (vector hwtable)
{
	int i;
	struct hwentry * hwe;

	if (!hwtable)
		return;

	vector_foreach_slot (hwtable, hwe, i)
		free_hwe(hwe);

	vector_free(hwtable);
}

void
free_mpe (struct mpentry * mpe)
{
	if (!mpe)
		return;

	if (mpe->wwid)
		free(mpe->wwid);

	if (mpe->selector)
		free(mpe->selector);

	if (mpe->uid_attribute)
		free(mpe->uid_attribute);

	if (mpe->alias)
		free(mpe->alias);

	if (mpe->prio_name)
		free(mpe->prio_name);

	if (mpe->prio_args)
		free(mpe->prio_args);

	free(mpe);
}

void
free_mptable (vector mptable)
{
	int i;
	struct mpentry * mpe;

	if (!mptable)
		return;

	vector_foreach_slot (mptable, mpe, i)
		free_mpe(mpe);

	vector_free(mptable);
}

struct mpentry *
alloc_mpe (void)
{
	struct mpentry * mpe = (struct mpentry *)
				calloc(1, sizeof(struct mpentry));

	return mpe;
}

struct hwentry *
alloc_hwe (void)
{
	struct hwentry * hwe = (struct hwentry *)
				calloc(1, sizeof(struct hwentry));

	return hwe;
}

struct pcentry *
alloc_pce (void)
{
	struct pcentry *pce = (struct pcentry *)
				calloc(1, sizeof(struct pcentry));
	pce->type = PCE_INVALID;
	return pce;
}

static char *
set_param_str(const char * str)
{
	char * dst;
	int len;

	if (!str)
		return NULL;

	len = strlen(str);

	if (!len)
		return NULL;

	dst = (char *)calloc(1, len + 1);

	if (!dst)
		return NULL;

	strcpy(dst, str);
	return dst;
}

#define merge_str(s) \
	if (!dst->s && src->s && strlen(src->s)) { \
		dst->s = src->s; \
		src->s = NULL; \
	}

#define merge_num(s) \
	if (!dst->s && src->s) \
		dst->s = src->s

static void
merge_pce(struct pcentry *dst, struct pcentry *src)
{
	merge_num(fast_io_fail);
	merge_num(dev_loss);
	merge_num(eh_deadline);
}

static void
merge_hwe (struct hwentry * dst, struct hwentry * src)
{
	char id[SCSI_VENDOR_SIZE+PATH_PRODUCT_SIZE];
	merge_str(vendor);
	merge_str(product);
	merge_str(revision);
	merge_str(uid_attribute);
	merge_str(features);
	merge_str(hwhandler);
	merge_str(selector);
	merge_str(checker_name);
	merge_str(prio_name);
	merge_str(prio_args);
	merge_str(alias_prefix);
	merge_str(bl_product);
	merge_num(pgpolicy);
	merge_num(pgfailback);
	merge_num(rr_weight);
	merge_num(no_path_retry);
	merge_num(minio);
	merge_num(minio_rq);
	merge_num(flush_on_last_del);
	merge_num(fast_io_fail);
	merge_num(dev_loss);
	merge_num(eh_deadline);
	merge_num(user_friendly_names);
	merge_num(retain_hwhandler);
	merge_num(detect_prio);
	merge_num(detect_checker);
	merge_num(deferred_remove);
	merge_num(delay_watch_checks);
	merge_num(delay_wait_checks);
	merge_num(skip_kpartx);
	merge_num(max_sectors_kb);
	merge_num(ghost_delay);
	merge_num(all_tg_pt);
	merge_num(recheck_wwid);
	merge_num(vpd_vendor_id);
	merge_num(san_path_err_threshold);
	merge_num(san_path_err_forget_rate);
	merge_num(san_path_err_recovery_time);
	merge_num(marginal_path_err_sample_time);
	merge_num(marginal_path_err_rate_threshold);
	merge_num(marginal_path_err_recheck_gap_time);
	merge_num(marginal_path_double_failed_time);

	snprintf(id, sizeof(id), "%s/%s", dst->vendor, dst->product);
	reconcile_features_with_options(id, &dst->features,
					&dst->no_path_retry,
					&dst->retain_hwhandler);
}

static void
merge_mpe(struct mpentry *dst, struct mpentry *src)
{
	merge_str(alias);
	merge_str(uid_attribute);
	merge_str(selector);
	merge_str(features);
	merge_str(prio_name);
	merge_str(prio_args);

	if (dst->prkey_source == PRKEY_SOURCE_NONE &&
	    src->prkey_source != PRKEY_SOURCE_NONE) {
		dst->prkey_source = src->prkey_source;
		dst->sa_flags = src->sa_flags;
		memcpy(&dst->reservation_key, &src->reservation_key,
		       sizeof(dst->reservation_key));
	}

	merge_num(pgpolicy);
	merge_num(pgfailback);
	merge_num(rr_weight);
	merge_num(no_path_retry);
	merge_num(minio);
	merge_num(minio_rq);
	merge_num(flush_on_last_del);
	merge_num(attribute_flags);
	merge_num(user_friendly_names);
	merge_num(deferred_remove);
	merge_num(delay_watch_checks);
	merge_num(delay_wait_checks);
	merge_num(san_path_err_threshold);
	merge_num(san_path_err_forget_rate);
	merge_num(san_path_err_recovery_time);
	merge_num(marginal_path_err_sample_time);
	merge_num(marginal_path_err_rate_threshold);
	merge_num(marginal_path_err_recheck_gap_time);
	merge_num(marginal_path_double_failed_time);
	merge_num(skip_kpartx);
	merge_num(max_sectors_kb);
	merge_num(ghost_delay);
	merge_num(uid);
	merge_num(gid);
	merge_num(mode);
}

static int wwid_compar(const void *p1, const void *p2)
{
	const char *wwid1 = (*(struct mpentry * const *)p1)->wwid;
	const char *wwid2 = (*(struct mpentry * const *)p2)->wwid;

	return strcmp(wwid1, wwid2);
}

void merge_mptable(vector mptable)
{
	struct mpentry *mp1, *mp2;
	int i, j;

	vector_foreach_slot(mptable, mp1, i) {
		/* drop invalid multipath configs */
		if (!mp1->wwid) {
			condlog(0, "multipaths config section missing wwid");
			vector_del_slot(mptable, i--);
			free_mpe(mp1);
			continue;
		}
	}
	vector_sort(mptable, wwid_compar);
	vector_foreach_slot(mptable, mp1, i) {
		j = i + 1;
		vector_foreach_slot_after(mptable, mp2, j) {
			if (strcmp(mp1->wwid, mp2->wwid))
				break;
			condlog(1, "%s: duplicate multipath config section for %s",
				__func__, mp1->wwid);
			merge_mpe(mp2, mp1);
			free_mpe(mp1);
			vector_del_slot(mptable, i);
			i--;
			break;
		}
	}
}

int
store_hwe (vector hwtable, struct hwentry * dhwe)
{
	struct hwentry * hwe;

	if (find_hwe_strmatch(hwtable, dhwe))
		return 0;

	if (!(hwe = alloc_hwe()))
		return 1;

	if (!dhwe->vendor || !(hwe->vendor = set_param_str(dhwe->vendor)))
		goto out;

	if (!dhwe->product || !(hwe->product = set_param_str(dhwe->product)))
		goto out;

	if (dhwe->revision && !(hwe->revision = set_param_str(dhwe->revision)))
		goto out;

	if (dhwe->uid_attribute && !(hwe->uid_attribute = set_param_str(dhwe->uid_attribute)))
		goto out;

	if (dhwe->features && !(hwe->features = set_param_str(dhwe->features)))
		goto out;

	if (dhwe->hwhandler && !(hwe->hwhandler = set_param_str(dhwe->hwhandler)))
		goto out;

	if (dhwe->selector && !(hwe->selector = set_param_str(dhwe->selector)))
		goto out;

	if (dhwe->checker_name && !(hwe->checker_name = set_param_str(dhwe->checker_name)))
		goto out;

	if (dhwe->prio_name && !(hwe->prio_name = set_param_str(dhwe->prio_name)))
		goto out;

	if (dhwe->prio_args && !(hwe->prio_args = set_param_str(dhwe->prio_args)))
		goto out;

	if (dhwe->alias_prefix && !(hwe->alias_prefix = set_param_str(dhwe->alias_prefix)))
		goto out;

	hwe->pgpolicy = dhwe->pgpolicy;
	hwe->pgfailback = dhwe->pgfailback;
	hwe->rr_weight = dhwe->rr_weight;
	hwe->no_path_retry = dhwe->no_path_retry;
	hwe->minio = dhwe->minio;
	hwe->minio_rq = dhwe->minio_rq;
	hwe->flush_on_last_del = dhwe->flush_on_last_del;
	hwe->fast_io_fail = dhwe->fast_io_fail;
	hwe->dev_loss = dhwe->dev_loss;
	hwe->eh_deadline = dhwe->eh_deadline;
	hwe->user_friendly_names = dhwe->user_friendly_names;
	hwe->retain_hwhandler = dhwe->retain_hwhandler;
	hwe->detect_prio = dhwe->detect_prio;
	hwe->detect_checker = dhwe->detect_checker;
	hwe->ghost_delay = dhwe->ghost_delay;
	hwe->vpd_vendor_id = dhwe->vpd_vendor_id;

	if (dhwe->bl_product && !(hwe->bl_product = set_param_str(dhwe->bl_product)))
		goto out;

	if (!vector_alloc_slot(hwtable))
		goto out;

	vector_set_slot(hwtable, hwe);
	return 0;
out:
	free_hwe(hwe);
	return 1;
}

static void
validate_pctable(struct hwentry *ovr, int idx, const char *table_desc)
{
	struct pcentry *pce;

	if (!ovr || !ovr->pctable)
		return;

	vector_foreach_slot_after(ovr->pctable, pce, idx) {
		if (pce->type == PCE_INVALID) {
			condlog(0, "protocol section in %s missing type",
				table_desc);
			vector_del_slot(ovr->pctable, idx--);
			free(pce);
		}
	}

	if (VECTOR_SIZE(ovr->pctable) == 0) {
		vector_free(ovr->pctable);
		ovr->pctable = NULL;
	}
}

static void
merge_pctable(struct hwentry *ovr)
{
	struct pcentry *pce1, *pce2;
	int i, j;

	if (!ovr || !ovr->pctable)
		return;

	vector_foreach_slot(ovr->pctable, pce1, i) {
		j = i + 1;
		vector_foreach_slot_after(ovr->pctable, pce2, j) {
			if (pce1->type != pce2->type)
				continue;
			merge_pce(pce2,pce1);
			vector_del_slot(ovr->pctable, i--);
			free(pce1);
			break;
		}
	}
}

static void
factorize_hwtable (vector hw, int n, const char *table_desc)
{
	struct hwentry *hwe1, *hwe2;
	int i, j;

restart:
	vector_foreach_slot(hw, hwe1, i) {
		/* drop invalid device configs */
		if (i >= n && (!hwe1->vendor || !hwe1->product)) {
			condlog(0, "device config in %s missing vendor or product parameter",
				table_desc);
			vector_del_slot(hw, i--);
			free_hwe(hwe1);
			continue;
		}
		j = n > i + 1 ? n : i + 1;
		vector_foreach_slot_after(hw, hwe2, j) {
			if (hwe_strmatch(hwe2, hwe1) == 0) {
				condlog(i >= n ? 1 : 3,
					"%s: duplicate device section for %s:%s:%s in %s",
					__func__, hwe1->vendor, hwe1->product,
					hwe1->revision, table_desc);
				vector_del_slot(hw, i);
				merge_hwe(hwe2, hwe1);
				free_hwe(hwe1);
				if (i < n)
					n -= 1;
				/*
				 * Play safe here; we have modified
				 * the original vector so the outer
				 * vector_foreach_slot() might
				 * become confused.
				 */
				goto restart;
			}
		}
	}
	return;
}

static struct config *alloc_config (void)
{
	return (struct config *)calloc(1, sizeof(struct config));
}

static void _uninit_config(struct config *conf)
{
	void *ptr;
	int i;

	if (!conf)
		conf = &__internal_config;

	if (conf->selector)
		free(conf->selector);

	if (conf->uid_attribute)
		free(conf->uid_attribute);

	vector_foreach_slot(&conf->uid_attrs, ptr, i)
		free(ptr);
	vector_reset(&conf->uid_attrs);

	if (conf->features)
		free(conf->features);

	if (conf->hwhandler)
		free(conf->hwhandler);

	if (conf->bindings_file)
		free(conf->bindings_file);

	if (conf->wwids_file)
		free(conf->wwids_file);

	if (conf->prkeys_file)
		free(conf->prkeys_file);

	if (conf->prio_name)
		free(conf->prio_name);

	if (conf->alias_prefix)
		free(conf->alias_prefix);
	if (conf->partition_delim)
		free(conf->partition_delim);

	if (conf->prio_args)
		free(conf->prio_args);

	if (conf->checker_name)
		free(conf->checker_name);

	if (conf->enable_foreign)
		free(conf->enable_foreign);

	free_blacklist(conf->blist_devnode);
	free_blacklist(conf->blist_wwid);
	free_blacklist(conf->blist_property);
	free_blacklist(conf->blist_protocol);
	free_blacklist_device(conf->blist_device);

	free_blacklist(conf->elist_devnode);
	free_blacklist(conf->elist_wwid);
	free_blacklist(conf->elist_property);
	free_blacklist(conf->elist_protocol);
	free_blacklist_device(conf->elist_device);

	free_mptable(conf->mptable);
	free_hwtable(conf->hwtable);
	free_hwe(conf->overrides);
	free_keywords(conf->keywords);

	memset(conf, 0, sizeof(*conf));
}

void uninit_config(void)
{
	_uninit_config(&__internal_config);
}

void free_config(struct config *conf)
{
	if (!conf)
		return;
	else if (conf == &__internal_config) {
		condlog(0, "ERROR: %s called for internal config. Use uninit_config() instead",
			__func__);
		return;
	}

	_uninit_config(conf);
	free(conf);
}

/* if multipath fails to process the config directory, it should continue,
 * with just a warning message */
static void
process_config_dir(struct config *conf, char *dir)
{
	struct dirent **namelist;
	struct scandir_result sr;
	int i, n;
	char path[LINE_MAX];
	int old_hwtable_size;
	int old_pctable_size = 0;

	if (dir[0] != '/') {
		condlog(1, "config_dir '%s' must be a fully qualified path",
			dir);
		return;
	}
	n = scandir(dir, &namelist, NULL, alphasort);
	if (n < 0) {
		if (errno == ENOENT)
			condlog(3, "No configuration dir '%s'", dir);
		else
			condlog(0, "couldn't open configuration dir '%s': %s",
				dir, strerror(errno));
		return;
	} else if (n == 0)
		return;
	sr.di = namelist;
	sr.n = n;
	pthread_cleanup_push_cast(free_scandir_result, &sr);
	for (i = 0; i < n; i++) {
		char *ext = strrchr(namelist[i]->d_name, '.');

		if (!ext || strcmp(ext, ".conf"))
			continue;

		old_hwtable_size = VECTOR_SIZE(conf->hwtable);
		old_pctable_size = conf->overrides ?
				   VECTOR_SIZE(conf->overrides->pctable) : 0;
		snprintf(path, LINE_MAX, "%s/%s", dir, namelist[i]->d_name);
		path[LINE_MAX-1] = '\0';
		process_file(conf, path);
		factorize_hwtable(conf->hwtable, old_hwtable_size,
				  namelist[i]->d_name);
		validate_pctable(conf->overrides, old_pctable_size,
				 namelist[i]->d_name);
	}
	pthread_cleanup_pop(1);
}

#ifdef USE_SYSTEMD
static void set_max_checkint_from_watchdog(struct config *conf)
{
	char *envp = getenv("WATCHDOG_USEC");
	unsigned long checkint;

	if (envp && sscanf(envp, "%lu", &checkint) == 1) {
		/* Value is in microseconds */
		checkint /= 1000000;
		if (checkint < 1 || checkint > UINT_MAX) {
			condlog(1, "invalid value for WatchdogSec: \"%s\"", envp);
			return;
		}
		if (conf->max_checkint == 0 || conf->max_checkint > checkint)
			conf->max_checkint = checkint;
		condlog(3, "enabling watchdog, interval %ld", checkint);
		conf->use_watchdog = true;
	}
}
#endif

static int _init_config (const char *file, struct config *conf);

int init_config(const char *file)
{
	return _init_config(file, &__internal_config);
}

struct config *load_config(const char *file)
{
	struct config *conf = alloc_config();

	if (conf && !_init_config(file, conf))
		return conf;

	free(conf);
	return NULL;
}

int _init_config (const char *file, struct config *conf)
{

	if (!conf)
		conf = &__internal_config;

	/*
	 * Processing the config file will overwrite conf->verbosity if set
	 * When we return, we'll copy the config value back
	 */
	conf->verbosity = libmp_verbosity;

	/*
	 * internal defaults
	 */
	get_sys_max_fds(&conf->max_fds);
	conf->bindings_file = set_default(DEFAULT_BINDINGS_FILE);
	conf->wwids_file = set_default(DEFAULT_WWIDS_FILE);
	conf->prkeys_file = set_default(DEFAULT_PRKEYS_FILE);
	conf->attribute_flags = 0;
	conf->reassign_maps = DEFAULT_REASSIGN_MAPS;
	conf->checkint = CHECKINT_UNDEF;
	conf->use_watchdog = false;
	conf->max_checkint = 0;
	conf->force_sync = DEFAULT_FORCE_SYNC;
	conf->partition_delim = (default_partition_delim != NULL ?
				 strdup(default_partition_delim) : NULL);
	conf->processed_main_config = 0;
	conf->find_multipaths = DEFAULT_FIND_MULTIPATHS;
	conf->uxsock_timeout = DEFAULT_REPLY_TIMEOUT;
	conf->retrigger_tries = DEFAULT_RETRIGGER_TRIES;
	conf->retrigger_delay = DEFAULT_RETRIGGER_DELAY;
	conf->uev_wait_timeout = DEFAULT_UEV_WAIT_TIMEOUT;
	conf->remove_retries = 0;
	conf->ghost_delay = DEFAULT_GHOST_DELAY;
	conf->all_tg_pt = DEFAULT_ALL_TG_PT;
	conf->recheck_wwid = DEFAULT_RECHECK_WWID;
	/*
	 * preload default hwtable
	 */
	conf->hwtable = vector_alloc();
	if (!conf->hwtable)
			goto out;
	if (setup_default_hwtable(conf->hwtable))
		goto out;

#ifdef CHECK_BUILTIN_HWTABLE
	factorize_hwtable(conf->hwtable, 0, "builtin");
#endif
	/*
	 * read the config file
	 */
	conf->keywords = vector_alloc();
	init_keywords(conf->keywords);
	if (filepresent(file)) {
		int builtin_hwtable_size;

		builtin_hwtable_size = VECTOR_SIZE(conf->hwtable);
		if (process_file(conf, file)) {
			condlog(0, "error parsing config file");
			goto out;
		}
		factorize_hwtable(conf->hwtable, builtin_hwtable_size, file);
		validate_pctable(conf->overrides, 0, file);
	}

	conf->processed_main_config = 1;
	process_config_dir(conf, CONFIG_DIR);

	/*
	 * fill the voids left in the config file
	 */
#ifdef USE_SYSTEMD
	set_max_checkint_from_watchdog(conf);
#endif
	if (conf->max_checkint == 0) {
		if (conf->checkint == CHECKINT_UNDEF)
			conf->checkint = DEFAULT_CHECKINT;
		conf->max_checkint = (conf->checkint < UINT_MAX / 4 ?
				      conf->checkint * 4 : UINT_MAX);
	} else if (conf->checkint == CHECKINT_UNDEF)
		conf->checkint = (conf->max_checkint >= 4 ?
				  conf->max_checkint / 4 : 1);
	else if (conf->checkint > conf->max_checkint)
		conf->checkint = conf->max_checkint;
	condlog(3, "polling interval: %d, max: %d",
		conf->checkint, conf->max_checkint);

	if (conf->blist_devnode == NULL) {
		conf->blist_devnode = vector_alloc();

		if (!conf->blist_devnode)
			goto out;
	}
	if (conf->blist_wwid == NULL) {
		conf->blist_wwid = vector_alloc();

		if (!conf->blist_wwid)
			goto out;
	}
	if (conf->blist_device == NULL) {
		conf->blist_device = vector_alloc();

		if (!conf->blist_device)
			goto out;
	}
	if (conf->blist_property == NULL) {
		conf->blist_property = vector_alloc();

		if (!conf->blist_property)
			goto out;
	}
	if (conf->blist_protocol == NULL) {
		conf->blist_protocol = vector_alloc();

		if (!conf->blist_protocol)
			goto out;
	}

	if (conf->elist_devnode == NULL) {
		conf->elist_devnode = vector_alloc();

		if (!conf->elist_devnode)
			goto out;
	}
	if (conf->elist_wwid == NULL) {
		conf->elist_wwid = vector_alloc();

		if (!conf->elist_wwid)
			goto out;
	}

	if (conf->elist_device == NULL) {
		conf->elist_device = vector_alloc();

		if (!conf->elist_device)
			goto out;
	}

	if (conf->elist_property == NULL) {
		conf->elist_property = vector_alloc();

		if (!conf->elist_property)
			goto out;
	}
	if (conf->elist_protocol == NULL) {
		conf->elist_protocol = vector_alloc();

		if (!conf->elist_protocol)
			goto out;
	}

	if (setup_default_blist(conf))
		goto out;

	if (conf->mptable == NULL) {
		conf->mptable = vector_alloc();
		if (!conf->mptable)
			goto out;
	}

	merge_pctable(conf->overrides);
	merge_mptable(conf->mptable);
	merge_blacklist(conf->blist_devnode);
	merge_blacklist(conf->blist_property);
	merge_blacklist(conf->blist_wwid);
	merge_blacklist_device(conf->blist_device);
	merge_blacklist(conf->elist_devnode);
	merge_blacklist(conf->elist_property);
	merge_blacklist(conf->elist_wwid);
	merge_blacklist_device(conf->elist_device);

	if (conf->bindings_file == NULL)
		conf->bindings_file = set_default(DEFAULT_BINDINGS_FILE);

	if (!conf->bindings_file || !conf->wwids_file || !conf->prkeys_file)
		goto out;

	libmp_verbosity = conf->verbosity;
	return 0;
out:
	_uninit_config(conf);
	return 1;
}

const char *get_uid_attribute_by_attrs(const struct config *conf,
				       const char *path_dev)
{
	const struct _vector *uid_attrs = &conf->uid_attrs;
	int j;
	char *att, *col;

	vector_foreach_slot(uid_attrs, att, j) {
		col = strrchr(att, ':');
		if (!col)
			continue;
		if (!strncmp(path_dev, att, col - att))
			return col + 1;
	}
	return NULL;
}

int parse_uid_attrs(char *uid_attrs, struct config *conf)
{
	vector attrs  = &conf->uid_attrs;
	char *uid_attr_record, *tmp;
	int  ret = 0, count;

	if (!uid_attrs)
		return 1;

	count = get_word(uid_attrs, &uid_attr_record);
	while (uid_attr_record) {
		tmp = strchr(uid_attr_record, ':');
		if (!tmp) {
			condlog(2, "invalid record in uid_attrs: %s",
				uid_attr_record);
			free(uid_attr_record);
			ret = 1;
		} else if (!vector_alloc_slot(attrs)) {
			free(uid_attr_record);
			ret = 1;
		} else
			vector_set_slot(attrs, uid_attr_record);
		if (!count)
			break;
		uid_attrs += count;
		count = get_word(uid_attrs, &uid_attr_record);
	}
	return ret;
}
