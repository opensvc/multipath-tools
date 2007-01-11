/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 */
#include <stdio.h>
#include <checkers.h>

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

	if (!ble || !blist)
		return 1;

	if (!vector_alloc_slot(blist)) {
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

	ble = VECTOR_SLOT(blist, VECTOR_SIZE(blist) - 1);

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

	str = STRDUP("^hd[a-z]");
	if (!str)
		return 1;
	if (store_ble(conf->blist_devnode, str, ORIGIN_DEFAULT))
		return 1;
	
	str = STRDUP("^cciss!c[0-9]d[0-9]*");
	if (!str)
		return 1;
	if (store_ble(conf->blist_devnode, str, ORIGIN_DEFAULT))
		return 1;

	vector_foreach_slot (conf->hwtable, hwe, i) {
		if (hwe->bl_product) {
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

int
blacklist_exceptions (vector elist, char * str)
{
        int i;
        struct blentry * ele;

        vector_foreach_slot (elist, ele, i) {
                if (!regexec(&ele->regex, str, 0, NULL, 0)) {
			condlog(3, "%s: exception-listed", str);
			return 1;
		}
	}
        return 0;
}

int
blacklist (vector blist, vector elist, char * str)
{
	int i;
	struct blentry * ble;

	if (blacklist_exceptions(elist, str))
		return 0;

	vector_foreach_slot (blist, ble, i) {
		if (!regexec(&ble->regex, str, 0, NULL, 0)) {
			condlog(3, "%s: blacklisted", str);
			return 1;
		}
	}
	return 0;
}

int
blacklist_exceptions_device(vector elist, char * vendor, char * product)
{
	int i;
	struct blentry_device * ble;

	vector_foreach_slot (elist, ble, i) {
		if (!regexec(&ble->vendor_reg, vendor, 0, NULL, 0) &&
		    !regexec(&ble->product_reg, product, 0, NULL, 0)) {
			condlog(3, "%s:%s: exception-listed", vendor, product);
			return 1;
		}
	}
	return 0;
}

int
blacklist_device (vector blist, vector elist, char * vendor, char * product)
{
	int i;
	struct blentry_device * ble;

	if (blacklist_exceptions_device(elist, vendor, product))
		return 0;

	vector_foreach_slot (blist, ble, i) {
		if (!regexec(&ble->vendor_reg, vendor, 0, NULL, 0) &&
		    !regexec(&ble->product_reg, product, 0, NULL, 0)) {
			condlog(3, "%s:%s: blacklisted", vendor, product);
			return 1;
		}
	}
	return 0;
}

int
blacklist_path (struct config * conf, struct path * pp)
{
	if (blacklist(conf->blist_devnode, conf->elist_devnode, pp->dev))
		return 1;

	if (blacklist(conf->blist_wwid, conf->elist_wwid, pp->wwid))
		return 1;

	if (pp->vendor_id && pp->product_id &&
	    blacklist_device(conf->blist_device, conf->elist_device, pp->vendor_id, pp->product_id))
		return 1;

	return 0;
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
			regfree(&ble->vendor_reg);
			regfree(&ble->product_reg);
			FREE(ble->vendor);
			FREE(ble->product);
			FREE(ble);
		}
	}
	vector_free(blist);
}
