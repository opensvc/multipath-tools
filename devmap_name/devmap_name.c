/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <linux/kdev_t.h>
#include <libdevmapper.h>

static void usage(char * progname) {
	fprintf(stderr, "usage : %s [-t target type] dev_t\n", progname);
	fprintf(stderr, "where dev_t is either 'major minor' or 'major:minor'\n");
	exit(1);
}

int dm_target_type(int major, int minor, char *type)
{
	struct dm_task *dmt;
	void *next = NULL;
	uint64_t start, length;
	char *target_type = NULL;
	char *params;
	int r = 1;

	if (!(dmt = dm_task_create(DM_DEVICE_STATUS)))
		return 1;

	if (!dm_task_set_major(dmt, major) ||
	    !dm_task_set_minor(dmt, minor))
		goto bad;

	dm_task_no_open_count(dmt);

	if (!dm_task_run(dmt))
		goto bad;

	if (!type)
		goto good;

	do {
		next = dm_get_next_target(dmt, next, &start, &length,
					  &target_type, &params);
		if (target_type && strcmp(target_type, type))
			goto bad;
	} while (next);

good:
	printf("%s\n", dm_task_get_name(dmt));
	r = 0;
bad:
	dm_task_destroy(dmt);
	return r;
}

int main(int argc, char **argv)
{
	int c;
	int major, minor;
	char *target_type = NULL;

	while ((c = getopt(argc, argv, "t:")) != -1) {
		switch (c) {
		case 't':
			target_type = optarg;
			break;
		default:
			usage(argv[0]);
			return 1;
			break;
		}
	}

	/* sanity check */
	if (optind == argc - 2) {
		major = atoi(argv[argc - 2]);
		minor = atoi(argv[argc - 1]);
	} else if (optind != argc - 1 ||
		   2 != sscanf(argv[argc - 1], "%i:%i", &major, &minor))
		usage(argv[0]);

	if (dm_target_type(major, minor, target_type))
		return 1;
                                                                                
	return 0;
}

