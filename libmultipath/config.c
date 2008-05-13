/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 * Copyright (c) 2005 Benjamin Marzinski, Redhat
 * Copyright (c) 2005 Edward Goggin, EMC
 */
#include <stdio.h>
#include <string.h>

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

static struct hwentry *
find_hwe_strmatch (vector hwtable, char * vendor, char * product, char * revision)
{
	int i;
	struct hwentry *hwe, *ret = NULL;

	vector_foreach_slot (hwtable, hwe, i) {
		if (hwe->vendor && vendor && strcmp(hwe->vendor, vendor))
			continue;

		if (hwe->product && product && strcmp(hwe->product, product))
			continue;

		if (hwe->revision && revision && strcmp(hwe->revision, revision))
			continue;

		ret = hwe;
		break;
	}
	return ret;
}

struct hwentry *
find_hwe (vector hwtable, char * vendor, char * product, char * revision)
{
	int i;
	struct hwentry *hwe, *ret = NULL;
	regex_t vre, pre, rre;

	vector_foreach_slot (hwtable, hwe, i) {
		if (hwe->vendor &&
		    regcomp(&vre, hwe->vendor, REG_EXTENDED|REG_NOSUB))
			break;
		if (hwe->product &&
		    regcomp(&pre, hwe->product, REG_EXTENDED|REG_NOSUB)) {
			regfree(&vre);
			break;
		}
		if (hwe->revision &&
		    regcomp(&rre, hwe->revision, REG_EXTENDED|REG_NOSUB)) {
			regfree(&vre);
			regfree(&pre);
			break;
		}
		if ((!hwe->vendor || !regexec(&vre, vendor, 0, NULL, 0)) &&
		    (!hwe->product || !regexec(&pre, product, 0, NULL, 0)) &&
		    (!hwe->revision || !regexec(&rre, revision, 0, NULL, 0)))
			ret = hwe;

		if (hwe->revision)
			regfree(&rre);
		if (hwe->product)
			regfree(&pre);
		if (hwe->vendor)
			regfree(&vre);

		if (ret)
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

	if (hwe->selector)
		FREE(hwe->selector);

	if (hwe->getuid)
		FREE(hwe->getuid);

	if (hwe->features)
		FREE(hwe->features);

	if (hwe->hwhandler)
		FREE(hwe->hwhandler);

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

	if (mpe->alias)
		FREE(mpe->alias);

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

static struct hwentry *
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

int
store_hwe (vector hwtable, struct hwentry * dhwe)
{
	struct hwentry * hwe;

	if (find_hwe_strmatch(hwtable, dhwe->vendor, dhwe->product, dhwe->revision))
		return 0;
	
	if (!(hwe = alloc_hwe()))
		return 1;

	if (!dhwe->vendor || !(hwe->vendor = set_param_str(dhwe->vendor)))
		goto out;
	
	if (!dhwe->product || !(hwe->product = set_param_str(dhwe->product)))
		goto out;
	
	if (dhwe->revision && !(hwe->revision = set_param_str(dhwe->revision)))
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
				
	hwe->pgpolicy = dhwe->pgpolicy;
	hwe->pgfailback = dhwe->pgfailback;
	hwe->rr_weight = dhwe->rr_weight;
	hwe->no_path_retry = dhwe->no_path_retry;
	hwe->minio = dhwe->minio;
	hwe->prio = dhwe->prio;

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

	if (conf->udev_dir)
		FREE(conf->udev_dir);

	if (conf->multipath_dir)
		FREE(conf->multipath_dir);

	if (conf->selector)
		FREE(conf->selector);

	if (conf->getuid)
		FREE(conf->getuid);

	if (conf->features)
		FREE(conf->features);

	if (conf->hwhandler)
		FREE(conf->hwhandler);

	free_blacklist(conf->blist_devnode);
	free_blacklist(conf->blist_wwid);
	free_blacklist_device(conf->blist_device);

	free_blacklist(conf->elist_devnode);
	free_blacklist(conf->elist_wwid);
	free_blacklist_device(conf->elist_device);

	free_mptable(conf->mptable);
	free_hwtable(conf->hwtable);
	free_keywords(conf->keywords);
	FREE(conf);
}

int
load_config (char * file)
{
	if (!conf)
		conf = alloc_config();

	if (!conf)
		return 1;

	/*
	 * internal defaults
	 */
	if (!conf->verbosity)
		conf->verbosity = 2;

	conf->dev_type = DEV_NONE;
	conf->minio = 1000;
	conf->max_fds = 0;
	conf->bindings_file = DEFAULT_BINDINGS_FILE;

	/*
	 * read the config file
	 */
	if (filepresent(file)) {
		set_current_keywords(&conf->keywords);
		if (init_data(file, init_keywords)) {
			condlog(0, "error parsing config file");
			goto out;
		}
	}

	/*
	 * fill the voids left in the config file
	 */
	if (conf->multipath_dir == NULL)
		conf->multipath_dir = set_default(DEFAULT_MULTIPATHDIR);

	if (conf->hwtable == NULL) {
		conf->hwtable = vector_alloc();

		if (!conf->hwtable)
			goto out;
	}
	if (setup_default_hwtable(conf->hwtable))
		goto out;

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
	if (setup_default_blist(conf))
		goto out;

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

	if (conf->mptable == NULL) {
		conf->mptable = vector_alloc();

		if (!conf->mptable)
			goto out;
	}
	if (conf->selector == NULL)
		conf->selector = set_default(DEFAULT_SELECTOR);

	if (conf->udev_dir == NULL)
		conf->udev_dir = set_default(DEFAULT_UDEVDIR);

	if (conf->getuid == NULL)
		conf->getuid = set_default(DEFAULT_GETUID);

	if (conf->features == NULL)
		conf->features = set_default(DEFAULT_FEATURES);

	if (conf->hwhandler == NULL)
		conf->hwhandler = set_default(DEFAULT_HWHANDLER);

	if (!conf->selector  || !conf->udev_dir || !conf->multipath_dir ||
	    !conf->getuid    || !conf->features ||
	    !conf->hwhandler)
		goto out;

	if (!conf->prio)
		conf->prio = prio_default();

	if (!conf->checker_name)
		conf->checker_name = set_default(DEFAULT_CHECKER);

	return 0;
out:
	free_config(conf);
	return 1;
}

