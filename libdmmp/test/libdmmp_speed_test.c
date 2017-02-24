/*
 * Copyright (C) 2015-2016 Red Hat, Inc.
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
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include <libdmmp/libdmmp.h>

int main(int argc, char *argv[])
{
	struct dmmp_context *ctx = NULL;
	struct dmmp_mpath **dmmp_mps = NULL;
	uint32_t dmmp_mp_count = 0;
	int rc = EXIT_SUCCESS;

	ctx = dmmp_context_new();
	dmmp_context_log_priority_set(ctx, DMMP_LOG_PRIORITY_WARNING);

	if (dmmp_mpath_array_get(ctx, &dmmp_mps, &dmmp_mp_count) != 0) {
		printf("FAILED\n");
		rc = EXIT_FAILURE;
	} else {
		printf("Got %" PRIu32 " mpath\n", dmmp_mp_count);
		dmmp_mpath_array_free(dmmp_mps, dmmp_mp_count);
	}
	dmmp_context_free(ctx);
	exit(rc);
}
