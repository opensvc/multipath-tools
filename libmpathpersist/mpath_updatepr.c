#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
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
#include "vector.h"
#include "globals.h"
#include "config.h"
#include "uxsock.h"
#include "mpathpr.h"
#include "structs.h"

static char *do_pr(char *alias, char *str)
{
	int fd;
	char *reply;
	int timeout;
	struct config *conf;

	conf = get_multipath_config();
	timeout = conf->uxsock_timeout;
	put_multipath_config(conf);

	fd = mpath_connect();
	if (fd == -1) {
		condlog (0, "ux socket connect error");
		return NULL;
	}

	condlog (2, "%s: pr message=%s", alias, str);
	if (send_packet(fd, str) != 0) {
		condlog(2, "%s: message=%s send error=%d", alias, str, errno);
		mpath_disconnect(fd);
		return NULL;
	}
	if (recv_packet(fd, &reply, timeout) < 0)
		condlog(2, "%s: message=%s recv error=%d", alias, str, errno);

	mpath_disconnect(fd);
	return reply;
}

static int do_update_pr(char *alias, char *cmd, char *key)
{
	char str[256];
	char *reply = NULL;
	int ret = -1;

	if (key)
		snprintf(str, sizeof(str), "%s map %s key %s", cmd, alias, key);
	else
		snprintf(str, sizeof(str), "%s map %s", cmd, alias);

	reply = do_pr(alias, str);
	if (reply) {
		condlog (2, "%s: message=%s reply=%s", alias, str, reply);
		if (reply && strncmp(reply,"ok", 2) == 0)
			ret = 0;
		else
			ret = -1;
	}

	free(reply);
	return ret;
}

int get_prflag(char *mapname)
{
	char str[256];
	char *reply;
	int prflag;

	snprintf(str, sizeof(str), "getprstatus map %s", mapname);
	reply = do_pr(mapname, str);
	if (!reply)
		prflag = PRFLAG_UNKNOWN;
	else if (strncmp(reply, "unset", 5) == 0)
		prflag = PRFLAG_UNSET;
	else if (strncmp(reply, "set", 3) == 0)
		prflag = PRFLAG_SET;
	else
		prflag = PRFLAG_UNKNOWN;

	free(reply);
	return prflag;
}

int update_prflag(char *mapname, int set) {
	return do_update_pr(mapname, (set)? "setprstatus" : "unsetprstatus",
			    NULL);
}

int update_prkey_flags(char *mapname, uint64_t prkey, uint8_t sa_flags) {
	char str[256];

	if (!prkey)
		return do_update_pr(mapname, "unsetprkey", NULL);
	sprintf(str, "%" PRIx64 "%s", prkey,
		(sa_flags & MPATH_F_APTPL_MASK) ? ":aptpl" : "");
	return do_update_pr(mapname, "setprkey", str);
}
