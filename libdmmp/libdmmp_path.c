/*
 * Copyright (C) 2015 - 2016 Red Hat, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Gris Ge <fge@redhat.com>
 *         Todd Gill <tgill@redhat.com>
 */

#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <json.h>

#include "libdmmp/libdmmp.h"
#include "libdmmp_private.h"

#define _DMMP_SHOW_PS_INDEX_BLK_NAME	0
#define _DMMP_SHOW_PS_INDEX_SATAUS	1
#define _DMMP_SHOW_PS_INDEX_WWID	2
#define _DMMP_SHOW_PS_INDEX_PGID	3

struct dmmp_path {
	char *blk_name;
	uint32_t status;
};

static const struct _num_str_conv _DMMP_PATH_STATUS_CONV[] = {
	{DMMP_PATH_STATUS_UNKNOWN, "undef"},
	{DMMP_PATH_STATUS_UP, "ready"},
	{DMMP_PATH_STATUS_DOWN, "faulty"},
	{DMMP_PATH_STATUS_SHAKY, "shaky"},
	{DMMP_PATH_STATUS_GHOST, "ghost"},
	{DMMP_PATH_STATUS_PENDING, "i/o pending"},
	{DMMP_PATH_STATUS_TIMEOUT, "i/o timeout"},
	{DMMP_PATH_STATUS_DELAYED, "delayed"},
};

_dmmp_str_func_gen(dmmp_path_status_str, uint32_t, path_status,
		   _DMMP_PATH_STATUS_CONV);
_dmmp_str_conv_func_gen(_dmmp_path_status_str_conv, ctx, path_status_str,
			uint32_t, DMMP_PATH_STATUS_UNKNOWN,
			_DMMP_PATH_STATUS_CONV);

_dmmp_getter_func_gen(dmmp_path_blk_name_get, struct dmmp_path, dmmp_p,
		      blk_name, const char *);
_dmmp_getter_func_gen(dmmp_path_status_get, struct dmmp_path, dmmp_p,
		      status, uint32_t);

struct dmmp_path *_dmmp_path_new(void)
{
	struct dmmp_path *dmmp_p = NULL;

	dmmp_p = (struct dmmp_path *) malloc(sizeof(struct dmmp_path));

	if (dmmp_p != NULL) {
		dmmp_p->blk_name = NULL;
		dmmp_p->status = DMMP_PATH_STATUS_UNKNOWN;
	}
	return dmmp_p;
}

int _dmmp_path_update(struct dmmp_context *ctx, struct dmmp_path *dmmp_p,
		      json_object *j_obj_p)
{
	int rc = DMMP_OK;
	const char *blk_name = NULL;
	const char *status_str = NULL;

	assert(ctx != NULL);
	assert(dmmp_p != NULL);
	assert(j_obj_p != NULL);

	_json_obj_get_value(ctx, j_obj_p, blk_name, "dev",
			    json_type_string, json_object_get_string, rc, out);
	_json_obj_get_value(ctx, j_obj_p, status_str, "chk_st",
			    json_type_string, json_object_get_string, rc, out);

	_dmmp_null_or_empty_str_check(ctx, blk_name, rc, out);
	_dmmp_null_or_empty_str_check(ctx, status_str, rc, out);

	dmmp_p->blk_name = strdup(blk_name);
	_dmmp_alloc_null_check(ctx, dmmp_p->blk_name, rc, out);

	dmmp_p->status = _dmmp_path_status_str_conv(ctx, status_str);

	_debug(ctx, "Got path blk_name: '%s'", dmmp_p->blk_name);
	_debug(ctx, "Got path status: %s(%" PRIu32 ")",
	       dmmp_path_status_str(dmmp_p->status), dmmp_p->status);

out:
	if (rc != DMMP_OK)
		_dmmp_path_free(dmmp_p);
	return rc;
}

void _dmmp_path_free(struct dmmp_path *dmmp_p)
{
	if (dmmp_p == NULL)
		return;
	free(dmmp_p->blk_name);
	free(dmmp_p);
}
