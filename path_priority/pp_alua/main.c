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
#include <sys/types.h>
#include <sys/stat.h>

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#include "rtpg.h"

#define ALUA_PRIO_SUCCESS			0
#define ALUA_PRIO_INVALID_COMMANDLINE		1
#define ALUA_PRIO_OPEN_FAILED			2
#define ALUA_PRIO_NOT_SUPPORTED			3
#define ALUA_PRIO_RTPG_FAILED			4
#define ALUA_PRIO_GETAAS_FAILED			5

#define ALUA_PRIO_MAJOR				0
#define ALUA_PRIO_MINOR				4

#define PRINT_ERROR(f, a...) \
		if (verbose) \
		fprintf(stderr, "ERROR: " f, ##a)
#define PRINT_VERBOSE(f, a...) \
		if (verbose) \
			printf(f, ##a)

char *	devicename	= NULL;
int	verbose		= 0;

char *basename(char *p)
{
	char *r;

	for(r = p; *r != '\0'; r++);
	for(; r > p && *(r - 1) != '/'; r--);

	return r;
}

void
print_help(char *command)
{
	printf("Usage: %s <options> <device> [<device> [...]]\n\n",
		basename(command));
	printf("Options are:\n");

	printf("\t-v\n");
	printf("\t\tTurn on verbose output.\n");

	printf("\t-V\n");
	printf("\t\tPrints the version number and exits.\n");
}

void
print_version(char *command)
{
	printf("(C) Copyright IBM Corp. 2004, 2005  All Rights Reserved.\n");
	printf("This is %s version %u.%u\n",
		basename(command),
		ALUA_PRIO_MAJOR,
		ALUA_PRIO_MINOR
	);
}

int
open_block_device(char *name)
{
	int		fd;
	struct stat	st;

	if (stat(name, &st) != 0) {
		PRINT_ERROR("Cannot get file status from %s (errno = %i)!\n",
			name, errno);
		return -ALUA_PRIO_OPEN_FAILED;
	}
	if (!S_ISBLK(st.st_mode)) {
		PRINT_ERROR("%s is not a block device!\n", name);
		return -ALUA_PRIO_OPEN_FAILED;
	}
	fd = open(name, O_RDONLY);
	if (fd < 0) {
		PRINT_ERROR("Couldn't open %s (errno = %i)!\n", name, errno);
		return -ALUA_PRIO_OPEN_FAILED;
	}
	return fd;
}

int
close_block_device(int fd)
{
	return close(fd);
}

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

	if (verbose) {
		printf("Target port groups are ");
		switch(rc) {
			case TPGS_NONE:
				printf("not");
				break;
			case TPGS_IMPLICIT:
				printf("implicitly");
				break;
			case TPGS_EXPLICIT:
				printf("explicitly");
				break;
			case TPGS_BOTH:
				printf("implicitly and explicitly");
				break;
		}
		printf(" supported.\n");
	}

	if (rc == TPGS_NONE)
		return -ALUA_PRIO_NOT_SUPPORTED;

	tpg = get_target_port_group(fd);
	if (tpg < 0) {
		PRINT_ERROR("Couldn't get target port group!\n");
		return -ALUA_PRIO_RTPG_FAILED;
	}
	PRINT_VERBOSE("Reported target port group is %i", tpg);

	rc = get_asymmetric_access_state(fd, tpg);
	if (rc < 0) {
		PRINT_VERBOSE(" [get AAS failed]\n");
		PRINT_ERROR("Couln't get asymmetric access state!\n");
		return -ALUA_PRIO_GETAAS_FAILED;
	}
	PRINT_VERBOSE(" [%s]\n",
		(aas_string[rc]) ? aas_string[rc] : "invalid/reserved"
	);

	return rc;
}

int
main (int argc, char **argv)
{
	char *		s_opts = "hvV";
	int		fd;
	int		rc;
	int		c;

	while ((c = getopt(argc, argv, s_opts)) >= 0) {
		switch(c) {
			case 'h':
				print_help(argv[0]);
				return ALUA_PRIO_SUCCESS;
			case 'V':
				print_version(argv[0]);
				return ALUA_PRIO_SUCCESS;
			case 'v':
				verbose = 1;
				break;
			case '?':
			case ':':
			default:
				return ALUA_PRIO_INVALID_COMMANDLINE;
		}
	}

	if (optind == argc) {
		print_help(argv[0]);
		printf("\n");
		PRINT_ERROR("No device specified!\n");
		return ALUA_PRIO_INVALID_COMMANDLINE;
	}

	rc = ALUA_PRIO_SUCCESS;
	for(c = optind; c < argc && !rc; c++) {
		fd = open_block_device(argv[c]);
		if (fd < 0) {
			return -fd;
		}
		rc = get_alua_info(fd);
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
			printf("%u\n", rc);
			rc = ALUA_PRIO_SUCCESS;
		}
		close_block_device(fd);
	}

	return -rc;
}
