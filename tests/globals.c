#include "structs.h"
#include "config.h"
#include "debug.h"

/* Required globals */
struct udev *udev;
int logsink = LOGSINK_STDERR_WITHOUT_TIME;
struct config conf = {
	.verbosity = 4,
};

struct config *get_multipath_config(void)
{
	return &conf;
}

void put_multipath_config(void *arg)
{}
