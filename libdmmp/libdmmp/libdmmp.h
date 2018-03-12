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


#ifndef _LIB_DMMP_H_
#define _LIB_DMMP_H_

#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DMMP_DLL_EXPORT		__attribute__ ((visibility ("default")))
#define DMMP_DLL_LOCAL		__attribute__ ((visibility ("hidden")))

#define DMMP_OK				0
#define DMMP_ERR_BUG			1
#define DMMP_ERR_NO_MEMORY		2
#define DMMP_ERR_IPC_TIMEOUT		3
#define DMMP_ERR_IPC_ERROR		4
#define DMMP_ERR_NO_DAEMON		5
#define DMMP_ERR_INCOMPATIBLE		6
#define DMMP_ERR_MPATH_BUSY		7
#define DMMP_ERR_MPATH_NOT_FOUND	8
#define DMMP_ERR_INVALID_ARGUMENT	9
#define DMMP_ERR_PERMISSION_DENY	10

/*
 * Use the syslog severity level as log priority
 */
#define DMMP_LOG_PRIORITY_ERROR		3
#define DMMP_LOG_PRIORITY_WARNING	4
#define DMMP_LOG_PRIORITY_INFO		6
#define DMMP_LOG_PRIORITY_DEBUG		7

#define DMMP_LOG_PRIORITY_DEFAULT	DMMP_LOG_PRIORITY_WARNING

/**
 * dmmp_log_priority_str() - Convert log priority to string.
 *
 * Convert log priority to string (const char *).
 *
 * @priority:
 *	int. Log priority.
 *
 * Return:
 *	const char *. Valid string are:
 *
 *	* "ERROR" for DMMP_LOG_PRIORITY_ERROR
 *
 *	* "WARN " for DMMP_LOG_PRIORITY_WARNING
 *
 *	* "INFO " for DMMP_LOG_PRIORITY_INFO
 *
 *	* "DEBUG" for DMMP_LOG_PRIORITY_DEBUG
 *
 *	* "Invalid argument" for invalid log priority.
 */
DMMP_DLL_EXPORT const char *dmmp_log_priority_str(int priority);

struct DMMP_DLL_EXPORT dmmp_context;

struct DMMP_DLL_EXPORT dmmp_mpath;

struct DMMP_DLL_EXPORT dmmp_path_group;

#define DMMP_PATH_GROUP_STATUS_UNKNOWN	0
#define DMMP_PATH_GROUP_STATUS_ENABLED	1
#define DMMP_PATH_GROUP_STATUS_DISABLED	2
#define DMMP_PATH_GROUP_STATUS_ACTIVE	3

struct DMMP_DLL_EXPORT dmmp_path;

#define DMMP_PATH_STATUS_UNKNOWN	0
//#define DMMP_PATH_STATUS_UNCHECKED	1
// ^ print.h does not expose this.
#define DMMP_PATH_STATUS_DOWN		2
#define DMMP_PATH_STATUS_UP		3
#define DMMP_PATH_STATUS_SHAKY		4
#define DMMP_PATH_STATUS_GHOST		5
#define DMMP_PATH_STATUS_PENDING	6
#define DMMP_PATH_STATUS_TIMEOUT	7
//#define DMMP_PATH_STATUS_REMOVED	8
// ^ print.h does not expose this.
#define DMMP_PATH_STATUS_DELAYED	9

/**
 * dmmp_strerror() - Convert error code to string.
 *
 * Convert error code (int) to string (const char *):
 *
 *	* DMMP_OK -- "OK"
 *
 *	* DMMP_ERR_BUG -- "BUG of libdmmp library"
 *
 *	* DMMP_ERR_NO_MEMORY -- "Out of memory"
 *
 *	* DMMP_ERR_IPC_TIMEOUT -- "Timeout when communicate with multipathd,
 *	  try to set bigger timeout value via dmmp_context_timeout_set ()"
 *
 *	* DMMP_ERR_IPC_ERROR -- "Error when communicate with multipathd daemon"
 *
 *	* DMMP_ERR_NO_DAEMON -- "The multipathd daemon not started"
 *
 *	* DMMP_ERR_INCOMPATIBLE -- "The multipathd daemon version is not
 *	  compatible with current library"
 *
 *	* Other invalid error number -- "Invalid argument"
 *
 * @rc:
 *	int. Return code by libdmmp functions. When provided error code is not a
 *	valid error code, return "Invalid argument".
 *
 * Return:
 *	const char *. The meaning of provided error code.
 *
 */
DMMP_DLL_EXPORT const char *dmmp_strerror(int rc);

/**
 * dmmp_context_new() - Create struct dmmp_context.
 *
 * The default logging level (DMMP_LOG_PRIORITY_DEFAULT) is
 * DMMP_LOG_PRIORITY_WARNING which means only warning and error message will be
 * forward to log handler function.  The default log handler function will print
 * log message to STDERR, to change so, please use dmmp_context_log_func_set()
 * to set your own log handler, check manpage libdmmp.h(3) for detail.
 *
 * Return:
 *	Pointer of 'struct dmmp_context'. Should be freed by
 *	dmmp_context_free().
 */
DMMP_DLL_EXPORT struct dmmp_context *dmmp_context_new(void);

/**
 * dmmp_context_free() - Release the memory of struct dmmp_context.
 *
 * Release the memory of struct dmmp_context, but the userdata memory defined
 * via dmmp_context_userdata_set() will not be touched.
 *
 * @ctx:
 *	Pointer of 'struct dmmp_context'.
 * Return:
 *	void
 */
DMMP_DLL_EXPORT void dmmp_context_free(struct dmmp_context *ctx);

/**
 * dmmp_context_timeout_set() - Set IPC timeout.
 *
 * By default, the IPC to multipathd daemon will timeout after 60 seconds.
 *
 * @ctx:
 *	Pointer of 'struct dmmp_context'.
 *	If this pointer is NULL, your program will be terminated by assert.
 *
 * @tmo:
 *	Timeout in milliseconds(1 seconds equal 1000 milliseconds).
 *	0 means infinite, function only return when error or pass.
 *
 * Return:
 *	void
 */
DMMP_DLL_EXPORT void dmmp_context_timeout_set(struct dmmp_context *ctx,
					      unsigned int tmo);

/**
 * dmmp_context_timeout_get() - Get IPC timeout.
 *
 * Retrieve timeout value of IPC connection to multipathd daemon.
 *
 * @ctx:
 *	Pointer of 'struct dmmp_context'.
 *	If this pointer is NULL, your program will be terminated by assert.
 *
 * Return:
 *	unsigned int. Timeout in milliseconds.
 */
DMMP_DLL_EXPORT unsigned int dmmp_context_timeout_get(struct dmmp_context *ctx);

/**
 * dmmp_context_log_priority_set() - Set log priority.
 *
 *
 * When library generates log message, only equal or more important(less value)
 * message will be forwarded to log handler function. Valid log priority values
 * are:
 *
 *	* DMMP_LOG_PRIORITY_ERROR -- 3
 *
 *	* DMMP_LOG_PRIORITY_WARNING -- 4
 *
 *	* DMMP_LOG_PRIORITY_INFO -- 5
 *
 *	* DMMP_LOG_PRIORITY_DEBUG -- 7
 *
 * @ctx:
 *	Pointer of 'struct dmmp_context'.
 *	If this pointer is NULL, your program will be terminated by assert.
 *
 * @priority:
 *	int, log priority.
 *
 * Return:
 *	void
 */
DMMP_DLL_EXPORT void dmmp_context_log_priority_set(struct dmmp_context *ctx,
						   int priority);

/**
 * dmmp_context_log_priority_get() - Get log priority.
 *
 * Retrieve current log priority. Valid log priority values are:
 *
 *	* DMMP_LOG_PRIORITY_ERROR -- 3
 *
 *	* DMMP_LOG_PRIORITY_WARNING -- 4
 *
 *	* DMMP_LOG_PRIORITY_INFO -- 5
 *
 *	* DMMP_LOG_PRIORITY_DEBUG -- 7
 *
 * @ctx:
 *	Pointer of 'struct dmmp_context'.
 *	If this pointer is NULL, your program will be terminated by assert.
 *
 * Return:
 *	int, log priority.
 */
DMMP_DLL_EXPORT int dmmp_context_log_priority_get(struct dmmp_context *ctx);

/**
 * dmmp_context_log_func_set() - Set log handler function.
 *
 * Set custom log handler. The log handler will be invoked when log message
 * is equal or more important(less value) than log priority setting.
 * Please check manpage libdmmp.h(3) for detail usage.
 *
 * @ctx:
 *	Pointer of 'struct dmmp_context'.
 *	If this pointer is NULL, your program will be terminated by assert.
 * @log_func:
 *	Pointer of log handler function. If set to NULL, all log will be
 *	ignored.
 *
 * Return:
 *	void
 */
DMMP_DLL_EXPORT void dmmp_context_log_func_set
	(struct dmmp_context *ctx,
	 void (*log_func)
	 (struct dmmp_context *ctx, int priority,
	  const char *file, int line, const char *func_name,
	  const char *format, va_list args));

/**
 * dmmp_context_userdata_set() - Set user data pointer.
 *
 * Store user data pointer into 'struct dmmp_context'.
 *
 * @ctx:
 *	Pointer of 'struct dmmp_context'.
 *	If this pointer is NULL, your program will be terminated by assert.
 * @userdata:
 *	Pointer of user defined data.
 *
 * Return:
 *	void
 */
DMMP_DLL_EXPORT void dmmp_context_userdata_set(struct dmmp_context *ctx,
					       void *userdata);

/**
 * dmmp_context_userdata_get() - Get user data pointer.
 *
 * Retrieve user data pointer from 'struct dmmp_context'.
 *
 * @ctx:
 *	Pointer of 'struct dmmp_context'.
 *	If this pointer is NULL, your program will be terminated by assert.
 *
 * Return:
 *	void *. Pointer of user defined data.
 */
DMMP_DLL_EXPORT void *dmmp_context_userdata_get(struct dmmp_context *ctx);

/**
 * dmmp_mpath_array_get() - Query all existing multipath devices.
 *
 * Query all existing multipath devices and store them into a pointer array.
 * The memory of 'dmmp_mps' should be freed via dmmp_mpath_array_free().
 *
 * @ctx:
 *	Pointer of 'struct dmmp_context'.
 *	If this pointer is NULL, your program will be terminated by assert.
 * @dmmp_mps:
 *	Output pointer array of 'struct dmmp_mpath'.
 *	If this pointer is NULL, your program will be terminated by assert.
 * @dmmp_mp_count:
 *	Output pointer of uint32_t. Hold the size of 'dmmp_mps' pointer array.
 *	If this pointer is NULL, your program will be terminated by assert.
 *
 * Return:
 *	int. Valid error codes are:
 *
 *	* DMMP_OK
 *
 *	* DMMP_ERR_BUG
 *
 *	* DMMP_ERR_NO_MEMORY
 *
 *	* DMMP_ERR_NO_DAEMON
 *
 *	* DMMP_ERR_INCONSISTENT_DATA
 *
 *	Error number could be converted to string by dmmp_strerror().
 */
DMMP_DLL_EXPORT int dmmp_mpath_array_get(struct dmmp_context *ctx,
					 struct dmmp_mpath ***dmmp_mps,
					 uint32_t *dmmp_mp_count);

/**
 * dmmp_mpath_array_free() - Free 'struct dmmp_mpath' pointer array.
 *
 * Free the 'dmmp_mps' pointer array generated by dmmp_mpath_array_get().
 * If provided 'dmmp_mps' pointer is NULL or dmmp_mp_count == 0, do nothing.
 *
 * @dmmp_mps:
 *	Pointer of 'struct dmmp_mpath' array.
 * @dmmp_mp_count:
 *	uint32_t, the size of 'dmmp_mps' pointer array.
 *
 * Return:
 *	void
 */
DMMP_DLL_EXPORT void dmmp_mpath_array_free(struct dmmp_mpath **dmmp_mps,
					   uint32_t dmmp_mp_count);

/**
 * dmmp_mpath_wwid_get() - Retrieve WWID of certain mpath.
 *
 * @dmmp_mp:
 *	Pointer of 'struct dmmp_mpath'.
 *	If this pointer is NULL, your program will be terminated by assert.
 *
 * Return:
 *	const char *. No need to free this memory, the resources will get
 *	freed when dmmp_mpath_array_free().
 */
DMMP_DLL_EXPORT const char *dmmp_mpath_wwid_get(struct dmmp_mpath *dmmp_mp);

/**
 * dmmp_mpath_name_get() - Retrieve name(alias) of certain mpath.
 *
 * Retrieve the name (also known as alias) of certain mpath.
 * When the config 'user_friendly_names' been set 'no', the name will be
 * identical to WWID retrieved by dmmp_mpath_wwid_get().
 *
 * @dmmp_mp:
 *	Pointer of 'struct dmmp_mpath'.
 *	If this pointer is NULL, your program will be terminated by assert.
 *
 * Return:
 *	const char *. No need to free this memory, the resources will get
 *	freed when dmmp_mpath_array_free().
 */
DMMP_DLL_EXPORT const char *dmmp_mpath_name_get(struct dmmp_mpath *dmmp_mp);

/**
 * dmmp_mpath_kdev_name_get() - Retrieve kernel DEVNAME of certain mpath.
 *
 * Retrieve DEVNAME name used by kernel uevent of specified mpath.
 * For example: 'dm-1'.
 *
 * @dmmp_mp:
 *	Pointer of 'struct dmmp_mpath'.
 *	If this pointer is NULL, your program will be terminated by assert.
 *
 * Return:
 *	const char *. No need to free this memory, the resources will get
 *	freed when dmmp_mpath_array_free().
 */
DMMP_DLL_EXPORT const char *dmmp_mpath_kdev_name_get
	(struct dmmp_mpath *dmmp_mp);

/**
 * dmmp_path_group_array_get() - Retrieve path groups pointer array.
 *
 * Retrieve the path groups of certain mpath.
 *
 * The memory of output pointer array is hold by 'struct dmmp_mpath', no
 * need to free this memory, the resources will got freed when
 * dmmp_mpath_array_free().
 *
 * @dmmp_mp:
 *	Pointer of 'struct dmmp_mpath'.
 *	If this pointer is NULL, your program will be terminated by assert.
 * @dmmp_pgs:
 *	Output pointer of 'struct dmmp_path_group' pointer array.
 *	If this pointer is NULL, your program will be terminated by assert.
 * @dmmp_pg_count:
 *	Output pointer of uint32_t. Hold the size of 'dmmp_pgs' pointer array.
 *	If this pointer is NULL, your program will be terminated by assert.
 *
 * Return:
 *	void
 */
DMMP_DLL_EXPORT void dmmp_path_group_array_get
	(struct dmmp_mpath *dmmp_mp, struct dmmp_path_group ***dmmp_pgs,
	 uint32_t *dmmp_pg_count);

/**
 * dmmp_path_group_id_get() - Retrieve path group ID.
 *
 * Retrieve the path group ID which could be used to switch active path group
 * via command:
 *
 *	multipathd -k'switch multipath mpathb group $id'
 *
 * @dmmp_pg:
 *	Pointer of 'struct dmmp_path_group'.
 *	If this pointer is NULL, your program will be terminated by assert.
 *
 * Return:
 *	uint32_t.
 */
DMMP_DLL_EXPORT uint32_t dmmp_path_group_id_get
	(struct dmmp_path_group *dmmp_pg);

/**
 * dmmp_path_group_priority_get() - Retrieve path group priority.
 *
 * The enabled path group with highest priority will be next active path group
 * if active path group down.
 *
 * @dmmp_pg:
 *	Pointer of 'struct dmmp_path_group'.
 *	If this pointer is NULL, your program will be terminated by assert.
 *
 * Return:
 *	uint32_t.
 */
DMMP_DLL_EXPORT uint32_t dmmp_path_group_priority_get
	(struct dmmp_path_group *dmmp_pg);

/**
 * dmmp_path_group_status_get() - Retrieve path group status.
 *
 * The valid path group statuses are:
 *
 *	* DMMP_PATH_GROUP_STATUS_UNKNOWN
 *
 *	* DMMP_PATH_GROUP_STATUS_ENABLED  -- standby to be active
 *
 *	* DMMP_PATH_GROUP_STATUS_DISABLED -- disabled due to all path down
 *
 *	* DMMP_PATH_GROUP_STATUS_ACTIVE -- selected to handle I/O
 *
 * @dmmp_pg:
 *	Pointer of 'struct dmmp_path_group'.
 *	If this pointer is NULL, your program will be terminated by assert.
 *
 * Return:
 *	uint32_t.
 */
DMMP_DLL_EXPORT uint32_t dmmp_path_group_status_get
	(struct dmmp_path_group *dmmp_pg);

/**
 * dmmp_path_group_status_str() - Convert path group status to string.
 *
 * Convert path group status uint32_t to string (const char *).
 *
 * @pg_status:
 *	uint32_t. Path group status.
 *	When provided value is not a valid path group status, return "Invalid
 *	argument".
 *
 * Return:
 *	const char *. Valid string are:
 *
 *		* "Invalid argument"
 *
 *		* "undef"
 *
 *		* "enabled"
 *
 *		* "disabled"
 *
 *		* "active"
 */
DMMP_DLL_EXPORT const char *dmmp_path_group_status_str(uint32_t pg_status);

/**
 * dmmp_path_group_selector_get() - Retrieve path group selector.
 *
 * Path group selector determine which path in active path group will be
 * use to next I/O.
 *
 * @dmmp_pg:
 *	Pointer of 'struct dmmp_path_group'.
 *	If this pointer is NULL, your program will be terminated by assert.
 *
 * Return:
 *	const char *.
 */
DMMP_DLL_EXPORT const char *dmmp_path_group_selector_get
	(struct dmmp_path_group *dmmp_pg);

/**
 * dmmp_path_array_get() - Retrieve path pointer array.
 *
 * The memory of output pointer array is hold by 'struct dmmp_mpath', no
 * need to free this memory, the resources will got freed when
 * dmmp_mpath_array_free().
 *
 * @dmmp_pg:
 *	Pointer of 'struct dmmp_path_group'.
 *	If this pointer is NULL, your program will be terminated by assert.
 * @dmmp_ps:
 *	Output pointer of 'struct dmmp_path' pointer array.
 *	If this pointer is NULL, your program will be terminated by assert.
 * @dmmp_p_count:
 *	Output pointer of uint32_t. Hold the size of 'dmmp_ps' pointer array.
 *	If this pointer is NULL, your program will be terminated by assert.
 *
 * Return:
 *	void
 */
DMMP_DLL_EXPORT void dmmp_path_array_get(struct dmmp_path_group *dmmp_pg,
					 struct dmmp_path ***dmmp_ps,
					 uint32_t *dmmp_p_count);

/**
 * dmmp_path_blk_name_get() - Retrieve block name.
 *
 * Retrieve block name of certain path. The example of block names are "sda",
 * "nvme0n1".
 *
 * @dmmp_p:
 *	Pointer of 'struct dmmp_path'.
 *	If this pointer is NULL, your program will be terminated by assert.
 *
 * Return:
 *	const char *. No need to free this memory, the resources will get
 *	freed when dmmp_mpath_array_free().
 */
DMMP_DLL_EXPORT const char *dmmp_path_blk_name_get(struct dmmp_path *dmmp_p);

/**
 * dmmp_path_status_get() - Retrieve the path status.
 *
 * The valid path statuses are:
 *
 *	* DMMP_PATH_STATUS_UNKNOWN
 *
 *	* DMMP_PATH_STATUS_DOWN
 *
 *	Path is down and you shouldn't try to send commands to it.
 *
 *	* DMMP_PATH_STATUS_UP
 *
 *	Path is up and I/O can be sent to it.
 *
 *	* DMMP_PATH_STATUS_SHAKY
 *
 *	Only emc_clariion checker when path not available for "normal"
 *	operations.
 *
 *	* DMMP_PATH_STATUS_GHOST
 *
 *		Only hp_sw and rdac checkers.  Indicates a "passive/standby"
 *		path on active/passive HP arrays. These paths will return valid
 *		answers to certain SCSI commands (tur, read_capacity, inquiry,
 *		start_stop), but will fail I/O commands.  The path needs an
 *		initialization command to be sent to it in order for I/Os to
 *		succeed.
 *
 *	* DMMP_PATH_STATUS_PENDING
 *
 *	Available for all async checkers when a check IO is in flight.
 *
 *	* DMMP_PATH_STATUS_TIMEOUT
 *
 *	Only tur checker when command timed out.
 *
 *	* DMMP_PATH_STATUS_DELAYED
 *
 *	If a path fails after being up for less than delay_watch_checks checks,
 *	when it comes back up again, it will not be marked as up until it has
 *	been up for delay_wait_checks checks. During this time, it is marked as
 *	"delayed".
 *
 * @dmmp_p:
 *	Pointer of 'struct dmmp_path'.
 *	If this pointer is NULL, your program will be terminated by assert.
 *
 * Return:
 *	uint32_t.
 */
DMMP_DLL_EXPORT uint32_t dmmp_path_status_get(struct dmmp_path *dmmp_p);

/**
 * dmmp_path_status_str() - Convert path status to string.
 *
 * Convert path status uint32_t to string (const char *):
 *
 *	* DMMP_PATH_STATUS_UNKNOWN -- "undef"
 *
 *	* DMMP_PATH_STATUS_DOWN -- "faulty"
 *
 *	* DMMP_PATH_STATUS_UP -- "ready"
 *
 *	* DMMP_PATH_STATUS_SHAKY -- "shaky"
 *
 *	* DMMP_PATH_STATUS_GHOST -- "ghost"
 *
 *	* DMMP_PATH_STATUS_PENDING -- "pending"
 *
 *	* DMMP_PATH_STATUS_TIMEOUT -- "timeout"
 *
 *	* DMMP_PATH_STATUS_REMOVED -- "removed"
 *
 *	* DMMP_PATH_STATUS_DELAYED -- "delayed"
 *
 * @path_status:
 *	uint32_t. Path status.
 *	When provided value is not a valid path status, return
 *	"Invalid argument".
 *
 * Return:
 *	const char *. The meaning of status value.
 */
DMMP_DLL_EXPORT const char *dmmp_path_status_str(uint32_t path_status);

/**
 * dmmp_flush_mpath() - Flush specified multipath device map if unused.
 *
 * Flush a multipath device map specified as parameter, if unused.
 *
 * @ctx:
 *	Pointer of 'struct dmmp_context'.
 *	If this pointer is NULL, your program will be terminated by assert.
 * @mpath_name:
 *	const char *. The name of multipath device map.
 *
 * Return:
 *	int. Valid error codes are:
 *
 *	* DMMP_OK
 *
 *	* DMMP_ERR_BUG
 *
 *	* DMMP_ERR_NO_MEMORY
 *
 *	* DMMP_ERR_NO_DAEMON
 *
 *	* DMMP_ERR_MPATH_BUSY
 *
 *	* DMMP_ERR_MPATH_NOT_FOUND
 *
 *	* DMMP_ERR_INVALID_ARGUMENT
 *
 *	* DMMP_ERR_PERMISSION_DENY
 *
 *	Error number could be converted to string by dmmp_strerror().
 */
DMMP_DLL_EXPORT int dmmp_flush_mpath(struct dmmp_context *ctx,
				     const char *mpath_name);

/**
 * dmmp_reconfig() - Instruct multipathd daemon to do reconfiguration.
 *
 * Instruct multipathd daemon to do reconfiguration.
 *
 * @ctx:
 *	Pointer of 'struct dmmp_context'.
 *	If this pointer is NULL, your program will be terminated by assert.
 *
 * Return:
 *	int. Valid error codes are:
 *
 *	* DMMP_OK
 *
 *	* DMMP_ERR_BUG
 *
 *	* DMMP_ERR_NO_MEMORY
 *
 *	* DMMP_ERR_NO_DAEMON
 *
 *	* DMMP_ERR_PERMISSION_DENY
 *
 *	Error number could be converted to string by dmmp_strerror().
 */
DMMP_DLL_EXPORT int dmmp_reconfig(struct dmmp_context *ctx);

/**
 * dmmp_last_error_msg() - Retrieves the last error message.
 *
 * Retrieves the last error message.
 *
 * @ctx:
 *	Pointer of 'struct dmmp_context'.
 *	If this pointer is NULL, your program will be terminated by assert.
 *
 * Return:
 *	const char *. No need to free this memory, the resources will get
 *	freed when dmmp_context_free().
 */
DMMP_DLL_EXPORT const char *dmmp_last_error_msg(struct dmmp_context *ctx);

#ifdef __cplusplus
} /* End of extern "C" */
#endif

#endif /* End of _LIB_DMMP_H_ */
