#include "structs.h"
#include "config.h"

/* Required globals */
struct udev *udev;
int logsink = 0;
struct config conf = {
	.uid_attrs = "sd:ID_BOGUS",
};

struct config *get_multipath_config(void)
{
	return &conf;
}

void put_multipath_config(struct config* c)
{}
