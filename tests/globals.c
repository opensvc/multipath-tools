#include <stdlib.h>
#include <string.h>

#include "defaults.h"
#include "structs.h"
#include "config.h"
#include "debug.h"

struct config conf;

struct config *get_multipath_config(void)
{
	return &conf;
}

void put_multipath_config(void *arg)
{}

static __attribute__((unused)) void init_test_verbosity(int test_verbosity)
{
	char *verb = getenv("MPATHTEST_VERBOSITY");

	libmp_verbosity = test_verbosity >= 0 ? test_verbosity :
		DEFAULT_VERBOSITY;
	if (verb && *verb) {
		char *c;
		int vb;

		vb = strtoul(verb, &c, 10);
		if (!*c && vb >= 0 && vb <= 5)
			libmp_verbosity = vb;
	}
}
