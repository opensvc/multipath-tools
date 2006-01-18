/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 */
#include <stdio.h>

#include "memory.h"
#include "vector.h"
#include "util.h"
#include "debug.h"
#include "regex.h"
#include "structs.h"
#include "config.h"
#include "blacklist.h"

struct blentry {
	char * str;
	regex_t regex;
};

struct blentry_device {
	char * vendor;
	char * product;
	regex_t vendor_reg;
	regex_t product_reg;
};

extern int
store_ble (vector blist, char * str)
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
set_ble_device (vector blist, char * vendor, char * product)
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
	return 0;
}

int
setup_default_blist (vector blist)
{
	int r = 0;

	r += store_ble(blist, "^(ram|raw|loop|fd|md|dm-|sr|scd|st)[0-9]*");
	r += store_ble(blist, "^hd[a-z]");
	r += store_ble(blist, "^cciss!c[0-9]d[0-9]*");

	return r;
}

int
blacklist (vector blist, char * str)
{
	int i;
	struct blentry * ble;

	vector_foreach_slot (blist, ble, i) {
		if (!regexec(&ble->regex, str, 0, NULL, 0)) {
			condlog(3, "%s blacklisted", str);
			return 1;
		}
	}
	return 0;
}

int
blacklist_device (vector blist, char * vendor, char * product)
{
	int i;
	struct blentry_device * ble;

	vector_foreach_slot (blist, ble, i) {
		if (!regexec(&ble->vendor_reg, vendor, 0, NULL, 0) &&
		    !regexec(&ble->product_reg, product, 0, NULL, 0)) {
			condlog(3, "%s:%s blacklisted", vendor, product);
			return 1;
		}
	}
	return 0;
}

int
blacklist_path (struct config * conf, struct path * pp)
{
	if (blacklist(conf->blist_devnode, pp->dev))
		return 1;

	if (blacklist(conf->blist_wwid, pp->wwid))
		return 1;

	if (pp->vendor_id && pp->product_id &&
	    blacklist_device(conf->blist_device, pp->vendor_id, pp->product_id))
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
			//regfree(ble->regex);
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
			FREE(ble->vendor);
			FREE(ble->product);
			FREE(ble);
		}
	}
	vector_free(blist);
}
