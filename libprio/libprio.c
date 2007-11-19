#include <stdio.h>
#include <string.h>

#include "libprio.h"

static struct prio prioritizers[] = {
	{
		.name    = PRIO_CONST,
		.getprio = prio_const
	},
	{
		.name    = PRIO_RANDOM,
		.getprio = prio_random
	},
	{
		.name    = PRIO_ALUA,
		.getprio = prio_alua
	},
	{
		.name    = PRIO_EMC,
		.getprio = prio_emc
	},
	{
		.name    = PRIO_RDAC,
		.getprio = prio_rdac
	},
	{
		.name    = PRIO_NETAPP,
		.getprio = prio_netapp
	},
	{
		.name    = PRIO_HDS,
		.getprio = prio_hds
	},
	{
		.name    = PRIO_HP_SW,
		.getprio = prio_hp_sw
	},
	{
		.name    = "",
		.getprio = NULL
	},
};

struct prio * prio_lookup (char * name)
{
	struct prio * p = &prioritizers[0];
	
	while (p->getprio) {
		if (!strncmp(name, p->name, PRIO_NAME_LEN))
			return p;
		p++;
	}
	return prio_default();
}

int prio_getprio (struct prio * p, struct path * pp)
{
	return p->getprio(pp);
}

char * prio_name (struct prio * p)
{
	return p->name;
}

struct prio * prio_default (void)
{
	return prio_lookup(DEFAULT_PRIO);
}
