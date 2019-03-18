/*
 * (C) Copyright IBM Corp. 2004, 2005   All Rights Reserved.
 *
 * main.c
 *
 * Tool to make use of a SCSI-feature called Asymmetric Logical Unit Access.
 * It determines the ALUA state of a device and prints a priority value to
 * stdout.
 *
 * Author(s): Jan Kunigk
 *            S. Bader <shbader@de.ibm.com>
 *
 * This file is released under the GPL.
 */
#include <stdio.h>

#include "debug.h"
#include "prio.h"
#include "structs.h"

#include "alua.h"

#define ALUA_PRIO_NOT_SUPPORTED			1
#define ALUA_PRIO_RTPG_FAILED			2
#define ALUA_PRIO_GETAAS_FAILED			3
#define ALUA_PRIO_TPGS_FAILED			4
#define ALUA_PRIO_NO_INFORMATION		5

static const char * aas_string[] = {
	[AAS_OPTIMIZED]		= "active/optimized",
	[AAS_NON_OPTIMIZED]	= "active/non-optimized",
	[AAS_STANDBY]		= "standby",
	[AAS_UNAVAILABLE]	= "unavailable",
	[AAS_LBA_DEPENDENT]	= "logical block dependent",
	[AAS_RESERVED]		= "ARRAY BUG: invalid TPGs state!",
	[AAS_OFFLINE]		= "offline",
	[AAS_TRANSITIONING]	= "transitioning between states",
};

static const char *aas_print_string(int rc)
{
	rc &= 0x7f;

	if (rc & 0x70)
		return aas_string[AAS_RESERVED];
	rc &= 0x0f;
	if (rc > AAS_RESERVED && rc < AAS_OFFLINE)
		return aas_string[AAS_RESERVED];
	else
		return aas_string[rc];
}

int
get_alua_info(struct path * pp, unsigned int timeout)
{
	int	rc;
	int	tpg;

	tpg = get_target_port_group(pp, timeout);
	if (tpg < 0) {
		rc = get_target_port_group_support(pp, timeout);
		if (rc < 0)
			return -ALUA_PRIO_TPGS_FAILED;
		if (rc == TPGS_NONE)
			return -ALUA_PRIO_NOT_SUPPORTED;
		return -ALUA_PRIO_RTPG_FAILED;
	}
	condlog(3, "%s: reported target port group is %i", pp->dev, tpg);
	rc = get_asymmetric_access_state(pp, tpg, timeout);
	if (rc < 0) {
		condlog(2, "%s: get_asymmetric_access_state returned %d",
			__func__, rc);
		return -ALUA_PRIO_GETAAS_FAILED;
	}

	condlog(3, "%s: aas = %02x [%s]%s", pp->dev, rc, aas_print_string(rc),
		(rc & 0x80) ? " [preferred]" : "");
	return rc;
}

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
	int rc;
	int aas;
	int priopath;
	int exclusive_pref;

	if (pp->fd < 0)
		return -ALUA_PRIO_NO_INFORMATION;

	exclusive_pref = get_exclusive_pref_arg(args);
	rc = get_alua_info(pp, timeout);
	if (rc >= 0) {
		aas = (rc & 0x0f);
		priopath = (rc & 0x80);
		switch(aas) {
		case AAS_OPTIMIZED:
			rc = 50;
			break;
		case AAS_NON_OPTIMIZED:
			rc = 10;
			break;
		case AAS_LBA_DEPENDENT:
			rc = 5;
			break;
		case AAS_STANDBY:
			rc = 1;
			break;
		default:
			rc = 0;
		}
		if (priopath && (aas != AAS_OPTIMIZED || exclusive_pref))
			rc += 80;
	} else {
		switch(-rc) {
		case ALUA_PRIO_NOT_SUPPORTED:
			condlog(0, "%s: alua not supported", pp->dev);
			break;
		case ALUA_PRIO_RTPG_FAILED:
			condlog(0, "%s: couldn't get target port group", pp->dev);
			break;
		case ALUA_PRIO_GETAAS_FAILED:
			condlog(0, "%s: couldn't get asymmetric access state", pp->dev);
			break;
		case ALUA_PRIO_TPGS_FAILED:
			condlog(3, "%s: couldn't get supported alua states", pp->dev);
			break;
		}
	}
	return rc;
}
