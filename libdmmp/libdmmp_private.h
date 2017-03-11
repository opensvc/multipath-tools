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

#ifndef _LIB_DMMP_PRIVATE_H_
#define _LIB_DMMP_PRIVATE_H_

/*
 * Notes:
 *	Internal/Private functions does not check input argument but using
 *	assert() to abort if NULL pointer found in argument.
 */

#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <json.h>

#include "libdmmp/libdmmp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define _good(rc, rc_val, out) \
	do { \
		rc_val = rc; \
		if (rc_val != DMMP_OK) \
			goto out; \
	} while(0)

#define _DMMP_PATH_GROUP_ID_UNKNOWN	0

struct DMMP_DLL_LOCAL _num_str_conv;
struct _num_str_conv {
	const uint32_t value;
	const char *str;
};

#define _dmmp_str_func_gen(func_name, var_type, var, conv_array) \
const char *func_name(var_type var) { \
	size_t i = 0; \
	uint32_t tmp_var = var & UINT32_MAX; \
	/* In the whole libdmmp, we don't have negative value */ \
	for (; i < sizeof(conv_array)/sizeof(conv_array[0]); ++i) { \
		if ((conv_array[i].value) == tmp_var) \
			return conv_array[i].str; \
	} \
	return "Invalid argument"; \
}

#define _dmmp_str_conv_func_gen(func_name, ctx, var_name, out_type, \
				unknown_value, conv_array) \
static out_type func_name(struct dmmp_context *ctx, const char *var_name) { \
	size_t i = 0; \
	for (; i < sizeof(conv_array)/sizeof(conv_array[0]); ++i) { \
		if (strcmp(conv_array[i].str, var_name) == 0) \
			return conv_array[i].value; \
	} \
	_warn(ctx, "Got unknown " #var_name ": '%s'", var_name); \
	return unknown_value; \
}

#define _json_obj_get_value(ctx, j_obj, out_value, key, value_type, \
			    value_func, rc, out) \
do { \
	json_type j_type = json_type_null; \
	json_object *j_obj_tmp = NULL; \
	if (json_object_object_get_ex(j_obj, key, &j_obj_tmp) != TRUE) { \
		_error(ctx, "Invalid JSON output from multipathd IPC: " \
		       "key '%s' not found", key); \
		rc = DMMP_ERR_IPC_ERROR; \
		goto out; \
	} \
	if (j_obj_tmp == NULL) { \
		_error(ctx, "BUG: Got NULL j_obj_tmp from " \
		       "json_object_object_get_ex() while it return TRUE"); \
		rc = DMMP_ERR_BUG; \
		goto out; \
	} \
	j_type = json_object_get_type(j_obj_tmp); \
	if (j_type != value_type) { \
		_error(ctx, "Invalid value type for key'%s' of JSON output " \
		       "from multipathd IPC. Should be %s(%d), " \
		       "but got %s(%d)", key, json_type_to_name(value_type), \
		       value_type, json_type_to_name(j_type), j_type); \
		rc = DMMP_ERR_IPC_ERROR; \
		goto out; \
	} \
	out_value = value_func(j_obj_tmp); \
} while(0);

DMMP_DLL_LOCAL int _dmmp_ipc_exec(struct dmmp_context *ctx, const char *cmd,
				  char **output);

DMMP_DLL_LOCAL struct dmmp_mpath *_dmmp_mpath_new(void);
DMMP_DLL_LOCAL struct dmmp_path_group *_dmmp_path_group_new(void);
DMMP_DLL_LOCAL struct dmmp_path *_dmmp_path_new(void);

DMMP_DLL_LOCAL int _dmmp_mpath_update(struct dmmp_context *ctx,
				      struct dmmp_mpath *dmmp_mp,
				      json_object *j_obj_map);
DMMP_DLL_LOCAL int _dmmp_path_group_update(struct dmmp_context *ctx,
					   struct dmmp_path_group *dmmp_pg,
					   json_object *j_obj_pg);
DMMP_DLL_LOCAL int _dmmp_path_update(struct dmmp_context *ctx,
				     struct dmmp_path *dmmp_p,
				     json_object *j_obj_p);

DMMP_DLL_LOCAL void _dmmp_mpath_free(struct dmmp_mpath *dmmp_mp);
DMMP_DLL_LOCAL void _dmmp_path_group_free(struct dmmp_path_group *dmmp_pg);
DMMP_DLL_LOCAL void _dmmp_path_group_array_free
	(struct dmmp_path_group **dmmp_pgs, uint32_t dmmp_pg_count);
DMMP_DLL_LOCAL void _dmmp_path_free(struct dmmp_path *dmmp_p);
DMMP_DLL_LOCAL void _dmmp_log(struct dmmp_context *ctx, int priority,
			      const char *file, int line,
			      const char *func_name,
			      const char *format, ...);
DMMP_DLL_LOCAL void _dmmp_log_err_str(struct dmmp_context *ctx, int rc);

DMMP_DLL_LOCAL void _dmmp_log_stderr(struct dmmp_context *ctx, int priority,
				     const char *file, int line,
				     const char *func_name, const char *format,
				     va_list args);


#define _dmmp_log_cond(ctx, prio, arg...) \
	do { \
		if (dmmp_context_log_priority_get(ctx) >= prio) \
			_dmmp_log(ctx, prio, __FILE__, __LINE__, __FUNCTION__, \
				  ## arg); \
	} while (0)

#define _debug(ctx, arg...) \
	_dmmp_log_cond(ctx, DMMP_LOG_PRIORITY_DEBUG, ## arg)
#define _info(ctx, arg...) \
	_dmmp_log_cond(ctx, DMMP_LOG_PRIORITY_INFO, ## arg)
#define _warn(ctx, arg...) \
	_dmmp_log_cond(ctx, DMMP_LOG_PRIORITY_WARNING, ## arg)
#define _error(ctx, arg...) \
	_dmmp_log_cond(ctx, DMMP_LOG_PRIORITY_ERROR, ## arg)

/*
 * Check pointer returned by malloc() or strdup(), if NULL, set
 * rc as DMMP_ERR_NO_MEMORY, report error and goto goto_out.
 */
#define _dmmp_alloc_null_check(ctx, ptr, rc, goto_out) \
	do { \
		if (ptr == NULL) { \
			rc = DMMP_ERR_NO_MEMORY; \
			_error(ctx, dmmp_strerror(rc)); \
			goto goto_out; \
		} \
	} while(0)

#define _dmmp_null_or_empty_str_check(ctx, var, rc, goto_out) \
	do { \
		if (var == NULL) { \
			rc = DMMP_ERR_BUG; \
			_error(ctx, "BUG: Got NULL " #var); \
			goto goto_out; \
		} \
		if (strlen(var) == 0) { \
			rc = DMMP_ERR_BUG; \
			_error(ctx, "BUG: Got empty " #var); \
			goto goto_out; \
		} \
	} while(0)

#define _dmmp_getter_func_gen(func_name, struct_name, struct_data, \
			      prop_name, prop_type) \
	prop_type func_name(struct_name *struct_data) \
	{ \
		assert(struct_data != NULL); \
		return struct_data->prop_name; \
	}

#define _dmmp_array_free_func_gen(func_name, struct_name, struct_free_func) \
	void func_name(struct_name **ptr_array, uint32_t ptr_count) \
	{ \
		uint32_t i = 0; \
		if (ptr_array == NULL) \
			return; \
		for (; i < ptr_count; ++i) \
			struct_free_func(ptr_array[i]); \
		free(ptr_array); \
	}

#ifdef __cplusplus
} /* End of extern "C" */
#endif

#endif /* End of _LIB_DMMP_PRIVATE_H_ */
