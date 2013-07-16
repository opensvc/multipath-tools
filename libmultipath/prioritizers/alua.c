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

#include <debug.h>
#include <prio.h>
#include <structs.h>

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
	[AAS_LBA_DEPENDENT]	= "lba dependent",
	[AAS_RESERVED]		= "invalid/reserved",
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
get_alua_info(int fd)
{
	int	rc;
	int	tpg;
	int	aas;

	rc = get_target_port_group_support(fd);
	if (rc < 0)
		return -ALUA_PRIO_TPGS_FAILED;

	if (rc == TPGS_NONE)
		return -ALUA_PRIO_NOT_SUPPORTED;

	tpg = get_target_port_group(fd);
	if (tpg < 0)
		return -ALUA_PRIO_RTPG_FAILED;

	condlog(3, "reported target port group is %i", tpg);
	rc = get_asymmetric_access_state(fd, tpg);
	if (rc < 0)
		return -ALUA_PRIO_GETAAS_FAILED;
	aas = (rc & 0x0f);

	condlog(3, "aas = %02x [%s]%s", rc, aas_print_string(rc),
		(rc & 0x80) ? " [preferred]" : "");
	return rc;
}

int getprio (struct path * pp, char * args)
{
	int rc;
	int aas;
	int priopath;

	if (pp->fd < 0)
		return -ALUA_PRIO_NO_INFORMATION;

	rc = get_alua_info(pp->fd);
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
		if (priopath && aas != AAS_OPTIMIZED)
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
				condlog(0, "%s: couln't get asymmetric access state", pp->dev);
				break;
			case ALUA_PRIO_TPGS_FAILED:
				condlog(3, "%s: couln't get supported alua states", pp->dev);
				break;
		}
	}
	return rc;
}
