/*
 * Copyright (C) 2015 - 2017 Red Hat, Inc.
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
#include <stdbool.h>
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
#include <time.h>
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
#define _IPC_MAX_CMD_LEN			512
/* ^ Was _MAX_CMD_LEN in ./libmultipath/uxsock.h */
#define _LAST_ERR_MSG_BUFF_SIZE			1024

struct dmmp_context {
	void (*log_func)(struct dmmp_context *ctx, int priority,
			 const char *file, int line, const char *func_name,
			 const char *format, va_list args);
	int log_priority;
	void *userdata;
	unsigned int tmo;
	char last_err_msg[_LAST_ERR_MSG_BUFF_SIZE];
};

/*
 * The multipathd daemon are using "uxsock_timeout" to define timeout value,
 * if timeout at daemon side, we will get message "timeout\n".
 * To unify this timeout with `dmmp_context_timeout_set()`, this function
 * will keep retry mpath_process_cmd() tile meet the time of
 * dmmp_context_timeout_get().
 * Need to free `*output` string manually.
 */
static int _process_cmd(struct dmmp_context *ctx, int fd, const char *cmd,
			char **output);

static int _ipc_connect(struct dmmp_context *ctx, int *fd);

_dmmp_getter_func_gen(dmmp_context_log_priority_get,
		      struct dmmp_context, ctx, log_priority,
		      int);

_dmmp_getter_func_gen(dmmp_context_userdata_get, struct dmmp_context, ctx,
		      userdata, void *);

_dmmp_getter_func_gen(dmmp_context_timeout_get, struct dmmp_context, ctx, tmo,
		      unsigned int);

_dmmp_getter_func_gen(dmmp_last_error_msg, struct dmmp_context, ctx,
		      last_err_msg, const char *);

_dmmp_array_free_func_gen(dmmp_mpath_array_free, struct dmmp_mpath,
			  _dmmp_mpath_free);

void _dmmp_log(struct dmmp_context *ctx, int priority, const char *file,
	       int line, const char *func_name, const char *format, ...)
{
	va_list args;

	if (ctx->log_func == NULL)
		return;

	va_start(args, format);
	ctx->log_func(ctx, priority, file, line, func_name, format, args);
	if (priority == DMMP_LOG_PRIORITY_ERROR)
		vsnprintf(ctx->last_err_msg, _LAST_ERR_MSG_BUFF_SIZE,
			  format, args);
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
	memset(ctx->last_err_msg, 0, _LAST_ERR_MSG_BUFF_SIZE);

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
	int ipc_fd = -1;

	assert(ctx != NULL);
	assert(dmmp_mps != NULL);
	assert(dmmp_mp_count != NULL);

	*dmmp_mps = NULL;
	*dmmp_mp_count = 0;

	_good(_ipc_connect(ctx, &ipc_fd), rc, out);

	_good(_process_cmd(ctx, ipc_fd, _DMMP_IPC_SHOW_JSON_CMD, &j_str),
	      rc, out);

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
	if (ipc_fd >= 0)
		mpath_disconnect(ipc_fd);
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

static int _process_cmd(struct dmmp_context *ctx, int fd, const char *cmd,
			char **output)
{
	int errno_save = 0;
	int rc = DMMP_OK;
	char errno_str_buff[_ERRNO_STR_BUFF_SIZE];
	struct timespec start_ts;
	struct timespec cur_ts;
	unsigned int ipc_tmo = 0;
	bool flag_check_tmo = false;
	unsigned int elapsed = 0;

	assert(output != NULL);
	assert(ctx != NULL);
	assert(cmd != NULL);

	*output = NULL;

	if (clock_gettime(CLOCK_MONOTONIC, &start_ts) != 0) {
		_error(ctx, "BUG: Failed to get CLOCK_MONOTONIC time "
		       "via clock_gettime(), error %d", errno);
		return DMMP_ERR_BUG;
	}

	ipc_tmo = ctx->tmo;
	if (ctx->tmo == 0)
		ipc_tmo = _DEFAULT_UXSOCK_TIMEOUT;

invoke:
	_debug(ctx, "Invoking IPC command '%s' with IPC tmo %u milliseconds",
	       cmd, ipc_tmo);
	flag_check_tmo = false;
	if (mpath_process_cmd(fd, cmd, output, ipc_tmo) != 0) {
		errno_save = errno;
		memset(errno_str_buff, 0, _ERRNO_STR_BUFF_SIZE);
		strerror_r(errno_save, errno_str_buff, _ERRNO_STR_BUFF_SIZE);
		if (errno_save == ETIMEDOUT) {
			flag_check_tmo = true;
		} else {
			_error(ctx, "IPC failed when process command '%s' with "
			       "error %d(%s)", cmd, errno_save, errno_str_buff);
			_debug(ctx, "%s", *output);
			rc = DMMP_ERR_IPC_ERROR;
			goto out;
		}
	}
	if ((*output != NULL) &&
	    (strncmp(*output, "timeout", strlen("timeout")) == 0))
		flag_check_tmo = true;

	if (flag_check_tmo == true) {
		free(*output);
		*output = NULL;
		if (ctx->tmo == 0) {
			_debug(ctx, "IPC timeout, but user requested infinite "
			       "timeout");
			goto invoke;
		}

		if (clock_gettime(CLOCK_MONOTONIC, &cur_ts) != 0) {
			_error(ctx, "BUG: Failed to get CLOCK_MONOTONIC time "
			       "via clock_gettime(), error %d", errno);
			rc = DMMP_ERR_BUG;
			goto out;
		}
		elapsed = (cur_ts.tv_sec - start_ts.tv_sec) * 1000 +
			(cur_ts.tv_nsec - start_ts.tv_nsec) / 1000000;

		if (elapsed >= ctx->tmo) {
			rc = DMMP_ERR_IPC_TIMEOUT;
			_error(ctx, "Timeout, try to increase it via "
			       "dmmp_context_timeout_set()");
			goto out;
		}
		if (ctx->tmo != 0)
			ipc_tmo = ctx->tmo - elapsed;

		_debug(ctx, "IPC timeout, but user requested timeout has not "
		       "reached yet, still have %u milliseconds", ipc_tmo);
		goto invoke;
	} else {
		if ((*output == NULL) || (strlen(*output) == 0)) {
			_error(ctx, "IPC return empty reply for command %s",
			       cmd);
			rc = DMMP_ERR_IPC_ERROR;
			goto out;
		}
	}

	if ((*output != NULL) &&
	    strncmp(*output, "permission deny",
		    strlen("permission deny")) == 0) {
		_error(ctx, "Permission deny, need to be root");
		rc = DMMP_ERR_PERMISSION_DENY;
		goto out;
	}

out:
	if (rc != DMMP_OK) {
		free(*output);
		*output = NULL;
	}
	return rc;
}

static int _ipc_connect(struct dmmp_context *ctx, int *fd)
{
	int rc = DMMP_OK;
	int errno_save = 0;
	char errno_str_buff[_ERRNO_STR_BUFF_SIZE];

	assert(ctx != NULL);
	assert(fd != NULL);

	*fd = -1;

	*fd = mpath_connect();
	if (*fd == -1) {
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
	}
	return rc;
}

int dmmp_flush_mpath(struct dmmp_context *ctx, const char *mpath_name)
{
	int rc = DMMP_OK;
	struct dmmp_mpath **dmmp_mps = NULL;
	uint32_t dmmp_mp_count = 0;
	uint32_t i = 0;
	bool found = false;
	int ipc_fd = -1;
	char cmd[_IPC_MAX_CMD_LEN];
	char *output = NULL;

	assert(ctx != NULL);
	assert(mpath_name != NULL);

	snprintf(cmd, _IPC_MAX_CMD_LEN, "del map %s", mpath_name);
	if (strlen(cmd) == _IPC_MAX_CMD_LEN - 1) {
		rc = DMMP_ERR_INVALID_ARGUMENT;
		_error(ctx, "Invalid mpath name %s", mpath_name);
		goto out;
	}

	_good(_ipc_connect(ctx, &ipc_fd), rc, out);
	_good(_process_cmd(ctx, ipc_fd, cmd, &output), rc, out);

	/* _process_cmd() already make sure output is not NULL */

	if (strncmp(output, "fail", strlen("fail")) == 0) {
		/* Check whether specified mpath exits */
		_good(dmmp_mpath_array_get(ctx, &dmmp_mps, &dmmp_mp_count),
		      rc, out);

		for (i = 0; i < dmmp_mp_count; ++i) {
			if (strcmp(dmmp_mpath_name_get(dmmp_mps[i]),
				   mpath_name) == 0) {
				found = true;
				break;
			}
		}

		if (found == false) {
			rc = DMMP_ERR_MPATH_NOT_FOUND;
			_error(ctx, "Specified mpath %s not found", mpath_name);
			goto out;
		}

		rc = DMMP_ERR_MPATH_BUSY;
		_error(ctx, "Specified mpath is in use");
	} else if (strncmp(output, "ok", strlen("ok")) != 0) {
		rc = DMMP_ERR_BUG;
		_error(ctx, "Got unexpected output for cmd '%s': '%s'",
		       cmd, output);
	}

out:
	if (ipc_fd >= 0)
		mpath_disconnect(ipc_fd);
	dmmp_mpath_array_free(dmmp_mps, dmmp_mp_count);
	free(output);
	return rc;
}

int dmmp_reconfig(struct dmmp_context *ctx)
{
	int rc = DMMP_OK;
	int ipc_fd = -1;
	char *output = NULL;
	char cmd[_IPC_MAX_CMD_LEN];

	snprintf(cmd, _IPC_MAX_CMD_LEN, "%s", "reconfigure");

	_good(_ipc_connect(ctx, &ipc_fd), rc, out);
	_good(_process_cmd(ctx, ipc_fd, cmd, &output), rc, out);

out:
	if (ipc_fd >= 0)
		mpath_disconnect(ipc_fd);
	free(output);
	return rc;
}
