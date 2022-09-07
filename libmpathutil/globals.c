#include <stddef.h>
#include <libudev.h>
#include "globals.h"

struct udev __attribute__((weak)) *udev;
struct config __attribute__((weak)) *get_multipath_config(void)
{
	return NULL;
}

void __attribute__((weak)) put_multipath_config(void *p __attribute__((unused)))
{}
