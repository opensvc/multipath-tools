#include "structs.h"
#include "config.h"

/* Required globals */
struct udev *udev;
int logsink = -1;
struct config conf = {
	.verbosity = 4,
};

struct config *get_multipath_config(void)
{
	return &conf;
}

void put_multipath_config(void *arg)
{}
