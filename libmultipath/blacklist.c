/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 */
#include <stdio.h>
#include <libudev.h>

#include "checkers.h"
#include "vector.h"
#include "util.h"
#include "debug.h"
#include "structs.h"
#include "config.h"
#include "blacklist.h"
#include "structs_vec.h"
#include "print.h"
#include "strbuf.h"

char *check_invert(char *str, bool *invert)
{
	if (str[0] == '!') {
		*invert = true;
		return str + 1;
	}
	if (str[0] == '\\' && str[1] == '!') {
		*invert = false;
		return str + 1;
	}
	*invert = false;
	return str;
}

int store_ble(vector blist, const char *str, int origin)
{
	struct blentry * ble;
	char *regex_str;
	char *strdup_str = NULL;

	if (!str)
		return 0;

	strdup_str = strdup(str);
	if (!strdup_str)
		return 1;

	if (!blist)
		goto out;

	ble = calloc(1, sizeof(struct blentry));

	if (!ble)
		goto out;

	regex_str = check_invert(strdup_str, &ble->invert);
	if (regcomp(&ble->regex, regex_str, REG_EXTENDED|REG_NOSUB))
		goto out1;

	if (!vector_alloc_slot(blist))
		goto out1;

	ble->str = strdup_str;
	ble->origin = origin;
	vector_set_slot(blist, ble);
	return 0;
out1:
	free(ble);
out:
	free(strdup_str);
	return 1;
}


int alloc_ble_device(vector blist)
{
	struct blentry_device *ble;

	if (!blist)
		return 1;

	ble = calloc(1, sizeof(struct blentry_device));
	if (!ble)
		return 1;

	if (!vector_alloc_slot(blist)) {
		free(ble);
		return 1;
	}
	vector_set_slot(blist, ble);
	return 0;
}

int set_ble_device(vector blist, const char *vendor, const char *product, int origin)
{
	struct blentry_device * ble;
	char *regex_str;
	char *vendor_str = NULL;
	char *product_str = NULL;

	if (!blist)
		return 1;

	ble = VECTOR_LAST_SLOT(blist);

	if (!ble)
		return 1;

	if (vendor) {
		vendor_str = strdup(vendor);
		if (!vendor_str)
			goto out;

		regex_str = check_invert(vendor_str, &ble->vendor_invert);
		if (regcomp(&ble->vendor_reg, regex_str, REG_EXTENDED|REG_NOSUB))
			goto out;

		ble->vendor = vendor_str;
	}
	if (product) {
		product_str = strdup(product);
		if (!product_str)
			goto out1;

		regex_str = check_invert(product_str, &ble->product_invert);
		if (regcomp(&ble->product_reg, regex_str, REG_EXTENDED|REG_NOSUB))
			goto out1;

		ble->product = product_str;
	}
	ble->origin = origin;
	return 0;
out1:
	if (vendor) {
		regfree(&ble->vendor_reg);
		ble->vendor = NULL;
	}
out:
	free(vendor_str);
	free(product_str);
	return 1;
}

static int
match_reglist (const struct _vector *blist, const char *str)
{
	int i;
	struct blentry * ble;

	vector_foreach_slot (blist, ble, i) {
		if (!!regexec(&ble->regex, str, 0, NULL, 0) == ble->invert)
			return 1;
	}
	return 0;
}

static int
match_reglist_device (const struct _vector *blist, const char *vendor,
		      const char * product)
{
	int i;
	struct blentry_device * ble;

	vector_foreach_slot (blist, ble, i) {
		if (!ble->vendor && !ble->product)
			continue;
		if ((!ble->vendor ||
		     !!regexec(&ble->vendor_reg, vendor, 0, NULL, 0) ==
		     ble->vendor_invert) &&
		    (!ble->product ||
		     !!regexec(&ble->product_reg, product, 0, NULL, 0) ==
		     ble->product_invert))
			return 1;
	}
	return 0;
}

static int
find_blacklist_device (const struct _vector *blist, const char *vendor,
		       const char *product)
{
	int i;
	struct blentry_device * ble;

	vector_foreach_slot (blist, ble, i) {
		if (((!vendor && !ble->vendor) ||
		     (vendor && ble->vendor &&
		      !strcmp(vendor, ble->vendor))) &&
		    ((!product && !ble->product) ||
		     (product && ble->product &&
		      !strcmp(product, ble->product))))
			return 1;
	}
	return 0;
}

int
setup_default_blist (struct config * conf)
{
	struct blentry * ble;
	struct hwentry *hwe;
	int i;

	if (store_ble(conf->blist_devnode, "!^(sd[a-z]|dasd[a-z]|nvme[0-9])", ORIGIN_DEFAULT))
		return 1;

	if (store_ble(conf->elist_property, "(SCSI_IDENT_|ID_WWN)", ORIGIN_DEFAULT))
		return 1;

	vector_foreach_slot (conf->hwtable, hwe, i) {
		if (hwe->bl_product) {
			if (find_blacklist_device(conf->blist_device,
						  hwe->vendor, hwe->bl_product))
				continue;
			if (alloc_ble_device(conf->blist_device))
				return 1;
			ble = VECTOR_SLOT(conf->blist_device,
					  VECTOR_SIZE(conf->blist_device) - 1);
			if (set_ble_device(conf->blist_device, hwe->vendor, hwe->bl_product,
					   ORIGIN_DEFAULT)) {
				free(ble);
				vector_del_slot(conf->blist_device, VECTOR_SIZE(conf->blist_device) - 1);
				return 1;
			}
		}
	}
	return 0;
}

#define LOG_BLIST(M, S, lvl)						\
	if (vendor && product)						\
		condlog(lvl, "%s: (%s:%s) %s %s",			\
			dev, vendor, product, (M), (S));		\
	else if (wwid && !dev)						\
		condlog(lvl, "%s: %s %s", wwid, (M), (S));		\
	else if (wwid)							\
		condlog(lvl, "%s: %s %s %s", dev, (M), wwid, (S));	\
	else if (env)							\
		condlog(lvl, "%s: %s %s %s", dev, (M), env, (S));	\
	else if (protocol)						\
		condlog(lvl, "%s: %s %s %s", dev, (M), protocol, (S));	\
	else								\
		condlog(lvl, "%s: %s %s", dev, (M), (S))

static void
log_filter (const char *dev, const char *vendor, const char *product,
	    const char *wwid, const char *env, const char *protocol,
	    int r, int lvl)
{
	/*
	 * Try to sort from most likely to least.
	 */
	switch (r) {
	case MATCH_NOTHING:
		break;
	case MATCH_DEVICE_BLIST:
		LOG_BLIST("vendor/product", "blacklisted", lvl);
		break;
	case MATCH_WWID_BLIST:
		LOG_BLIST("wwid", "blacklisted", lvl);
		break;
	case MATCH_DEVNODE_BLIST:
		LOG_BLIST("device node name", "blacklisted", lvl);
		break;
	case MATCH_PROPERTY_BLIST:
		LOG_BLIST("udev property", "blacklisted", lvl);
		break;
	case MATCH_PROTOCOL_BLIST:
		LOG_BLIST("protocol", "blacklisted", lvl);
		break;
	case MATCH_DEVICE_BLIST_EXCEPT:
		LOG_BLIST("vendor/product", "whitelisted", lvl);
		break;
	case MATCH_WWID_BLIST_EXCEPT:
		LOG_BLIST("wwid", "whitelisted", lvl);
		break;
	case MATCH_DEVNODE_BLIST_EXCEPT:
		LOG_BLIST("device node name", "whitelisted", lvl);
		break;
	case MATCH_PROPERTY_BLIST_EXCEPT:
		LOG_BLIST("udev property", "whitelisted", lvl);
		break;
	case MATCH_PROPERTY_BLIST_MISSING:
		LOG_BLIST("blacklisted,", "udev property missing", lvl);
		break;
	case MATCH_PROTOCOL_BLIST_EXCEPT:
		LOG_BLIST("protocol", "whitelisted", lvl);
		break;
	}
}

int
filter_device (const struct _vector *blist, const struct _vector *elist,
	       const char *vendor, const char * product, const char *dev)
{
	int r = MATCH_NOTHING;

	if (vendor && product) {
		if (match_reglist_device(elist, vendor, product))
			r = MATCH_DEVICE_BLIST_EXCEPT;
		else if (match_reglist_device(blist, vendor, product))
			r = MATCH_DEVICE_BLIST;
	}

	log_filter(dev, vendor, product, NULL, NULL, NULL, r, 3);
	return r;
}

int
filter_devnode (const struct _vector *blist, const struct _vector *elist,
		const char *dev)
{
	int r = MATCH_NOTHING;

	if (dev) {
		if (match_reglist(elist, dev))
			r = MATCH_DEVNODE_BLIST_EXCEPT;
		else if (match_reglist(blist, dev))
			r = MATCH_DEVNODE_BLIST;
	}

	log_filter(dev, NULL, NULL, NULL, NULL, NULL, r, 3);
	return r;
}

int
filter_wwid (const struct _vector *blist, const struct _vector *elist,
	     const char *wwid, const char *dev)
{
	int r = MATCH_NOTHING;

	if (wwid) {
		if (match_reglist(elist, wwid))
			r = MATCH_WWID_BLIST_EXCEPT;
		else if (match_reglist(blist, wwid))
			r = MATCH_WWID_BLIST;
	}

	log_filter(dev, NULL, NULL, wwid, NULL, NULL, r, 3);
	return r;
}

int
filter_protocol(const struct _vector *blist, const struct _vector *elist,
		const struct path *pp)
{
	STRBUF_ON_STACK(buf);
	const char *prot;
	int r = MATCH_NOTHING;

	if (pp) {
		snprint_path_protocol(&buf, pp);
		prot = get_strbuf_str(&buf);

		if (match_reglist(elist, prot))
			r = MATCH_PROTOCOL_BLIST_EXCEPT;
		else if (match_reglist(blist, prot))
			r = MATCH_PROTOCOL_BLIST;
		log_filter(pp->dev, NULL, NULL, NULL, NULL, prot, r, 3);
	}

	return r;
}

int
filter_path (const struct config *conf, const struct path *pp)
{
	int r;

	r = filter_property(conf, pp->udev, 3, pp->uid_attribute);
	if (r > 0)
		return r;
	r = filter_devnode(conf->blist_devnode, conf->elist_devnode, pp->dev);
	if (r > 0)
		return r;
	r = filter_device(conf->blist_device, conf->elist_device,
			   pp->vendor_id, pp->product_id, pp->dev);
	if (r > 0)
		return r;
	r = filter_protocol(conf->blist_protocol, conf->elist_protocol, pp);
	if (r > 0)
		return r;
	r = filter_wwid(conf->blist_wwid, conf->elist_wwid, pp->wwid, pp->dev);
	return r;
}

int
filter_property(const struct config *conf, struct udev_device *udev,
		int lvl, const char *uid_attribute)
{
	const char *devname = udev_device_get_sysname(udev);
	struct udev_list_entry *list_entry;
	const char *env = NULL;
	int r = MATCH_NOTHING;

	if (udev) {
		/*
		 * This is the inverse of the 'normal' matching;
		 * the environment variable _has_ to match.
		 * But only if the uid_attribute used for determining the WWID
		 * of the path is is present in the environment
		 * (uid_attr_seen). If this is not the case, udev probably
		 * just failed to access the device, which should not cause the
		 * device to be blacklisted (it won't be used by multipath
		 * anyway without WWID).
		 * Likewise, if no uid attribute is defined, udev-based WWID
		 * determination is effectively off, and devices shouldn't be
		 * blacklisted by missing properties (check_missing_prop).
		 */

		bool check_missing_prop = uid_attribute != NULL &&
			*uid_attribute != '\0';
		bool uid_attr_seen = false;

		r = MATCH_PROPERTY_BLIST_MISSING;
		udev_list_entry_foreach(list_entry,
				udev_device_get_properties_list_entry(udev)) {

			env = udev_list_entry_get_name(list_entry);
			if (!env)
				continue;

			if (check_missing_prop && !strcmp(env, uid_attribute))
				uid_attr_seen = true;

			if (match_reglist(conf->elist_property, env)) {
				r = MATCH_PROPERTY_BLIST_EXCEPT;
				break;
			}
			if (match_reglist(conf->blist_property, env)) {
				r = MATCH_PROPERTY_BLIST;
				break;
			}
			env = NULL;
		}
		if (r == MATCH_PROPERTY_BLIST_MISSING &&
		    (!check_missing_prop || !uid_attr_seen))
			r = MATCH_NOTHING;
	}

	log_filter(devname, NULL, NULL, NULL, env, NULL, r, lvl);
	return r;
}

static void free_ble(struct blentry *ble)
{
	if (!ble)
		return;
	regfree(&ble->regex);
	free(ble->str);
	free(ble);
}

void
free_blacklist (vector blist)
{
	struct blentry * ble;
	int i;

	if (!blist)
		return;

	vector_foreach_slot (blist, ble, i) {
		free_ble(ble);
	}
	vector_free(blist);
}

void merge_blacklist(vector blist)
{
	struct blentry *bl1, *bl2;
	int i, j;

	vector_foreach_slot(blist, bl1, i) {
		j = i + 1;
		vector_foreach_slot_after(blist, bl2, j) {
			if (!bl1->str || !bl2->str || strcmp(bl1->str, bl2->str))
				continue;
			condlog(3, "%s: duplicate blist entry section for %s",
				__func__, bl1->str);
			free_ble(bl2);
			vector_del_slot(blist, j);
			j--;
		}
	}
}

static void free_ble_device(struct blentry_device *ble)
{
	if (ble) {
		if (ble->vendor) {
			regfree(&ble->vendor_reg);
			free(ble->vendor);
		}
		if (ble->product) {
			regfree(&ble->product_reg);
			free(ble->product);
		}
		free(ble);
	}
}

void
free_blacklist_device (vector blist)
{
	struct blentry_device * ble;
	int i;

	if (!blist)
		return;

	vector_foreach_slot (blist, ble, i) {
		free_ble_device(ble);
	}
	vector_free(blist);
}

void merge_blacklist_device(vector blist)
{
	struct blentry_device *bl1, *bl2;
	int i, j;

	vector_foreach_slot(blist, bl1, i) {
		if (!bl1->vendor && !bl1->product) {
			free_ble_device(bl1);
			vector_del_slot(blist, i);
			i--;
		}
	}

	vector_foreach_slot(blist, bl1, i) {
		j = i + 1;
		vector_foreach_slot_after(blist, bl2, j) {
			if ((!bl1->vendor && bl2->vendor) ||
			    (bl1->vendor && !bl2->vendor) ||
			    (bl1->vendor && bl2->vendor &&
			     strcmp(bl1->vendor, bl2->vendor)))
				continue;
			if ((!bl1->product && bl2->product) ||
			    (bl1->product && !bl2->product) ||
			    (bl1->product && bl2->product &&
			     strcmp(bl1->product, bl2->product)))
				continue;
			condlog(3, "%s: duplicate blist entry section for %s:%s",
				__func__, bl1->vendor, bl1->product);
			free_ble_device(bl2);
			vector_del_slot(blist, j);
			j--;
		}
	}
}
