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

#include "prio.h"
#include "weightedpath.h"
#include "config.h"
#include "structs.h"
#include "debug.h"
#include <regex.h>
#include "structs_vec.h"
#include "print.h"
#include "util.h"
#include "strbuf.h"

static int
build_serial_path(struct path *pp, struct strbuf *buf)
{
	int rc = snprint_path_serial(buf, pp);

	return rc < 0 ? rc : 0;
}

static int
build_wwn_path(struct path *pp, struct strbuf *buf)
{
	int rc;

	if ((rc = snprint_host_wwnn(buf, pp)) < 0 ||
	    (rc = fill_strbuf(buf, ':', 1)) < 0 ||
	    (rc = snprint_host_wwpn(buf, pp)) < 0 ||
	    (rc = fill_strbuf(buf, ':', 1)) < 0 ||
	    (rc = snprint_tgt_wwnn(buf, pp) < 0) ||
	    (rc = fill_strbuf(buf, ':', 1) < 0) ||
	    (rc = snprint_tgt_wwpn(buf, pp) < 0))
		return rc;
	return 0;
}

/* main priority routine */
int prio_path_weight(struct path *pp, char *prio_args)
{
	STRBUF_ON_STACK(path);
	char *arg __attribute__((cleanup(cleanup_charp))) = NULL;
	char *temp, *regex, *prio;
	char split_char[] = " \t";
	int priority = DEFAULT_PRIORITY, path_found = 0;
	regex_t pathe;

	/* Return default priority if there is no argument */
	if (!prio_args)
		return priority;

	arg = strdup(prio_args);
	temp = arg;

	regex = get_next_string(&temp, split_char);

	/* Return default priority if the argument is not parseable */
	if (!regex) {
		return priority;
	}

	if (!strcmp(regex, HBTL)) {
		if (print_strbuf(&path, "%d:%d:%d:%" PRIu64, pp->sg_id.host_no,
				 pp->sg_id.channel, pp->sg_id.scsi_id,
				 pp->sg_id.lun) < 0)
			return priority;
	} else if (!strcmp(regex, DEV_NAME)) {
		if (append_strbuf_str(&path, pp->dev) < 0)
			return priority;
	} else if (!strcmp(regex, SERIAL)) {
		if (build_serial_path(pp, &path) != 0)
			return priority;
	} else if (!strcmp(regex, WWN)) {
		if (build_wwn_path(pp, &path) != 0)
			return priority;
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
			if (!regexec(&pathe, get_strbuf_str(&path), 0,
				     NULL, 0)) {
				path_found = 1;
				priority = atoi(prio);
			}
			regfree(&pathe);
		}
	}

	return priority;
}

int getprio(struct path *pp, char *args)
{
	return prio_path_weight(pp, args);
}
