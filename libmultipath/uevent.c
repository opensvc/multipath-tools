/*
 * uevent.c - trigger upon netlink uevents from the kernel
 *
 *	Only kernels from version 2.6.10* on provide the uevent netlink socket.
 *	Until the libc-kernel-headers are updated, you need to compile with:
 *
 *	  gcc -I /lib/modules/`uname -r`/build/include -o uevent_listen uevent_listen.c
 *
 * Copyright (C) 2004 Kay Sievers <kay.sievers@vrfy.org>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation version 2 of the License.
 *
 *	This program is distributed in the hope that it will be useful, but
 *	WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *	General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License along
 *	with this program; if not, write to the Free Software Foundation, Inc.,
 *	675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/user.h>
#include <asm/types.h>
#include <linux/netlink.h>

#include "memory.h"
#include "debug.h"
#include "uevent.h"

struct uevent * alloc_uevent (void)
{
	return (struct uevent *)MALLOC(sizeof(struct uevent));
}

int uevent_listen(int (*uev_trigger)(struct uevent *, void * trigger_data),
		  void * trigger_data)
{
	int sock;
	struct sockaddr_nl snl;
	int retval;
	int rcvbufsz = 128*1024;
	int rcvsz = 0;
	int rcvszsz = sizeof(rcvsz);
	unsigned int *prcvszsz = (unsigned int *)&rcvszsz;

	memset(&snl, 0x00, sizeof(struct sockaddr_nl));
	snl.nl_family = AF_NETLINK;
	snl.nl_pid = getpid();
	snl.nl_groups = 0xffffffff;

	sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
	if (sock == -1) {
		condlog(0, "error getting socket, exit\n");
		return 1;
	}

	/*
	 * try to avoid dropping uevents, even so, this is not a guarantee,
	 * but it does help to change the netlink uevent socket's
	 * receive buffer threshold from the default value of 106,496 to
	 * the maximum value of 262,142.
	 */
	retval = setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbufsz,
			    sizeof(rcvbufsz));

	if (retval < 0) {
		condlog(0, "error setting receive buffer size for socket, exit\n");
		exit(1);
	}
	retval = getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvsz, prcvszsz);

	if (retval < 0) {
		condlog(0, "error setting receive buffer size for socket, exit\n");
		exit(1);
	}
	condlog(3, "receive buffer size for socket is %u.\n", rcvsz);

	retval = bind(sock, (struct sockaddr *) &snl,
		      sizeof(struct sockaddr_nl));
	if (retval < 0) {
		condlog(0, "bind failed, exit\n");
		goto exit;
	}

	while (1) {
		static char buffer[HOTPLUG_BUFFER_SIZE + OBJECT_SIZE];
		int i;
		char *pos;
		size_t bufpos;
		ssize_t buflen;
		struct uevent *uev;

		buflen = recv(sock, &buffer, sizeof(buffer), 0);
		if (buflen <  0) {
			condlog(0, "error receiving message\n");
			continue;
		}

		if ((size_t)buflen > sizeof(buffer)-1)
			buflen = sizeof(buffer)-1;

		buffer[buflen] = '\0';
		uev = alloc_uevent();

		if (!uev) {
			condlog(1, "lost uevent, oom");
			continue;
		}

		/* save start of payload */
		bufpos = strlen(buffer) + 1;

		/* action string */
		uev->action = buffer;
		pos = strchr(buffer, '@');
		if (!pos)
			continue;
		pos[0] = '\0';

		/* sysfs path */
		uev->devpath = &pos[1];

		/* hotplug events have the environment attached - reconstruct envp[] */
		for (i = 0; (bufpos < (size_t)buflen) && (i < HOTPLUG_NUM_ENVP-1); i++) {
			int keylen;
			char *key;

			key = &buffer[bufpos];
			keylen = strlen(key);
			uev->envp[i] = key;
			bufpos += keylen + 1;
		}
		uev->envp[i] = NULL;

		condlog(3, "uevent '%s' from '%s'\n", uev->action, uev->devpath);

		/* print payload environment */
		for (i = 0; uev->envp[i] != NULL; i++)
			condlog(3, "%s\n", uev->envp[i]);

		if (uev_trigger && uev_trigger(uev, trigger_data))
			condlog(0, "uevent trigger error");

		FREE(uev);
	}

exit:
	close(sock);
	return 1;
}
