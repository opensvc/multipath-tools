/*
 * Copyright (c) 2004, 2005 Christophe Varoqui
 */
#include <stdio.h>

#include "checkers.h"
#include "libsg.h"

#define MSG_READSECTOR0_UP	"readsector0 checker reports path is up"
#define MSG_READSECTOR0_DOWN	"readsector0 checker reports path is down"

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

	ret = sg_read(c->fd, &buf[0], 4069, &sbuf[0],
		      SENSE_BUFF_LEN, c->timeout);

	switch (ret)
	{
	case PATH_DOWN:
		MSG(c, MSG_READSECTOR0_DOWN);
		break;
	case PATH_UP:
		MSG(c, MSG_READSECTOR0_UP);
		break;
	default:
		break;
	}
	return ret;
}
