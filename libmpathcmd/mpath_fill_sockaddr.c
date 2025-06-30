// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * Copyright (c) 2025 SUSE LLC
 */

static size_t mpath_fill_sockaddr__(struct sockaddr_un *addr, const char *name)
{
	size_t len;

	addr->sun_family = AF_LOCAL;

	if (name[0] != '@') {
		/* Pathname socket. This should be NULL-terminated. */
		strncpy(&addr->sun_path[0], name, sizeof(addr->sun_path) - 1);
		addr->sun_path[sizeof(addr->sun_path) - 1] = '\0';
		len = offsetof(struct sockaddr_un, sun_path) + strlen(name) + 1;
	} else {
		addr->sun_path[0] = '\0';
		/*
		 * The abstract socket's name doesn't need to be NULL terminated.
		 * Actually, a trailing NULL would be considered part of the socket name.
		 */
#pragma GCC diagnostic push
#if WSTRINGOP_TRUNCATION
#pragma GCC diagnostic ignored "-Wstringop-truncation"
#endif
		strncpy(&addr->sun_path[1], &name[1], sizeof(addr->sun_path) - 1);
#pragma GCC diagnostic pop
		len = offsetof(struct sockaddr_un, sun_path) + strlen(name);
	}
	return len > sizeof(*addr) ? sizeof(*addr) : len;
}
