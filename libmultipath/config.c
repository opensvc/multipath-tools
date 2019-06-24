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
#include "memory.h"
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
		if (vector_alloc_slot(result) != NULL) {
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

	if (!wwid)
		return NULL;

	vector_foreach_slot (mptable, mpe, i)
		if (mpe->wwid && !strcmp(mpe->wwid, wwid))
			return mpe;

	return NULL;
}

char *get_mpe_wwid(vector mptable, char *alias)
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

void
free_hwe (struct hwentry * hwe)
{
	if (!hwe)
		return;

	if (hwe->vendor)
		FREE(hwe->vendor);

	if (hwe->product)
		FREE(hwe->product);

	if (hwe->revision)
		FREE(hwe->revision);

	if (hwe->getuid)
		FREE(hwe->getuid);

	if (hwe->uid_attribute)
		FREE(hwe->uid_attribute);

	if (hwe->features)
		FREE(hwe->features);

	if (hwe->hwhandler)
		FREE(hwe->hwhandler);

	if (hwe->selector)
		FREE(hwe->selector);

	if (hwe->checker_name)
		FREE(hwe->checker_name);

	if (hwe->prio_name)
		FREE(hwe->prio_name);

	if (hwe->prio_args)
		FREE(hwe->prio_args);

	if (hwe->alias_prefix)
		FREE(hwe->alias_prefix);

	if (hwe->bl_product)
		FREE(hwe->bl_product);

	FREE(hwe);
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
		FREE(mpe->wwid);

	if (mpe->selector)
		FREE(mpe->selector);

	if (mpe->getuid)
		FREE(mpe->getuid);

	if (mpe->uid_attribute)
		FREE(mpe->uid_attribute);

	if (mpe->alias)
		FREE(mpe->alias);

	if (mpe->prio_name)
		FREE(mpe->prio_name);

	if (mpe->prio_args)
		FREE(mpe->prio_args);

	FREE(mpe);
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
				MALLOC(sizeof(struct mpentry));

	return mpe;
}

struct hwentry *
alloc_hwe (void)
{
	struct hwentry * hwe = (struct hwentry *)
				MALLOC(sizeof(struct hwentry));

	return hwe;
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

	dst = (char *)MALLOC(len + 1);

	if (!dst)
		return NULL;

	strcpy(dst, str);
	return dst;
}

#define merge_str(s) \
	if (!dst->s && src->s) { \
		if (!(dst->s = set_param_str(src->s))) \
			return 1; \
	}

#define merge_num(s) \
	if (!dst->s && src->s) \
		dst->s = src->s


static int
merge_hwe (struct hwentry * dst, struct hwentry * src)
{
	char id[SCSI_VENDOR_SIZE+PATH_PRODUCT_SIZE];
	merge_str(vendor);
	merge_str(product);
	merge_str(revision);
	merge_str(getuid);
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
	merge_num(san_path_err_threshold);
	merge_num(san_path_err_forget_rate);
	merge_num(san_path_err_recovery_time);

	snprintf(id, sizeof(id), "%s/%s", dst->vendor, dst->product);
	reconcile_features_with_options(id, &dst->features,
					&dst->no_path_retry,
					&dst->retain_hwhandler);
	return 0;
}

static int
merge_mpe(struct mpentry *dst, struct mpentry *src)
{
	if (!dst || !src)
		return 1;

	merge_str(alias);
	merge_str(uid_attribute);
	merge_str(getuid);
	merge_str(selector);
	merge_str(features);
	merge_str(prio_name);
	merge_str(prio_args);

	if (dst->prkey_source == PRKEY_SOURCE_NONE &&
	    src->prkey_source != PRKEY_SOURCE_NONE) {
		dst->prkey_source = src->prkey_source;
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

	return 0;
}

void merge_mptable(vector mptable)
{
	struct mpentry *mp1, *mp2;
	int i, j;

	vector_foreach_slot(mptable, mp1, i) {
		j = i + 1;
		vector_foreach_slot_after(mptable, mp2, j) {
			if (strcmp(mp1->wwid, mp2->wwid))
				continue;
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

	if (dhwe->getuid && !(hwe->getuid = set_param_str(dhwe->getuid)))
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
	hwe->user_friendly_names = dhwe->user_friendly_names;
	hwe->retain_hwhandler = dhwe->retain_hwhandler;
	hwe->detect_prio = dhwe->detect_prio;
	hwe->detect_checker = dhwe->detect_checker;
	hwe->ghost_delay = dhwe->ghost_delay;

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

struct config *
alloc_config (void)
{
	return (struct config *)MALLOC(sizeof(struct config));
}

void
free_config (struct config * conf)
{
	if (!conf)
		return;

	if (conf->multipath_dir)
		FREE(conf->multipath_dir);

	if (conf->selector)
		FREE(conf->selector);

	if (conf->uid_attribute)
		FREE(conf->uid_attribute);

	vector_reset(&conf->uid_attrs);

	if (conf->getuid)
		FREE(conf->getuid);

	if (conf->features)
		FREE(conf->features);

	if (conf->hwhandler)
		FREE(conf->hwhandler);

	if (conf->bindings_file)
		FREE(conf->bindings_file);

	if (conf->wwids_file)
		FREE(conf->wwids_file);

	if (conf->prkeys_file)
		FREE(conf->prkeys_file);

	if (conf->prio_name)
		FREE(conf->prio_name);

	if (conf->alias_prefix)
		FREE(conf->alias_prefix);
	if (conf->partition_delim)
		FREE(conf->partition_delim);

	if (conf->prio_args)
		FREE(conf->prio_args);

	if (conf->checker_name)
		FREE(conf->checker_name);

	if (conf->config_dir)
		FREE(conf->config_dir);

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
	FREE(conf);
}

/* if multipath fails to process the config directory, it should continue,
 * with just a warning message */
static void
process_config_dir(struct config *conf, vector keywords, char *dir)
{
	struct dirent **namelist;
	struct scandir_result sr;
	int i, n;
	char path[LINE_MAX];
	int old_hwtable_size;

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
		if (!strstr(namelist[i]->d_name, ".conf"))
			continue;
		old_hwtable_size = VECTOR_SIZE(conf->hwtable);
		snprintf(path, LINE_MAX, "%s/%s", dir, namelist[i]->d_name);
		path[LINE_MAX-1] = '\0';
		process_file(conf, path);
		factorize_hwtable(conf->hwtable, old_hwtable_size,
				  namelist[i]->d_name);
	}
	pthread_cleanup_pop(1);
}

struct config *
load_config (char * file)
{
	struct config *conf = alloc_config();

	if (!conf)
		return NULL;

	/*
	 * internal defaults
	 */
	conf->verbosity = DEFAULT_VERBOSITY;

	get_sys_max_fds(&conf->max_fds);
	conf->bindings_file = set_default(DEFAULT_BINDINGS_FILE);
	conf->wwids_file = set_default(DEFAULT_WWIDS_FILE);
	conf->prkeys_file = set_default(DEFAULT_PRKEYS_FILE);
	conf->multipath_dir = set_default(DEFAULT_MULTIPATHDIR);
	conf->attribute_flags = 0;
	conf->reassign_maps = DEFAULT_REASSIGN_MAPS;
	conf->checkint = DEFAULT_CHECKINT;
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
	}

	conf->processed_main_config = 1;
	if (conf->config_dir == NULL)
		conf->config_dir = set_default(DEFAULT_CONFIG_DIR);
	if (conf->config_dir && conf->config_dir[0] != '\0')
		process_config_dir(conf, conf->keywords, conf->config_dir);

	/*
	 * fill the voids left in the config file
	 */
	if (conf->max_checkint == 0)
		conf->max_checkint = MAX_CHECKINT(conf->checkint);
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

	if (!conf->multipath_dir || !conf->bindings_file ||
	    !conf->wwids_file || !conf->prkeys_file)
		goto out;

	return conf;
out:
	free_config(conf);
	return NULL;
}

char *get_uid_attribute_by_attrs(struct config *conf,
				 const char *path_dev)
{
	vector uid_attrs = &conf->uid_attrs;
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
