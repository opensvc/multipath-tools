#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <errno.h>
#include <libudev.h>
#include <mpath_persist.h>
#include "debug.h"
#include "mpath_cmd.h"
#include "uxsock.h"
#include "memory.h"
#include "mpathpr.h"


static int do_update_pr(char *alias, char *arg)
{
	int fd;
	char str[256];
	char *reply;
	int ret = 0;

	fd = mpath_connect();
	if (fd == -1) {
		condlog (0, "ux socket connect error");
		return -1;
	}

	snprintf(str,sizeof(str),"map %s %s", alias, arg);
	condlog (2, "%s: pr message=%s", alias, str);
	if (send_packet(fd, str) != 0) {
		condlog(2, "%s: message=%s send error=%d", alias, str, errno);
		mpath_disconnect(fd);
		return -1;
	}
	ret = recv_packet(fd, &reply, DEFAULT_REPLY_TIMEOUT);
	if (ret < 0) {
		condlog(2, "%s: message=%s recv error=%d", alias, str, errno);
		ret = -1;
	} else {
		condlog (2, "%s: message=%s reply=%s", alias, str, reply);
		if (reply && strncmp(reply,"ok", 2) == 0)
			ret = 0;
		else
			ret = -1;
	}

	free(reply);
	mpath_disconnect(fd);
	return ret;
}

int update_prflag(char *mapname, int set) {
	return do_update_pr(mapname, (set)? "setprstatus" : "unsetprstatus");
}

int update_prkey_flags(char *mapname, uint64_t prkey, uint8_t sa_flags) {
	char str[256];
	char *flagstr = "";

	if (sa_flags & MPATH_F_APTPL_MASK)
		flagstr = ":aptpl";
	if (prkey)
		sprintf(str, "setprkey key %" PRIx64 "%s", prkey, flagstr);
	else
		sprintf(str, "unsetprkey");
	return do_update_pr(mapname, str);
}
