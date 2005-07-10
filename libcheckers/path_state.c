#include <stdio.h>
#include "path_state.h"

int
pstate_snprintf (char * str, int len, int state)
{
	switch (state) {
	case PATH_UP:
		return snprintf(str, len, "ready");

       	case PATH_DOWN:
		return snprintf(str, len, "faulty");

       	case PATH_GHOST:
		return snprintf(str, len, "ghost");

       	case PATH_SHAKY:
		return snprintf(str, len, "shaky");

       	default:
		break;
       	}
	return 0;
}
