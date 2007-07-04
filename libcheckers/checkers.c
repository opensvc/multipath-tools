#include <stdio.h>
#include <string.h>

#include "checkers.h"

#include "directio.h"
#include "tur.h"
#include "hp_sw.h"
#include "emc_clariion.h"
#include "rdac.h"
#include "readsector0.h"

static struct checker checkers[] = {
	{
		.fd         = 0,
		.sync       = 1,
		.name       = DIRECTIO,
		.message    = "",
		.context    = NULL,
		.check      = directio,
		.init       = directio_init,
		.free       = directio_free
	},
	{
		.fd         = 0,
		.sync       = 1,
		.name       = TUR,
		.message    = "",
		.context    = NULL,
		.check      = tur,
		.init       = tur_init,
		.free       = tur_free
	},
	{
		.fd         = 0,
		.sync       = 1,
		.name       = HP_SW,
		.message    = "",
		.context    = NULL,
		.check      = hp_sw,
		.init       = hp_sw_init,
		.free       = hp_sw_free
	},
	{
		.fd         = 0,
		.sync       = 1,
		.name       = EMC_CLARIION,
		.message    = "",
		.context    = NULL,
		.check      = emc_clariion,
		.init       = emc_clariion_init,
		.free       = emc_clariion_free
	},
	{
		.fd         = 0,
		.sync       = 1,
		.name       = RDAC,
		.message    = "",
		.context    = NULL,
		.check      = rdac,
		.init       = rdac_init,
		.free       = rdac_free
	},
	{
		.fd         = 0,
		.sync       = 1,
		.name       = READSECTOR0,
		.message    = "",
		.context    = NULL,
		.check      = readsector0,
		.init       = readsector0_init,
		.free       = readsector0_free
	},
	{0, 1, "", "", NULL, NULL, NULL, NULL},
};

void checker_set_fd (struct checker * c, int fd)
{
	c->fd = fd;
}

void checker_set_sync (struct checker * c)
{
	c->sync = 1;
}

void checker_set_async (struct checker * c)
{
	c->sync = 0;
}

struct checker * checker_lookup (char * name)
{
	struct checker * c = &checkers[0];
	
	while (c->check) {
		if (!strncmp(name, c->name, CHECKER_NAME_LEN))
			return c;
		c++;
	}
	return NULL;
}

int checker_init (struct checker * c, void ** mpctxt_addr)
{
	c->mpcontext = mpctxt_addr;
	return c->init(c);
}

void checker_put (struct checker * c)
{
	if (c->free)
		c->free(c);
	memset(c, 0x0, sizeof(struct checker));
}

int checker_check (struct checker * c)
{
	int r;

	if (c->fd <= 0) {
		MSG(c, "no usable fd");
		return PATH_WILD;
	}
	r = c->check(c);

	return r;
}

int checker_selected (struct checker * c)
{
	return (c->check) ? 1 : 0;
}

char * checker_name (struct checker * c)
{
	return c->name;
}

char * checker_message (struct checker * c)
{
	return c->message;
}

struct checker * checker_default (void)
{
	return checker_lookup(DEFAULT_CHECKER);
}

void checker_get (struct checker * dst, struct checker * src)
{
	dst->fd = src->fd;
	dst->sync = src->sync;
	strncpy(dst->name, src->name, CHECKER_NAME_LEN);
	strncpy(dst->message, src->message, CHECKER_MSG_LEN);
	dst->check = src->check;
	dst->init = src->init;
	dst->free = src->free;
}
