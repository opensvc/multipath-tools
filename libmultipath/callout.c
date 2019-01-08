/*
 * Source: copy of the udev package source file
 *
 * Copyrights of the source file apply
 * Copyright (c) 2004 Christophe Varoqui
 */
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>

#include "checkers.h"
#include "vector.h"
#include "structs.h"
#include "util.h"
#include "callout.h"
#include "debug.h"

int execute_program(char *path, char *value, int len)
{
	int retval;
	int count;
	int status;
	int fds[2], null_fd;
	pid_t pid;
	char *pos;
	char arg[CALLOUT_MAX_SIZE];
	int argc = sizeof(arg) / 2;
	char *argv[argc + 1];
	int i;

	i = 0;

	if (strchr(path, ' ')) {
		strlcpy(arg, path, sizeof(arg));
		pos = arg;
		while (pos != NULL && i < argc) {
			if (pos[0] == '\'') {
				/* don't separate if in apostrophes */
				pos++;
				argv[i] = strsep(&pos, "\'");
				while (pos[0] == ' ')
					pos++;
			} else {
				argv[i] = strsep(&pos, " ");
			}
			i++;
		}
	} else {
		argv[i++] = path;
	}
	argv[i] =  NULL;

	retval = pipe(fds);

	if (retval != 0) {
		condlog(0, "error creating pipe for callout: %s", strerror(errno));
		return -1;
	}

	pid = fork();

	switch(pid) {
	case 0:
		/* child */

		/* dup write side of pipe to STDOUT */
		if (dup2(fds[1], STDOUT_FILENO) < 0) {
			condlog(1, "failed to dup2 stdout: %m");
			return -1;
		}
		close(fds[0]);
		close(fds[1]);

		/* Ignore writes to stderr */
		null_fd = open("/dev/null", O_WRONLY);
		if (null_fd > 0) {
			if (dup2(null_fd, STDERR_FILENO) < 0)
				condlog(1, "failed to dup2 stderr: %m");
			close(null_fd);
		}

		retval = execv(argv[0], argv);
		condlog(0, "error execing %s : %s", argv[0], strerror(errno));
		exit(-1);
	case -1:
		condlog(0, "fork failed: %s", strerror(errno));
		close(fds[0]);
		close(fds[1]);
		return -1;
	default:
		/* parent reads from fds[0] */
		close(fds[1]);
		retval = 0;
		i = 0;
		while (1) {
			count = read(fds[0], value + i, len - i-1);
			if (count <= 0)
				break;

			i += count;
			if (i >= len-1) {
				condlog(0, "not enough space for response from %s", argv[0]);
				retval = -1;
				break;
			}
		}

		if (count < 0) {
			condlog(0, "no response from %s", argv[0]);
			retval = -1;
		}

		if (i > 0 && value[i-1] == '\n')
			i--;
		value[i] = '\0';

		wait(&status);
		close(fds[0]);

		retval = -1;
		if (WIFEXITED(status)) {
			status = WEXITSTATUS(status);
			if (status == 0)
				retval = 0;
			else
				condlog(0, "%s exited with %d", argv[0], status);
		}
		else if (WIFSIGNALED(status))
			condlog(0, "%s was terminated by signal %d", argv[0], WTERMSIG(status));
		else
			condlog(0, "%s terminated abnormally", argv[0]);
	}
	return retval;
}

int apply_format(char * string, char * cmd, struct path * pp)
{
	char * pos;
	char * dst;
	char * p;
	char * q;
	int len;
	int myfree;

	if (!string)
		return 1;

	if (!cmd)
		return 1;

	dst = cmd;
	p = dst;
	pos = strchr(string, '%');
	myfree = CALLOUT_MAX_SIZE;

	if (!pos) {
		strcpy(dst, string);
		return 0;
	}

	len = (int) (pos - string) + 1;
	myfree -= len;

	if (myfree < 2)
		return 1;

	snprintf(p, len, "%s", string);
	p += len - 1;
	pos++;

	switch (*pos) {
	case 'n':
		len = strlen(pp->dev) + 1;
		myfree -= len;

		if (myfree < 2)
			return 1;

		snprintf(p, len, "%s", pp->dev);
		for (q = p; q < p + len; q++) {
			if (q && *q == '!')
				*q = '/';
		}
		p += len - 1;
		break;
	case 'd':
		len = strlen(pp->dev_t) + 1;
		myfree -= len;

		if (myfree < 2)
			return 1;

		snprintf(p, len, "%s", pp->dev_t);
		p += len - 1;
		break;
	default:
		break;
	}
	pos++;

	if (!*pos) {
		condlog(3, "formatted callout = %s", dst);
		return 0;
	}

	len = strlen(pos) + 1;
	myfree -= len;

	if (myfree < 2)
		return 1;

	snprintf(p, len, "%s", pos);
	condlog(3, "reformatted callout = %s", dst);
	return 0;
}
