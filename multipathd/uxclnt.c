/*
 * Original author : tridge@samba.org, January 2002
 *
 * Copyright (c) 2005 Christophe Varoqui
 * Copyright (c) 2005 Benjamin Marzinski, Redhat
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "mpath_cmd.h"
#include "uxsock.h"
#include "uxclnt.h"

static int process_req(int fd, char * inbuf, unsigned int timeout)
{
	char *reply;
	int ret;

	if (send_packet(fd, inbuf) != 0) {
		printf("cannot send packet\n");
		return 1;
	}
	ret = recv_packet(fd, &reply, timeout);
	if (ret < 0) {
		if (ret == -ETIMEDOUT)
			printf("timeout receiving packet\n");
		else
			printf("error %d receiving packet\n", ret);
		return 1;
	} else {
		printf("%s", reply);
		ret = (strcmp(reply, "fail\n") == 0);
		free(reply);
		return ret;
	}
}

/*
 * entry point
 */
int uxclnt(char * inbuf, unsigned int timeout)
{
	int fd, ret = 0;

	if (!inbuf)
		return 1;
	fd = mpath_connect();
	if (fd == -1)
		return 1;

	ret = process_req(fd, inbuf, timeout);

	mpath_disconnect(fd);
	return ret;
}
