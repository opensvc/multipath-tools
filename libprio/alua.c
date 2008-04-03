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

#include "libprio.h"
#include "alua.h"

#define ALUA_PRIO_NOT_SUPPORTED			1
#define ALUA_PRIO_RTPG_FAILED			2
#define ALUA_PRIO_GETAAS_FAILED			3

int
get_alua_info(int fd)
{
	char *	aas_string[] = {
		[AAS_OPTIMIZED]		= "active/optimized",
		[AAS_NON_OPTIMIZED]	= "active/non-optimized",
		[AAS_STANDBY]		= "standby",
		[AAS_UNAVAILABLE]	= "unavailable",
		[AAS_TRANSITIONING]	= "transitioning between states",
	};
	int	rc;
	int	tpg;

	rc = get_target_port_group_support(fd);
	if (rc < 0)
		return rc;

	if (rc == TPGS_NONE)
		return -ALUA_PRIO_NOT_SUPPORTED;

	tpg = get_target_port_group(fd);
	if (tpg < 0)
		return -ALUA_PRIO_RTPG_FAILED;

	condlog(3, "reported target port group is %i", tpg);
	rc = get_asymmetric_access_state(fd, tpg);
	if (rc < 0)
		return -ALUA_PRIO_GETAAS_FAILED;

	condlog(3, "aas = [%s]",
		(aas_string[rc]) ? aas_string[rc] : "invalid/reserved");
	return rc;
}

int getprio (struct path * pp)
{
	int rc = get_alua_info(pp->fd);
	if (rc >= 0) {
		switch(rc) {
			case AAS_OPTIMIZED:
				rc = 50;
				break;
			case AAS_NON_OPTIMIZED:
				rc = 10;
				break;
			case AAS_STANDBY:
				rc = 1;
				break;
			default:
				rc = 0;
		}
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
		}
	}
	return rc;
}
