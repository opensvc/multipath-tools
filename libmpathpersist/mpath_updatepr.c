#include<stdio.h>
#include<unistd.h>
#include <errno.h>

#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/poll.h>
#include <errno.h>
#include <debug.h>
#include "memory.h"
#include "../libmultipath/uxsock.h"
#include "../libmultipath/defaults.h"

unsigned long mem_allocated;    /* Total memory used in Bytes */

int update_prflag(char * arg1, char * arg2, int noisy)
{
	int fd;
	char str[64];
	char *reply;
	size_t len;
	int ret = 0;

	fd = ux_socket_connect(DEFAULT_SOCKET);
	if (fd == -1) {
		condlog (0, "ux socket connect error");
		return 1 ;
	}

	snprintf(str,sizeof(str),"map %s %s", arg1, arg2);
	condlog (2, "%s: pr flag message=%s", arg1, str);
	send_packet(fd, str, strlen(str) + 1);
	recv_packet(fd, &reply, &len);

	condlog (2, "%s: message=%s reply=%s", arg1, str, reply);
	if (!reply || strncmp(reply,"ok", 2) == 0)
		ret = -1;
	else if (strncmp(reply, "fail", 4) == 0)
		ret = -2;
	else{
		ret = atoi(reply);
	}

	free(reply);
	return ret;
}
