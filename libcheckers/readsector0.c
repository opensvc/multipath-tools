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

int readsector0_init (struct checker * c)
{
	return 0;
}

void readsector0_free (struct checker * c)
{
	return;
}

extern int
readsector0 (struct checker * c)
{
	unsigned char buf[512];
	unsigned char sbuf[SENSE_BUFF_LEN];
	int ret;

	ret = sg_read(c->fd, &buf[0], &sbuf[0]);

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
