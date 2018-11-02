/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 */
#include <stdio.h>

#include "checkers.h"
#include "libsg.h"

struct readsector0_checker_context {
	void * dummy;
};

int libcheck_init (struct checker * c)
{
	return 0;
}

void libcheck_free (struct checker * c)
{
	return;
}

int libcheck_check (struct checker * c)
{
	unsigned char buf[4096];
	unsigned char sbuf[SENSE_BUFF_LEN];
	int ret;

	ret = sg_read(c->fd, &buf[0], 4096, &sbuf[0],
		      SENSE_BUFF_LEN, c->timeout);

	switch (ret)
	{
	case PATH_DOWN:
		c->msgid = CHECKER_MSGID_DOWN;
		break;
	case PATH_UP:
		c->msgid = CHECKER_MSGID_UP;
		break;
	default:
		break;
	}
	return ret;
}
