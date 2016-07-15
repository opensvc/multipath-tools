/*
 * sysfs.c
 *
 * Copyright(c) 2016 Hannes Reinecke, SUSE Linux GmbH
 */

#include <stdio.h>

#include "structs.h"
#include "discovery.h"
#include "prio.h"

static const struct {
	unsigned char value;
	char *name;
} sysfs_access_state_map[] = {
	{ 50, "active/optimized" },
	{ 10, "active/non-optimized" },
	{  5, "lba-dependent" },
	{  1, "standby" },
};

int get_exclusive_pref_arg(char *args)
{
	char *ptr;

	if (args == NULL)
		return 0;
	ptr = strstr(args, "exclusive_pref_bit");
	if (!ptr)
		return 0;
	if (ptr[18] != '\0' && ptr[18] != ' ' && ptr[18] != '\t')
		return 0;
	if (ptr != args && ptr[-1] != ' ' && ptr[-1] != '\t')
		return 0;
	return 1;
}

int getprio (struct path * pp, char * args, unsigned int timeout)
{
	int prio = 0, rc, i;
	char buff[512];
	int exclusive_pref;

	exclusive_pref = get_exclusive_pref_arg(args);
	rc = sysfs_get_asymmetric_access_state(pp, buff, 512);
	if (rc < 0)
		return PRIO_UNDEF;
	prio = 0;
	for (i = 0; i < 4; i++) {
		if (!strncmp(buff, sysfs_access_state_map[i].name,
			     strlen(sysfs_access_state_map[i].name))) {
			prio = sysfs_access_state_map[i].value;
			break;
		}
	}
	if (rc > 0 && (prio != 50 || exclusive_pref))
		prio += 80;

	return prio;
}
