/*
 * Original author : tridge@samba.org, January 2002
 *
 * Copyright (c) 2005 Christophe Varoqui
 * Copyright (c) 2005 Benjamin Marzinski, Redhat
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "mpath_cmd.h"
#include "uxsock.h"
#include "memory.h"
#include "defaults.h"

#include "vector.h"
#include "cli.h"
#include "uxclnt.h"

static void print_reply(char *s)
{
	if (!s)
		return;

	if (isatty(1)) {
		printf("%s", s);
		return;
	}
	/* strip ANSI color markers */
	while (*s != '\0') {
		if ((*s == 0x1b) && (*(s+1) == '['))
			while ((*s++ != 'm') && (*s != '\0')) {};
		putchar(*s++);
	}
}

static int need_quit(char *str, size_t len)
{
	char *ptr, *start;
	size_t trimed_len = len;

	for (ptr = str; trimed_len && isspace(*ptr);
	     trimed_len--, ptr++)
		;

	start = ptr;

	for (ptr = str + len - 1; trimed_len && isspace(*ptr);
	     trimed_len--, ptr--)
		;

	if ((trimed_len == 4 && !strncmp(start, "exit", 4)) ||
	    (trimed_len == 4 && !strncmp(start, "quit", 4)))
		return 1;

	return 0;
}

/*
 * process the client
 */
static void process(int fd, unsigned int timeout)
{
	char *line;
	char *reply;
	int ret;

	cli_init();
	rl_readline_name = "multipathd";
	rl_completion_entry_function = key_generator;
	while ((line = readline("multipathd> "))) {
		size_t llen = strlen(line);

		if (!llen) {
			free(line);
			continue;
		}

		if (need_quit(line, llen))
			break;

		if (send_packet(fd, line) != 0) break;
		ret = recv_packet(fd, &reply, timeout);
		if (ret != 0) break;

		print_reply(reply);

		if (line && *line)
			add_history(line);

		free(line);
		FREE(reply);
	}
}

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
		FREE(reply);
		return ret;
	}
}

/*
 * entry point
 */
int uxclnt(char * inbuf, unsigned int timeout)
{
	int fd, ret = 0;

	fd = mpath_connect();
	if (fd == -1)
		exit(1);

	if (inbuf)
		ret = process_req(fd, inbuf, timeout);
	else
		process(fd, timeout);
	mpath_disconnect(fd);
	return ret;
}
