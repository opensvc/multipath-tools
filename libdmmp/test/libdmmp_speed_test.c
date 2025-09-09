// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2015-2016 Red Hat, Inc.
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

int main(void)
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
