#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "checkers.h"

extern int
get_checker_id (char * str)
{
	if (0 == strncmp(str, "tur", 3))
		return TUR;
	if (0 == strncmp(str, "readsector0", 11))
		return READSECTOR0;
 	if (0 == strncmp(str, "emc_clariion", 12))
		return EMC_CLARIION;
	return -1;
}

extern void * 
get_checker_addr (int id)
{
	int (*checker) (int, char *, void **);

	switch (id) {
	case TUR:
		checker = &tur;
		break;
	case READSECTOR0:
		checker = &readsector0;
		break;
	case EMC_CLARIION:
		checker = &emc_clariion;
		break;
	default:
		checker = NULL;
		break;
	}
	return checker;
}

extern int
get_checker_name (char * str, int id)
{
	char * s;

	switch (id) {
	case TUR:
		s = "tur";
		break;
	case READSECTOR0:
		s = "readsector0";
		break;
	case EMC_CLARIION:
		s = "emc_clariion";
		break;
	default:
		s = "undefined";
		break;
	}
	if (snprintf(str, CHECKER_NAME_SIZE, "%s", s) >= CHECKER_NAME_SIZE) {
		fprintf(stderr, "checker_name too small\n");
		return 1;
	}
	return 0;
}
