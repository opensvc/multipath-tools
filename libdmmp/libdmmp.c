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

#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <libudev.h>
#include <errno.h>
#include <libdevmapper.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>
#include <json.h>
#include <mpath_cmd.h>

#include "libdmmp/libdmmp.h"
#include "libdmmp_private.h"

#define _DEFAULT_UXSOCK_TIMEOUT		60000
/* ^ 60 seconds. On system with 10k sdX, dmmp_mpath_array_get()
 *   only take 3.5 seconds, so this default value should be OK for most users.
 */

#define _DMMP_IPC_SHOW_JSON_CMD			"show maps json"
#define _DMMP_JSON_MAJOR_KEY			"major_version"
#define _DMMP_JSON_MAJOR_VERSION		0
#define _DMMP_JSON_MAPS_KEY			"maps"
#define _ERRNO_STR_BUFF_SIZE			256

struct dmmp_context {
	void (*log_func)(struct dmmp_context *ctx, int priority,
			 const char *file, int line, const char *func_name,
			 const char *format, va_list args);
	int log_priority;
	void *userdata;
	unsigned int tmo;
};

_dmmp_getter_func_gen(dmmp_context_log_priority_get,
		      struct dmmp_context, ctx, log_priority,
		      int);

_dmmp_getter_func_gen(dmmp_context_userdata_get, struct dmmp_context, ctx,
		      userdata, void *);

_dmmp_getter_func_gen(dmmp_context_timeout_get, struct dmmp_context, ctx, tmo,
		      unsigned int);

_dmmp_array_free_func_gen(dmmp_mpath_array_free, struct dmmp_mpath,
			  _dmmp_mpath_free);

void _dmmp_log(struct dmmp_context *ctx, int priority, const char *file,
	       int line, const char *func_name, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	ctx->log_func(ctx, priority, file, line, func_name, format, args);
	va_end(args);
}

struct dmmp_context *dmmp_context_new(void)
{
	struct dmmp_context *ctx = NULL;

	ctx = (struct dmmp_context *) malloc(sizeof(struct dmmp_context));

	if (ctx == NULL)
		return NULL;

	ctx->log_func = _dmmp_log_stderr;
	ctx->log_priority = DMMP_LOG_PRIORITY_DEFAULT;
	ctx->userdata = NULL;
	ctx->tmo = _DEFAULT_UXSOCK_TIMEOUT;

	return ctx;
}

void dmmp_context_free(struct dmmp_context *ctx)
{
	free(ctx);
}

void dmmp_context_log_priority_set(struct dmmp_context *ctx, int priority)
{
	assert(ctx != NULL);
	ctx->log_priority = priority;
}

void dmmp_context_timeout_set(struct dmmp_context *ctx, unsigned int tmo)
{
	assert(ctx != NULL);
	ctx->tmo = tmo;
}

void dmmp_context_log_func_set
	(struct dmmp_context *ctx,
	 void (*log_func)(struct dmmp_context *ctx, int priority,
			  const char *file, int line, const char *func_name,
			  const char *format, va_list args))
{
	assert(ctx != NULL);
	ctx->log_func = log_func;
}

void dmmp_context_userdata_set(struct dmmp_context *ctx, void *userdata)
{
	assert(ctx != NULL);
	ctx->userdata = userdata;
}

int dmmp_mpath_array_get(struct dmmp_context *ctx,
			 struct dmmp_mpath ***dmmp_mps, uint32_t *dmmp_mp_count)
{
	struct dmmp_mpath *dmmp_mp = NULL;
	int rc = DMMP_OK;
	char *j_str = NULL;
	json_object *j_obj = NULL;
	json_object *j_obj_map = NULL;
	enum json_tokener_error j_err = json_tokener_success;
	json_tokener *j_token = NULL;
	struct array_list *ar_maps = NULL;
	uint32_t i = 0;
	int cur_json_major_version = -1;
	int ar_maps_len = -1;
	int socket_fd = -1;
	int errno_save = 0;
	char errno_str_buff[_ERRNO_STR_BUFF_SIZE];

	assert(ctx != NULL);
	assert(dmmp_mps != NULL);
	assert(dmmp_mp_count != NULL);

	*dmmp_mps = NULL;
	*dmmp_mp_count = 0;

	socket_fd = mpath_connect();
	if (socket_fd == -1) {
		errno_save = errno;
		memset(errno_str_buff, 0, _ERRNO_STR_BUFF_SIZE);
		strerror_r(errno_save, errno_str_buff, _ERRNO_STR_BUFF_SIZE);
		if (errno_save == ECONNREFUSED) {
			rc = DMMP_ERR_NO_DAEMON;
			_error(ctx, "Socket connection refuse. "
			       "Maybe multipathd daemon is not running");
		} else {
			_error(ctx, "IPC failed with error %d(%s)", errno_save,
			       errno_str_buff);
			rc = DMMP_ERR_IPC_ERROR;
		}
		goto out;
	}

	if (mpath_process_cmd(socket_fd, _DMMP_IPC_SHOW_JSON_CMD,
			      &j_str, ctx->tmo) != 0) {
		errno_save = errno;
		memset(errno_str_buff, 0, _ERRNO_STR_BUFF_SIZE);
		strerror_r(errno_save, errno_str_buff, _ERRNO_STR_BUFF_SIZE);
		mpath_disconnect(socket_fd);
		if (errno_save == ETIMEDOUT) {
			rc = DMMP_ERR_IPC_TIMEOUT;
			_error(ctx, "IPC communication timeout, try to "
			       "increase it via dmmp_context_timeout_set()");
			goto out;
		}
		_error(ctx, "IPC failed when process command '%s' with "
		       "error %d(%s)", _DMMP_IPC_SHOW_JSON_CMD, errno_save,
		       errno_str_buff);
		rc = DMMP_ERR_IPC_ERROR;
		goto out;
	}

	if ((j_str == NULL) || (strlen(j_str) == 0)) {
		_error(ctx, "IPC return empty reply for command %s",
		       _DMMP_IPC_SHOW_JSON_CMD);
		rc = DMMP_ERR_IPC_ERROR;
		goto out;
	}

	_debug(ctx, "Got json output from multipathd: '%s'", j_str);
	j_token = json_tokener_new();
	if (j_token == NULL) {
		rc = DMMP_ERR_BUG;
		_error(ctx, "BUG: json_tokener_new() retuned NULL");
		goto out;
	}
	j_obj = json_tokener_parse_ex(j_token, j_str, strlen(j_str) + 1);

	if (j_obj == NULL) {
		rc = DMMP_ERR_IPC_ERROR;
		j_err = json_tokener_get_error(j_token);
		_error(ctx, "Failed to parse JSON output from multipathd IPC: "
		       "%s", json_tokener_error_desc(j_err));
		goto out;
	}

	_json_obj_get_value(ctx, j_obj, cur_json_major_version,
			    _DMMP_JSON_MAJOR_KEY, json_type_int,
			    json_object_get_int, rc, out);

	if (cur_json_major_version != _DMMP_JSON_MAJOR_VERSION) {
		rc = DMMP_ERR_INCOMPATIBLE;
		_error(ctx, "Incompatible multipathd JSON major version %d, "
		       "should be %d", cur_json_major_version,
		       _DMMP_JSON_MAJOR_VERSION);
		goto out;
	}
	_debug(ctx, "multipathd JSON major version(%d) check pass",
	       _DMMP_JSON_MAJOR_VERSION);

	_json_obj_get_value(ctx, j_obj, ar_maps, _DMMP_JSON_MAPS_KEY,
			    json_type_array, json_object_get_array, rc, out);

	if (ar_maps == NULL) {
		rc = DMMP_ERR_BUG;
		_error(ctx, "BUG: Got NULL map array from "
		       "_json_obj_get_value()");
		goto out;
	}

	ar_maps_len = array_list_length(ar_maps);
	if (ar_maps_len < 0) {
		rc = DMMP_ERR_BUG;
		_error(ctx, "BUG: Got negative length for ar_maps");
		goto out;
	}
	else if (ar_maps_len == 0)
		goto out;
	else
		*dmmp_mp_count = ar_maps_len & UINT32_MAX;

	*dmmp_mps = (struct dmmp_mpath **)
		malloc(sizeof(struct dmmp_mpath *) * (*dmmp_mp_count));
	_dmmp_alloc_null_check(ctx, dmmp_mps, rc, out);
	for (; i < *dmmp_mp_count; ++i)
		(*dmmp_mps)[i] = NULL;

	for (i = 0; i < *dmmp_mp_count; ++i) {
		j_obj_map = array_list_get_idx(ar_maps, i);
		if (j_obj_map == NULL) {
			rc = DMMP_ERR_BUG;
			_error(ctx, "BUG: array_list_get_idx() return NULL");
			goto out;
		}

		dmmp_mp = _dmmp_mpath_new();
		_dmmp_alloc_null_check(ctx, dmmp_mp, rc, out);
		(*dmmp_mps)[i] = dmmp_mp;
		_good(_dmmp_mpath_update(ctx, dmmp_mp, j_obj_map), rc, out);
	}

out:
	if (socket_fd >= 0)
		mpath_disconnect(socket_fd);
	free(j_str);
	if (j_token != NULL)
		json_tokener_free(j_token);
	if (j_obj != NULL)
		json_object_put(j_obj);

	if (rc != DMMP_OK) {
		dmmp_mpath_array_free(*dmmp_mps, *dmmp_mp_count);
		*dmmp_mps = NULL;
		*dmmp_mp_count = 0;
	}

	return rc;
}
