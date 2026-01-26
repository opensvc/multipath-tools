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
#include "strbuf.h"

static char *do_pr(char *alias, const char *str)
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

	condlog(4, "%s: pr message=%s", alias, str);
	if (send_packet(fd, str) != 0) {
		condlog(2, "%s: message=%s send error=%d", alias, str, errno);
		mpath_disconnect(fd);
		return NULL;
	}
	if (recv_packet(fd, &reply, timeout) < 0)
		condlog(0, "%s: message=%s recv error=%d", alias, str, errno);

	mpath_disconnect(fd);
	return reply;
}

static int do_update_pr(char *alias, char *cmd, const char *data)
{
	STRBUF_ON_STACK(buf);
	char *reply = NULL;
	int ret = -1;

	if (data)
		print_strbuf(&buf, "%s map %s %s %s", cmd, alias,
			     strcmp(cmd, "setprkey") ? "pathlist" : "key", data);
	else
		print_strbuf(&buf, "%s map %s", cmd, alias);

	reply = do_pr(alias, get_strbuf_str(&buf));
	if (reply) {
		if (strncmp(reply, "ok", 2) == 0)
			ret = 0;
		else
			ret = -1;
		condlog(ret ? 0 : 4, "%s: message=%s reply=%s", alias,
			get_strbuf_str(&buf), reply);
	}

	free(reply);
	return ret;
}

static int do_get_pr(char *mapname, const char *cmd)
{
	char str[256];
	char *reply;
	int prflag;

	snprintf(str, sizeof(str), "%s map %s", cmd, mapname);
	reply = do_pr(mapname, str);
	if (!reply)
		prflag = PR_UNKNOWN;
	else if (strncmp(reply, "unset", 5) == 0)
		prflag = PR_UNSET;
	else if (strncmp(reply, "set", 3) == 0)
		prflag = PR_SET;
	else
		prflag = PR_UNKNOWN;

	free(reply);
	return prflag;
}

int get_prflag(char *mapname)
{
	return do_get_pr(mapname, "getprstatus");
}

int get_prhold(char *mapname)
{
	return do_get_pr(mapname, "getprhold");
}

int update_prflag(struct multipath *mpp, int set)
{
	STRBUF_ON_STACK(buf);
	int i, j;
	bool first = true;
	struct pathgroup *pgp = NULL;
	struct path *pp = NULL;

	if (!set)
		return do_update_pr(mpp->alias, "unsetprstatus", NULL);

	append_strbuf_str(&buf, "\"");
	vector_foreach_slot (mpp->pg, pgp, j) {
		vector_foreach_slot (pgp->paths, pp, i) {
			if (pp->state == PATH_UP || pp->state == PATH_GHOST) {
				if (first) {
					append_strbuf_str(&buf, pp->dev);
					first = false;
				} else
					print_strbuf(&buf, " %s", pp->dev_t);
			}
		}
	}
	append_strbuf_str(&buf, "\"");
	return do_update_pr(mpp->alias, "setprstatus", get_strbuf_str(&buf));
}

int update_prhold(char *mapname, bool set)
{
	return do_update_pr(mapname, (set) ? "setprhold" : "unsetprhold", NULL);
}

int update_prkey_flags(char *mapname, uint64_t prkey, uint8_t sa_flags) {
	char str[256];

	if (!prkey)
		return do_update_pr(mapname, "unsetprkey", NULL);
	sprintf(str, "%" PRIx64 "%s", prkey,
		(sa_flags & MPATH_F_APTPL_MASK) ? ":aptpl" : "");
	return do_update_pr(mapname, "setprkey", str);
}
