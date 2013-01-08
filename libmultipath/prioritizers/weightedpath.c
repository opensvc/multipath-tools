/*
 *
 *  (C)  Copyright 2008 Hewlett-Packard Development Company, L.P
 *
 *  This file is released under the GPL
 */

/*
 * Prioritizer for device mapper multipath, where specific paths and the
 * corresponding priority values are provided as arguments.
 *
 * This prioritizer assigns the priority value provided in the configuration
 * file based on the comparison made between the specified paths and the path
 * instance for which this is called.
 * Paths can be specified as a regular expression of devname of the path or
 * as hbtl information of the path.
 *
 * Examples:
 *	prio            "weightedpath hbtl 1:.:.:. 2 4:.:.:. 4"
 *	prio            "weightedpath devname sda 10 sde 20"
 *
 * Returns zero as the default priority.
 */

#include <stdio.h>
#include <string.h>

#include <prio.h>
#include "weightedpath.h"
#include <config.h>
#include <structs.h>
#include <memory.h>
#include <debug.h>
#include <regex.h>

char *get_next_string(char **temp, char *split_char)
{
	char *token = NULL;
	token = strsep(temp, split_char);
	while (token != NULL && !strcmp(token, ""))
		token = strsep(temp, split_char);
	return token;
}

/* main priority routine */
int prio_path_weight(struct path *pp, char *prio_args)
{
	char path[FILE_NAME_SIZE];
	char *arg;
	char *temp, *regex, *prio;
	char split_char[] = " \t";
	int priority = DEFAULT_PRIORITY, path_found = 0;
	regex_t pathe;

	/* Return default priority if there is no argument */
	if (!prio_args)
		return priority;

	arg = temp = STRDUP(prio_args);

	regex = get_next_string(&temp, split_char);

	/* Return default priority if the argument is not parseable */
	if (!regex)
		return priority;

	if (!strcmp(regex, HBTL)) {
		sprintf(path, "%d:%d:%d:%d", pp->sg_id.host_no,
			pp->sg_id.channel, pp->sg_id.scsi_id, pp->sg_id.lun);
	} else if (!strcmp(regex, DEV_NAME)) {
		strcpy(path, pp->dev);
	} else {
		condlog(0, "%s: %s - Invalid arguments", pp->dev,
			pp->prio.name);
		return priority;
	}

	while (!path_found) {
		if (!temp)
			break;
		if (!(regex = get_next_string(&temp, split_char)))
			break;
		if (!(prio = get_next_string(&temp, split_char)))
			break;

		if (!regcomp(&pathe, regex, REG_EXTENDED|REG_NOSUB)) {
			if (!regexec(&pathe, path, 0, NULL, 0)) {
				path_found = 1;
				priority = atoi(prio);
			}
			regfree(&pathe);
		}
	}

	FREE(arg);
	return priority;
}

int getprio(struct path *pp, char *args)
{
	return prio_path_weight(pp, args);
}

