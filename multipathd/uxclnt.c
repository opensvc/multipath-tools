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
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/poll.h>
#include <readline/readline.h>
#include <readline/history.h>

#include <uxsock.h>
#include <memory.h>
#include <defaults.h>

#include <vector.h>
#include "cli.h"

static void print_reply(char *s)
{
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
static void process(int fd)
{
	char *line;
	char *reply;

	cli_init();
	rl_readline_name = "multipathd";
	rl_completion_entry_function = key_generator;
	while ((line = readline("multipathd> "))) {
		size_t len;
		size_t llen = strlen(line);

		if (!llen) {
			free(line);
			continue;
		}

		if (need_quit(line, llen))
			break;

		if (send_packet(fd, line, llen + 1) != 0) break;
		if (recv_packet(fd, &reply, &len) != 0) break;

		print_reply(reply);

		if (line && *line)
			add_history(line);

		free(line);
		FREE(reply);
	}
}

static void process_req(int fd, char * inbuf)
{
	char *reply;
	size_t len;

	if (send_packet(fd, inbuf, strlen(inbuf) + 1) != 0) {
		printf("cannot send packet\n");
		return;
	}
	if (recv_packet(fd, &reply, &len) != 0)
		printf("error receiving packet\n");
	else {
		printf("%s", reply);
		FREE(reply);
	}
}

/*
 * entry point
 */
int uxclnt(char * inbuf)
{
	int fd;

	fd = ux_socket_connect(DEFAULT_SOCKET);
	if (fd == -1)
		exit(1);

	if (inbuf)
		process_req(fd, inbuf);
	else
		process(fd);

	return 0;
}
