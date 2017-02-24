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
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <json.h>

#include "libdmmp/libdmmp.h"
#include "libdmmp_private.h"

#define _DMMP_SHOW_PGS_CMD "show groups raw format %w|%g|%p|%t|%s"
#define _DMMP_SHOW_PG_INDEX_WWID	0
#define _DMMP_SHOW_PG_INDEX_PG_ID	1
#define _DMMP_SHOW_PG_INDEX_PRI		2
#define _DMMP_SHOW_PG_INDEX_STATUS	3
#define _DMMP_SHOW_PG_INDEX_SELECTOR	4

struct dmmp_path_group {
	uint32_t id;
	/* ^ pgindex of struct path, will be used for path group switch */
	uint32_t status;
	uint32_t priority;
	char *selector;
	uint32_t dmmp_p_count;
	struct dmmp_path **dmmp_ps;
};

static const struct _num_str_conv _DMMP_PATH_GROUP_STATUS_CONV[] = {
	{DMMP_PATH_GROUP_STATUS_UNKNOWN, "undef"},
	{DMMP_PATH_GROUP_STATUS_ACTIVE, "active"},
	{DMMP_PATH_GROUP_STATUS_DISABLED, "disabled"},
	{DMMP_PATH_GROUP_STATUS_ENABLED, "enabled"},
};

_dmmp_str_func_gen(dmmp_path_group_status_str, uint32_t, pg_status,
		   _DMMP_PATH_GROUP_STATUS_CONV);
_dmmp_str_conv_func_gen(_dmmp_path_group_status_str_conv, ctx, pg_status_str,
			uint32_t, DMMP_PATH_GROUP_STATUS_UNKNOWN,
			_DMMP_PATH_GROUP_STATUS_CONV);

_dmmp_getter_func_gen(dmmp_path_group_id_get, struct dmmp_path_group, dmmp_pg,
		      id, uint32_t);
_dmmp_getter_func_gen(dmmp_path_group_status_get, struct dmmp_path_group,
		      dmmp_pg, status, uint32_t);
_dmmp_getter_func_gen(dmmp_path_group_priority_get, struct dmmp_path_group,
		      dmmp_pg, priority, uint32_t);
_dmmp_getter_func_gen(dmmp_path_group_selector_get, struct dmmp_path_group,
		      dmmp_pg, selector, const char *);
_dmmp_array_free_func_gen(_dmmp_path_group_array_free, struct dmmp_path_group,
			  _dmmp_path_group_free);


struct dmmp_path_group *_dmmp_path_group_new(void)
{
	struct dmmp_path_group *dmmp_pg = NULL;

	dmmp_pg = (struct dmmp_path_group *)
		malloc(sizeof(struct dmmp_path_group));

	if (dmmp_pg != NULL) {
		dmmp_pg->id = _DMMP_PATH_GROUP_ID_UNKNOWN;
		dmmp_pg->status = DMMP_PATH_GROUP_STATUS_UNKNOWN;
		dmmp_pg->priority = 0;
		dmmp_pg->selector = NULL;
		dmmp_pg->dmmp_p_count = 0;
		dmmp_pg->dmmp_ps = NULL;
	}
	return dmmp_pg;
}
int _dmmp_path_group_update(struct dmmp_context *ctx,
			    struct dmmp_path_group *dmmp_pg,
			    json_object *j_obj_pg)
{
	int rc = DMMP_OK;
	uint32_t id = 0;
	int priority_int = -1 ;
	const char *status_str = NULL;
	const char *selector = NULL;
	struct array_list *ar_ps = NULL;
	int ar_ps_len = -1;
	uint32_t i = 0;
	struct dmmp_path *dmmp_p = NULL;

	assert(ctx != NULL);
	assert(dmmp_pg != NULL);
	assert(j_obj_pg != NULL);

	_json_obj_get_value(ctx, j_obj_pg, status_str, "dm_st",
			    json_type_string, json_object_get_string, rc, out);

	_json_obj_get_value(ctx, j_obj_pg, selector, "selector",
			    json_type_string, json_object_get_string, rc, out);

	_json_obj_get_value(ctx, j_obj_pg, priority_int, "pri",
			    json_type_int, json_object_get_int, rc, out);

	_json_obj_get_value(ctx, j_obj_pg, id, "group",
			    json_type_int, json_object_get_int, rc, out);

	dmmp_pg->priority = (priority_int <= 0) ? 0 : priority_int & UINT32_MAX;

	_dmmp_null_or_empty_str_check(ctx, status_str, rc, out);
	_dmmp_null_or_empty_str_check(ctx, selector, rc, out);

	dmmp_pg->selector = strdup(selector);
	_dmmp_alloc_null_check(ctx, dmmp_pg->selector, rc, out);

	dmmp_pg->id = id;

	if (dmmp_pg->id == _DMMP_PATH_GROUP_ID_UNKNOWN) {
		rc = DMMP_ERR_BUG;
		_error(ctx, "BUG: Got unknown(%d) path group ID",
		       _DMMP_PATH_GROUP_ID_UNKNOWN);
		goto out;
	}

	dmmp_pg->status = _dmmp_path_group_status_str_conv(ctx, status_str);

	_json_obj_get_value(ctx, j_obj_pg, ar_ps, "paths",
			    json_type_array, json_object_get_array, rc, out);

	ar_ps_len = array_list_length(ar_ps);
	if (ar_ps_len < 0) {
		rc = DMMP_ERR_BUG;
		_error(ctx, "BUG: Got negative length for ar_ps");
		goto out;
	}
	else if (ar_ps_len == 0)
		goto out;
	else
		dmmp_pg->dmmp_p_count = ar_ps_len & UINT32_MAX;

	dmmp_pg->dmmp_ps = (struct dmmp_path **)
		malloc(sizeof(struct dmmp_path *) * dmmp_pg->dmmp_p_count);
	_dmmp_alloc_null_check(ctx, dmmp_pg->dmmp_ps, rc, out);
	for (; i < dmmp_pg->dmmp_p_count; ++i)
		dmmp_pg->dmmp_ps[i] = NULL;

	for (i = 0; i < dmmp_pg->dmmp_p_count; ++i) {
		dmmp_p = _dmmp_path_new();
		_dmmp_alloc_null_check(ctx, dmmp_p, rc, out);
		dmmp_pg->dmmp_ps[i] = dmmp_p;
		_good(_dmmp_path_update(ctx, dmmp_p,
					array_list_get_idx(ar_ps, i)),
		      rc, out);
	}

	_debug(ctx, "Got path group id: %" PRIu32 "", dmmp_pg->id);
	_debug(ctx, "Got path group priority: %" PRIu32 "", dmmp_pg->priority);
	_debug(ctx, "Got path group status: %s(%" PRIu32 ")",
	       dmmp_path_group_status_str(dmmp_pg->status), dmmp_pg->status);
	_debug(ctx, "Got path group selector: '%s'", dmmp_pg->selector);

out:
	if (rc != DMMP_OK)
		_dmmp_path_group_free(dmmp_pg);
	return rc;
}

void _dmmp_path_group_free(struct dmmp_path_group *dmmp_pg)
{
	uint32_t i = 0;

	if (dmmp_pg == NULL)
		return;

	free((char *) dmmp_pg->selector);

	if (dmmp_pg->dmmp_ps != NULL) {
		for (i = 0; i < dmmp_pg->dmmp_p_count; ++i) {
			_dmmp_path_free(dmmp_pg->dmmp_ps[i]);
		}
		free(dmmp_pg->dmmp_ps);
	}
	free(dmmp_pg);
}

void dmmp_path_array_get(struct dmmp_path_group *mp_pg,
			 struct dmmp_path ***mp_paths,
			 uint32_t *dmmp_p_count)
{
	assert(mp_pg != NULL);
	assert(mp_paths != NULL);
	assert(dmmp_p_count != NULL);

	*mp_paths = mp_pg->dmmp_ps;
	*dmmp_p_count = mp_pg->dmmp_p_count;
}
