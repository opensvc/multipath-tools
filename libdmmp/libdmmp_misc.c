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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include <assert.h>
#include <json.h>

#include "libdmmp/libdmmp.h"
#include "libdmmp_private.h"

#define _DMMP_LOG_STRERR_ALIGN_WIDTH	80
/* ^ Only used in _dmmp_log_stderr() for pretty log output.
 *   When provided log message is less than 80 bytes, fill it with space, then
 *   print code file name, function name, line after the 80th bytes.
 */

static const struct _num_str_conv _DMMP_RC_MSG_CONV[] = {
	{DMMP_OK, "OK"},
	{DMMP_ERR_NO_MEMORY, "Out of memory"},
	{DMMP_ERR_BUG, "BUG of libdmmp library"},
	{DMMP_ERR_IPC_TIMEOUT, "Timeout when communicate with multipathd, "
			       "try to increase it via "
				"dmmp_context_timeout_set()"},
	{DMMP_ERR_IPC_ERROR, "Error when communicate with multipathd daemon"},
	{DMMP_ERR_NO_DAEMON, "The multipathd daemon not started"},
	{DMMP_ERR_INCOMPATIBLE, "Incompatible multipathd daemon version"},
	{DMMP_ERR_MPATH_BUSY, "Specified multipath device map is in use"},
	{DMMP_ERR_MPATH_NOT_FOUND, "Specified multipath not found"},
	{DMMP_ERR_INVALID_ARGUMENT, "Invalid argument"},
	{DMMP_ERR_PERMISSION_DENY, "Permission deny"},
};

_dmmp_str_func_gen(dmmp_strerror, int, rc, _DMMP_RC_MSG_CONV);

static const struct _num_str_conv _DMMP_PRI_CONV[] = {
	{DMMP_LOG_PRIORITY_DEBUG, "DEBUG"},
	{DMMP_LOG_PRIORITY_INFO, "INFO"},
	{DMMP_LOG_PRIORITY_WARNING, "WARNING"},
	{DMMP_LOG_PRIORITY_ERROR, "ERROR"},
};
_dmmp_str_func_gen(dmmp_log_priority_str, int, priority, _DMMP_PRI_CONV);

void _dmmp_log_stderr(struct dmmp_context *ctx, int priority,
		      const char *file, int line, const char *func_name,
		      const char *format, va_list args)
{
	int printed_bytes = 0;
	void *userdata = NULL;

	printed_bytes += fprintf(stderr, "libdmmp %s: ",
				 dmmp_log_priority_str(priority));
	printed_bytes += vfprintf(stderr, format, args);

	userdata = dmmp_context_userdata_get(ctx);
	if (userdata != NULL)
		fprintf(stderr, "(userdata address: %p)",
			userdata);
	/* ^ Just demonstrate how userdata could be used and
	 *   bypass clang static analyzer about unused ctx argument warning
	 */

	if (printed_bytes < _DMMP_LOG_STRERR_ALIGN_WIDTH) {
		fprintf(stderr, "%*s # %s:%s():%d\n",
			_DMMP_LOG_STRERR_ALIGN_WIDTH - printed_bytes, "", file,
			func_name, line);
	} else {
		fprintf(stderr, " # %s:%s():%d\n", file, func_name, line);
	}
}
