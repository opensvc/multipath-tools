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
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <json.h>

#include "libdmmp/libdmmp.h"
#include "libdmmp_private.h"

struct dmmp_mpath {
	char *wwid;
	char *alias;
	uint32_t dmmp_pg_count;
	struct dmmp_path_group **dmmp_pgs;
	char *kdev_name;
};

_dmmp_getter_func_gen(dmmp_mpath_name_get, struct dmmp_mpath, dmmp_mp,
		      alias, const char *);
_dmmp_getter_func_gen(dmmp_mpath_wwid_get, struct dmmp_mpath, dmmp_mp,
		      wwid, const char *);
_dmmp_getter_func_gen(dmmp_mpath_kdev_name_get, struct dmmp_mpath, dmmp_mp,
		      kdev_name, const char *);

struct dmmp_mpath *_dmmp_mpath_new(void)
{
	struct dmmp_mpath *dmmp_mp = NULL;

	dmmp_mp = (struct dmmp_mpath *) malloc(sizeof(struct dmmp_mpath));

	if (dmmp_mp != NULL) {
		dmmp_mp->wwid = NULL;
		dmmp_mp->alias = NULL;
		dmmp_mp->dmmp_pg_count = 0;
		dmmp_mp->dmmp_pgs = NULL;
	}
	return dmmp_mp;
}

int _dmmp_mpath_update(struct dmmp_context *ctx, struct dmmp_mpath *dmmp_mp,
		       json_object *j_obj_map)
{
	int rc = DMMP_OK;
	const char *wwid = NULL;
	const char *alias = NULL;
	struct array_list *ar_pgs = NULL;
	int ar_pgs_len = -1;
	uint32_t i = 0;
	struct dmmp_path_group *dmmp_pg = NULL;
	const char *kdev_name = NULL;

	assert(ctx != NULL);
	assert(dmmp_mp != NULL);
	assert(j_obj_map != NULL);

	_json_obj_get_value(ctx, j_obj_map, wwid, "uuid", json_type_string,
			    json_object_get_string, rc, out);
	_json_obj_get_value(ctx, j_obj_map, alias, "name", json_type_string,
			    json_object_get_string, rc, out);
	_json_obj_get_value(ctx, j_obj_map, kdev_name, "sysfs",
			    json_type_string, json_object_get_string, rc, out);

	_dmmp_null_or_empty_str_check(ctx, wwid, rc, out);
	_dmmp_null_or_empty_str_check(ctx, alias, rc, out);

	dmmp_mp->wwid = strdup(wwid);
	_dmmp_alloc_null_check(ctx, dmmp_mp->wwid, rc, out);
	dmmp_mp->alias = strdup(alias);
	_dmmp_alloc_null_check(ctx, dmmp_mp->alias, rc, out);
	dmmp_mp->kdev_name = strdup(kdev_name);
	_dmmp_alloc_null_check(ctx, dmmp_mp->kdev_name, rc, out);

	_json_obj_get_value(ctx, j_obj_map, ar_pgs, "path_groups",
			    json_type_array, json_object_get_array, rc, out);
	ar_pgs_len = array_list_length(ar_pgs);
	if (ar_pgs_len < 0) {
		rc = DMMP_ERR_BUG;
		_error(ctx, "BUG: Got negative length for ar_pgs");
		goto out;
	}
	else if (ar_pgs_len == 0)
		goto out;
	else
		dmmp_mp->dmmp_pg_count = ar_pgs_len & UINT32_MAX;

	dmmp_mp->dmmp_pgs = (struct dmmp_path_group **)
		malloc(sizeof(struct dmmp_path_group *) *
		       dmmp_mp->dmmp_pg_count);
	_dmmp_alloc_null_check(ctx, dmmp_mp->dmmp_pgs, rc, out);
	for (; i < dmmp_mp->dmmp_pg_count; ++i)
		dmmp_mp->dmmp_pgs[i] = NULL;

	for (i = 0; i < dmmp_mp->dmmp_pg_count; ++i) {
		dmmp_pg = _dmmp_path_group_new();
		_dmmp_alloc_null_check(ctx, dmmp_pg, rc, out);
		dmmp_mp->dmmp_pgs[i] = dmmp_pg;
		_good(_dmmp_path_group_update(ctx, dmmp_pg,
					      array_list_get_idx(ar_pgs, i)),
		      rc, out);
	}

	_debug(ctx, "Got mpath wwid: '%s', alias: '%s'", dmmp_mp->wwid,
	       dmmp_mp->alias);

out:
	if (rc != DMMP_OK)
		_dmmp_mpath_free(dmmp_mp);
	return rc;
}

void _dmmp_mpath_free(struct dmmp_mpath *dmmp_mp)
{
	if (dmmp_mp == NULL)
		return ;

	free((char *) dmmp_mp->alias);
	free((char *) dmmp_mp->wwid);
	free((char *) dmmp_mp->kdev_name);

	if (dmmp_mp->dmmp_pgs != NULL)
		_dmmp_path_group_array_free(dmmp_mp->dmmp_pgs,
					    dmmp_mp->dmmp_pg_count);

	free(dmmp_mp);
}

void dmmp_path_group_array_get(struct dmmp_mpath *dmmp_mp,
			       struct dmmp_path_group ***dmmp_pgs,
			       uint32_t *dmmp_pg_count)
{
	assert(dmmp_mp != NULL);
	assert(dmmp_pgs != NULL);
	assert(dmmp_pg_count != NULL);

	*dmmp_pgs = dmmp_mp->dmmp_pgs;
	*dmmp_pg_count = dmmp_mp->dmmp_pg_count;
}
