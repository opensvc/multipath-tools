/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 * Copyright (c) 2005 Benjamin Marzinski, Redhat
 * Copyright (c) 2005 Edward Goggin, EMC
 */
#include <stdio.h>
#include <string.h>
#include <libudev.h>

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

static int
hwe_strmatch (struct hwentry *hwe1, struct hwentry *hwe2)
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
find_hwe_strmatch (vector hwtable, struct hwentry *hwe)
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
hwe_regmatch (struct hwentry *hwe1, struct hwentry *hwe2)
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

	if ((!hwe1->vendor || !hwe2->vendor ||
	     !regexec(&vre, hwe2->vendor, 0, NULL, 0)) &&
	    (!hwe1->product || !hwe2->product ||
	     !regexec(&pre, hwe2->product, 0, NULL, 0)) &&
	    (!hwe1->revision || !hwe2->revision ||
	     !regexec(&rre, hwe2->revision, 0, NULL, 0)))
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

struct hwentry *
find_hwe (vector hwtable, char * vendor, char * product, char * revision)
{
	int i;
	struct hwentry hwe, *tmp, *ret = NULL;

	hwe.vendor = vendor;
	hwe.product = product;
	hwe.revision = revision;
	/*
	 * Search backwards here.
	 * User modified entries are attached at the end of
	 * the list, so we have to check them first before
	 * continuing to the generic entries
	 */
	vector_foreach_slot_backwards (hwtable, tmp, i) {
		if (hwe_regmatch(tmp, &hwe))
			continue;
		ret = tmp;
		break;
	}
	return ret;
}

extern struct mpentry *
find_mpe (char * wwid)
{
	int i;
	struct mpentry * mpe;

	if (!wwid)
		return NULL;

	vector_foreach_slot (conf->mptable, mpe, i)
		if (mpe->wwid && !strcmp(mpe->wwid, wwid))
			return mpe;

	return NULL;
}

extern char *
get_mpe_wwid (char * alias)
{
	int i;
	struct mpentry * mpe;

	if (!alias)
		return NULL;

	vector_foreach_slot (conf->mptable, mpe, i)
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
set_param_str(char * str)
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

	/*
	 * Make sure features is consistent with
	 * no_path_retry
	 */
	if (dst->no_path_retry == NO_PATH_RETRY_FAIL)
		remove_feature(&dst->features, "queue_if_no_path");
	else if (dst->no_path_retry != NO_PATH_RETRY_UNDEF)
		add_feature(&dst->features, "queue_if_no_path");

	return 0;
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
factorize_hwtable (vector hw, int n)
{
	struct hwentry *hwe1, *hwe2;
	int i, j;

restart:
	vector_foreach_slot(hw, hwe1, i) {
		if (i == n)
			break;
		j = n;
		vector_foreach_slot_after(hw, hwe2, j) {
			if (hwe_regmatch(hwe1, hwe2))
				continue;
			/* dup */
			merge_hwe(hwe2, hwe1);
			if (hwe_strmatch(hwe2, hwe1) == 0) {
				vector_del_slot(hw, i);
				free_hwe(hwe1);
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

	if (conf->dev)
		FREE(conf->dev);

	if (conf->multipath_dir)
		FREE(conf->multipath_dir);

	if (conf->selector)
		FREE(conf->selector);

	if (conf->uid_attribute)
		FREE(conf->uid_attribute);

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
	if (conf->prio_name)
		FREE(conf->prio_name);

	if (conf->alias_prefix)
		FREE(conf->alias_prefix);

	if (conf->prio_args)
		FREE(conf->prio_args);

	if (conf->checker_name)
		FREE(conf->checker_name);
	if (conf->reservation_key)
		FREE(conf->reservation_key);

	free_blacklist(conf->blist_devnode);
	free_blacklist(conf->blist_wwid);
	free_blacklist(conf->blist_property);
	free_blacklist_device(conf->blist_device);

	free_blacklist(conf->elist_devnode);
	free_blacklist(conf->elist_wwid);
	free_blacklist(conf->elist_property);
	free_blacklist_device(conf->elist_device);

	free_mptable(conf->mptable);
	free_hwtable(conf->hwtable);
	free_keywords(conf->keywords);
	FREE(conf);
}

int
load_config (char * file, struct udev *udev)
{
	if (!conf)
		conf = alloc_config();

	if (!conf || !udev)
		return 1;

	/*
	 * internal defaults
	 */
	if (!conf->verbosity)
		conf->verbosity = DEFAULT_VERBOSITY;

	conf->udev = udev;
	conf->dev_type = DEV_NONE;
	conf->minio = DEFAULT_MINIO;
	conf->minio_rq = DEFAULT_MINIO_RQ;
	get_sys_max_fds(&conf->max_fds);
	conf->bindings_file = set_default(DEFAULT_BINDINGS_FILE);
	conf->wwids_file = set_default(DEFAULT_WWIDS_FILE);
	conf->bindings_read_only = 0;
	conf->multipath_dir = set_default(DEFAULT_MULTIPATHDIR);
	conf->features = set_default(DEFAULT_FEATURES);
	conf->flush_on_last_del = 0;
	conf->attribute_flags = 0;
	conf->reassign_maps = DEFAULT_REASSIGN_MAPS;
	conf->checkint = DEFAULT_CHECKINT;
	conf->max_checkint = MAX_CHECKINT(conf->checkint);
	conf->pgfailback = DEFAULT_FAILBACK;
	conf->fast_io_fail = DEFAULT_FAST_IO_FAIL;
	conf->retain_hwhandler = DEFAULT_RETAIN_HWHANDLER;
	conf->detect_prio = DEFAULT_DETECT_PRIO;

	/*
	 * preload default hwtable
	 */
	if (conf->hwtable == NULL) {
		conf->hwtable = vector_alloc();

		if (!conf->hwtable)
			goto out;
	}
	if (setup_default_hwtable(conf->hwtable))
		goto out;

	/*
	 * read the config file
	 */
	set_current_keywords(&conf->keywords);
	alloc_keywords();
	if (filepresent(file)) {
		int builtin_hwtable_size;

		builtin_hwtable_size = VECTOR_SIZE(conf->hwtable);
		if (init_data(file, init_keywords)) {
			condlog(0, "error parsing config file");
			goto out;
		}
		if (VECTOR_SIZE(conf->hwtable) > builtin_hwtable_size) {
			/*
			 * remove duplica in hwtable. config file
			 * takes precedence over build-in hwtable
			 */
			factorize_hwtable(conf->hwtable, builtin_hwtable_size);
		}

	} else {
		init_keywords();
	}

	/*
	 * fill the voids left in the config file
	 */
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
	if (setup_default_blist(conf))
		goto out;

	if (conf->mptable == NULL) {
		conf->mptable = vector_alloc();
		if (!conf->mptable)
			goto out;
	}
	if (conf->bindings_file == NULL)
		conf->bindings_file = set_default(DEFAULT_BINDINGS_FILE);

	if (!conf->multipath_dir || !conf->bindings_file ||
	    !conf->wwids_file)
		goto out;

	return 0;
out:
	free_config(conf);
	return 1;
}

