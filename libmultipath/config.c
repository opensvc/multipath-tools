/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 * Copyright (c) 2005 Benjamin Marzinski, Redhat
 * Copyright (c) 2005 Edward Goggin, EMC
 */
#include <stdio.h>
#include <string.h>

#include "regex.h"
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

#include "../libcheckers/checkers.h"

struct hwentry *
find_hwe (vector hwtable, char * vendor, char * product)
{
	int i;
	struct hwentry *hwe, *ret = NULL;
	regex_t vre, pre;

	vector_foreach_slot (hwtable, hwe, i) {
		if (regcomp(&vre, hwe->vendor, REG_EXTENDED|REG_NOSUB))
			break;
		if (regcomp(&pre, hwe->product, REG_EXTENDED|REG_NOSUB)) {
			regfree(&vre);
			break;
		}
		if (!regexec(&vre, vendor, 0, NULL, 0) &&
		    !regexec(&pre, product, 0, NULL, 0))
			ret = hwe;
		
		regfree(&pre);
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

	if (hwe->selector)
		FREE(hwe->selector);

	if (hwe->getuid)
		FREE(hwe->getuid);

	if (hwe->getprio)
		FREE(hwe->getprio);

	if (hwe->features)
		FREE(hwe->features);

	if (hwe->hwhandler)
		FREE(hwe->hwhandler);

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

static int
dup_hwe (vector hwtable, char * vendor, char * product)
{
	struct hwentry * hwe = find_hwe(hwtable, vendor, product);

	if (hwe)
		return 1;

	return 0;
}

int
store_hwe (vector hwtable, struct hwentry * dhwe)
{
	struct hwentry * hwe;

	if (dup_hwe(hwtable, dhwe->vendor, dhwe->product))
		return 0;
	
	if (!(hwe = alloc_hwe()))
		return 1;

	if (!dhwe->vendor || !(hwe->vendor = set_param_str(dhwe->vendor)))
		goto out;
	
	if (!dhwe->product || !(hwe->product = set_param_str(dhwe->product)))
		goto out;
	
	if (dhwe->getuid && !(hwe->getuid = set_param_str(dhwe->getuid)))
		goto out;

	if (dhwe->getprio && !(hwe->getprio = set_param_str(dhwe->getprio)))
		goto out;
				
	if (dhwe->features && !(hwe->features = set_param_str(dhwe->features)))
		goto out;
	
	if (dhwe->hwhandler && !(hwe->hwhandler = set_param_str(dhwe->hwhandler)))
		goto out;

	if (dhwe->selector && !(hwe->selector = set_param_str(dhwe->selector)))
		goto out;
				
	hwe->pgpolicy = dhwe->pgpolicy;
	hwe->pgfailback = dhwe->pgfailback;
	hwe->rr_weight = dhwe->rr_weight;
	hwe->no_path_retry = dhwe->no_path_retry;
	hwe->minio = dhwe->minio;
	hwe->checker_index = dhwe->checker_index;


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

	if (conf->selector)
		FREE(conf->selector);

	if (conf->default_getuid)
		FREE(conf->default_getuid);

	if (conf->default_getprio)
		FREE(conf->default_getprio);

	if (conf->features)
		FREE(conf->features);

	if (conf->default_hwhandler)
		FREE(conf->default_hwhandler);

	free_blacklist(conf->blist_devnode);
	free_blacklist(conf->blist_wwid);
	free_blacklist_device(conf->blist_device);
	free_mptable(conf->mptable);
	free_hwtable(conf->hwtable);

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
	conf->bindings_file = DEFAULT_BINDINGS_FILE;

	/*
	 * read the config file
	 */
	if (filepresent(file)) {
		if (init_data(file, init_keywords)) {
			condlog(0, "error parsing config file");
			goto out;
		}
	}
	
	/*
	 * fill the voids left in the config file
	 */
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
		
		if (setup_default_blist(conf->blist_devnode))
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
	if (conf->mptable == NULL) {
		conf->mptable = vector_alloc();

		if (!conf->mptable)
			goto out;
	}
	if (conf->selector == NULL)
		conf->selector = set_default(DEFAULT_SELECTOR);

	if (conf->udev_dir == NULL)
		conf->udev_dir = set_default(DEFAULT_UDEVDIR);

	if (conf->default_getuid == NULL)
		conf->default_getuid = set_default(DEFAULT_GETUID);

	if (conf->features == NULL)
		conf->features = set_default(DEFAULT_FEATURES);

	if (conf->default_hwhandler == NULL)
		conf->default_hwhandler = set_default(DEFAULT_HWHANDLER);

	if (!conf->selector  || !conf->udev_dir         ||
	    !conf->default_getuid    || !conf->features ||
	    !conf->default_hwhandler)
		goto out;

	if (!conf->checker_index)
		conf->checker_index = READSECTOR0;

	return 0;
out:
	free_config(conf);
	return 1;
}

