#include <stddef.h>
#include "mt-udev-wrap.h"
#include "globals.h"

__attribute__((weak)) struct config *get_multipath_config(void)
{
	return NULL;
}

__attribute__((weak)) void put_multipath_config(void *p __attribute__((unused)))
{}
