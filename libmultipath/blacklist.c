#include <stdio.h>

#include "memory.h"
#include "vector.h"
#include "util.h"
#include "debug.h"
#include "regex.h"
#include "blacklist.h"

static int
store_ble (vector blist, char * str)
{
	struct blentry * ble;
	
	if (!str)
		return 0;

	ble = (struct blentry *)MALLOC(sizeof(struct blentry));

	if (!ble)
		goto out;

	ble->preg = MALLOC(sizeof(regex_t));

	if (!ble->preg)
		goto out1;

	ble->str = (char *)MALLOC(strlen(str) + 1);

	if (!ble->str)
		goto out2;

	strcpy(ble->str, str);

	if (regcomp((regex_t *)ble->preg, ble->str, REG_EXTENDED|REG_NOSUB))
		goto out3;

	if (!vector_alloc_slot(blist))
		goto out3;

	vector_set_slot(blist, ble);
	return 0;
out3:
	FREE(ble->str);
out2:
	FREE(ble->preg);
out1:
	FREE(ble);
out:
	return 1;
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
blacklist (vector blist, char * dev)
{
	int i;
	struct blentry *ble;

	vector_foreach_slot (blist, ble, i) {
		if (!regexec(ble->preg, dev, 0, NULL, 0)) {
			condlog(3, "%s blacklisted", dev);
			return 1;
		}
	}
	return 0;
}

int
store_regex (vector blist, char * regex)
{
	if (!blist)
		return 1;

	if (!regex)
		return 1;

	return store_ble(blist, regex);
}	

void
free_blacklist (vector blist)
{
	struct blentry * ble;
	int i;

	if (!blist)
		return;

	vector_foreach_slot (blist, ble, i) {
		if (ble->str)
			FREE(ble->str);

		if (ble->preg)
			FREE(ble->preg);

		FREE(ble);
	}
	vector_free(blist);
}
