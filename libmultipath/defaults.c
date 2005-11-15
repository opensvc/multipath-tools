/*
 * Copyright (c) 2005 Christophe Varoqui
 */
#include <string.h>

#include "memory.h"

char *
set_default (char * str)
{
	int len;
	char * p;

	len = strlen(str);
	p = MALLOC(len + 1);

	if (!p)
		return NULL;

	strncat(p, str, len);

	return p;
}
