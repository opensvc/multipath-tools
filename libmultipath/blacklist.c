/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 */
#include <stdio.h>
#include <libudev.h>

#include "checkers.h"
#include "memory.h"
#include "vector.h"
#include "util.h"
#include "debug.h"
#include "structs.h"
#include "config.h"
#include "blacklist.h"

extern int
store_ble (vector blist, char * str, int origin)
{
	struct blentry * ble;

	if (!str)
		return 0;

	if (!blist)
		goto out;

	ble = MALLOC(sizeof(struct blentry));

	if (!ble)
		goto out;

	if (regcomp(&ble->regex, str, REG_EXTENDED|REG_NOSUB))
		goto out1;

	if (!vector_alloc_slot(blist))
		goto out1;

	ble->str = str;
	ble->origin = origin;
	vector_set_slot(blist, ble);
	return 0;
out1:
	FREE(ble);
out:
	FREE(str);
	return 1;
}


extern int
alloc_ble_device (vector blist)
{
	struct blentry_device * ble = MALLOC(sizeof(struct blentry_device));

	if (!ble)
		return 1;

	if (!blist || !vector_alloc_slot(blist)) {
		FREE(ble);
		return 1;
	}
	vector_set_slot(blist, ble);
	return 0;
}

extern int
set_ble_device (vector blist, char * vendor, char * product, int origin)
{
	struct blentry_device * ble;

	if (!blist)
		return 1;

	ble = VECTOR_LAST_SLOT(blist);

	if (!ble)
		return 1;

	if (vendor) {
		if (regcomp(&ble->vendor_reg, vendor,
			    REG_EXTENDED|REG_NOSUB)) {
			FREE(vendor);
			return 1;
		}
		ble->vendor = vendor;
	}
	if (product) {
		if (regcomp(&ble->product_reg, product,
			    REG_EXTENDED|REG_NOSUB)) {
			FREE(product);
			return 1;
		}
		ble->product = product;
	}
	ble->origin = origin;
	return 0;
}

int
_blacklist_exceptions (vector elist, const char * str)
{
	int i;
	struct blentry * ele;

	vector_foreach_slot (elist, ele, i) {
		if (!regexec(&ele->regex, str, 0, NULL, 0))
			return 1;
	}
	return 0;
}

int
_blacklist (vector blist, const char * str)
{
	int i;
	struct blentry * ble;

	vector_foreach_slot (blist, ble, i) {
		if (!regexec(&ble->regex, str, 0, NULL, 0))
			return 1;
	}
	return 0;
}

int
_blacklist_exceptions_device(vector elist, char * vendor, char * product)
{
	int i;
	struct blentry_device * ble;

	vector_foreach_slot (elist, ble, i) {
		if (!regexec(&ble->vendor_reg, vendor, 0, NULL, 0) &&
		    !regexec(&ble->product_reg, product, 0, NULL, 0))
			return 1;
	}
	return 0;
}

int
_blacklist_device (vector blist, char * vendor, char * product)
{
	int i;
	struct blentry_device * ble;

	vector_foreach_slot (blist, ble, i) {
		if (!regexec(&ble->vendor_reg, vendor, 0, NULL, 0) &&
		    !regexec(&ble->product_reg, product, 0, NULL, 0))
			return 1;
	}
	return 0;
}

int
setup_default_blist (struct config * conf)
{
	struct blentry * ble;
	struct hwentry *hwe;
	char * str;
	int i;

	str = STRDUP("^(ram|raw|loop|fd|md|dm-|sr|scd|st)[0-9]*");
	if (!str)
		return 1;
	if (store_ble(conf->blist_devnode, str, ORIGIN_DEFAULT))
		return 1;

	str = STRDUP("^(td|hd)[a-z]");
	if (!str)
		return 1;
	if (store_ble(conf->blist_devnode, str, ORIGIN_DEFAULT))
		return 1;

	str = STRDUP("^dcssblk[0-9]*");
	if (!str)
		return 1;
	if (store_ble(conf->blist_devnode, str, ORIGIN_DEFAULT))
		return 1;

	str = STRDUP("(ID_SCSI_VPD|ID_WWN)");
	if (!str)
		return 1;
	if (store_ble(conf->elist_property, str, ORIGIN_DEFAULT))
		return 1;

	vector_foreach_slot (conf->hwtable, hwe, i) {
		if (hwe->bl_product) {
			if (_blacklist_device(conf->blist_device, hwe->vendor,
					      hwe->bl_product))
				continue;
			if (alloc_ble_device(conf->blist_device))
				return 1;
			ble = VECTOR_SLOT(conf->blist_device,
					  VECTOR_SIZE(conf->blist_device) -1);
			if (set_ble_device(conf->blist_device,
					   STRDUP(hwe->vendor),
					   STRDUP(hwe->bl_product),
					   ORIGIN_DEFAULT)) {
				FREE(ble);
				return 1;
			}
		}
	}
	return 0;
}

#define LOG_BLIST(M,S)							\
	if (vendor && product)						\
		condlog(3, "%s: (%s:%s) %s %s",				\
			dev, vendor, product, (M), (S));		\
	else if (wwid)							\
		condlog(3, "%s: %s %s %s", dev, (M), wwid, (S));	\
	else if (env)							\
		condlog(3, "%s: %s %s %s", dev, (M), env, (S));		\
	else								\
		condlog(3, "%s: %s %s", dev, (M), (S))

void
log_filter (const char *dev, char *vendor, char *product, char *wwid,
	    const char *env, int r)
{
	/*
	 * Try to sort from most likely to least.
	 */
	switch (r) {
	case MATCH_NOTHING:
		break;
	case MATCH_DEVICE_BLIST:
		LOG_BLIST("vendor/product", "blacklisted");
		break;
	case MATCH_WWID_BLIST:
		LOG_BLIST("wwid", "blacklisted");
		break;
	case MATCH_DEVNODE_BLIST:
		LOG_BLIST("device node name", "blacklisted");
		break;
	case MATCH_PROPERTY_BLIST:
		LOG_BLIST("udev property", "blacklisted");
		break;
	case MATCH_DEVICE_BLIST_EXCEPT:
		LOG_BLIST("vendor/product", "whitelisted");
		break;
	case MATCH_WWID_BLIST_EXCEPT:
		LOG_BLIST("wwid", "whitelisted");
		break;
	case MATCH_DEVNODE_BLIST_EXCEPT:
		LOG_BLIST("device node name", "whitelisted");
		break;
	case MATCH_PROPERTY_BLIST_EXCEPT:
		LOG_BLIST("udev property", "whitelisted");
		break;
	case MATCH_PROPERTY_BLIST_MISSING:
		LOG_BLIST("blacklisted,", "udev property missing");
		break;
	}
}

int
_filter_device (vector blist, vector elist, char * vendor, char * product)
{
	if (!vendor || !product)
		return 0;
	if (_blacklist_exceptions_device(elist, vendor, product))
		return MATCH_DEVICE_BLIST_EXCEPT;
	if (_blacklist_device(blist, vendor, product))
		return MATCH_DEVICE_BLIST;
	return 0;
}

int
filter_device (vector blist, vector elist, char * vendor, char * product)
{
	int r = _filter_device(blist, elist, vendor, product);
	log_filter(NULL, vendor, product, NULL, NULL, r);
	return r;
}

int
_filter_devnode (vector blist, vector elist, char * dev)
{
	if (!dev)
		return 0;
	if (_blacklist_exceptions(elist, dev))
		return MATCH_DEVNODE_BLIST_EXCEPT;
	if (_blacklist(blist, dev))
		return MATCH_DEVNODE_BLIST;
	return 0;
}

int
filter_devnode (vector blist, vector elist, char * dev)
{
	int r = _filter_devnode(blist, elist, dev);
	log_filter(dev, NULL, NULL, NULL, NULL, r);
	return r;
}

int
_filter_wwid (vector blist, vector elist, char * wwid)
{
	if (!wwid)
		return 0;
	if (_blacklist_exceptions(elist, wwid))
		return MATCH_WWID_BLIST_EXCEPT;
	if (_blacklist(blist, wwid))
		return MATCH_WWID_BLIST;
	return 0;
}

int
filter_wwid (vector blist, vector elist, char * wwid)
{
	int r = _filter_wwid(blist, elist, wwid);
	log_filter(NULL, NULL, NULL, wwid, NULL, r);
	return r;
}

int
_filter_path (struct config * conf, struct path * pp)
{
	int r;

	r = _filter_devnode(conf->blist_devnode, conf->elist_devnode,pp->dev);
	if (r > 0)
		return r;
	r = _filter_device(conf->blist_device, conf->elist_device,
			   pp->vendor_id, pp->product_id);
	if (r > 0)
		return r;
	r = _filter_wwid(conf->blist_wwid, conf->elist_wwid, pp->wwid);
	return r;
}

int
filter_path (struct config * conf, struct path * pp)
{
	int r=_filter_path(conf, pp);
	log_filter(pp->dev, pp->vendor_id, pp->product_id, pp->wwid, NULL, r);
	return r;
}

int
_filter_property (struct config *conf, const char *env)
{
	if (_blacklist_exceptions(conf->elist_property, env))
		return MATCH_PROPERTY_BLIST_EXCEPT;
	if (_blacklist(conf->blist_property, env))
		return MATCH_PROPERTY_BLIST;

	return 0;
}

int
filter_property(struct config * conf, struct udev_device * udev)
{
	const char *devname = udev_device_get_sysname(udev);
	struct udev_list_entry *list_entry;
	int r;

	if (!udev)
		return 0;

	udev_list_entry_foreach(list_entry,
				udev_device_get_properties_list_entry(udev)) {
		const char *env;

		env = udev_list_entry_get_name(list_entry);
		if (!env)
			continue;

		r = _filter_property(conf, env);
		if (r) {
			log_filter(devname, NULL, NULL, NULL, env, r);
			return r;
		}
	}

	/*
	 * This is the inverse of the 'normal' matching;
	 * the environment variable _has_ to match.
	 */
	log_filter(devname, NULL, NULL, NULL, NULL,
		   MATCH_PROPERTY_BLIST_MISSING);
	return MATCH_PROPERTY_BLIST_MISSING;
}

void
free_blacklist (vector blist)
{
	struct blentry * ble;
	int i;

	if (!blist)
		return;

	vector_foreach_slot (blist, ble, i) {
		if (ble) {
			regfree(&ble->regex);
			FREE(ble->str);
			FREE(ble);
		}
	}
	vector_free(blist);
}

void
free_blacklist_device (vector blist)
{
	struct blentry_device * ble;
	int i;

	if (!blist)
		return;

	vector_foreach_slot (blist, ble, i) {
		if (ble) {
			if (ble->vendor) {
				regfree(&ble->vendor_reg);
				FREE(ble->vendor);
			}
			if (ble->product) {
				regfree(&ble->product_reg);
				FREE(ble->product);
			}
			FREE(ble);
		}
	}
	vector_free(blist);
}
