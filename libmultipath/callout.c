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
#include <sys/wait.h>
#include <errno.h>

#define PROGRAM_SIZE	100
#define FIELD_PROGRAM

#define strfieldcpy(to, from) \
do { \
	to[sizeof(to)-1] = '\0'; \
	strncpy(to, from, sizeof(to)-1); \
} while (0)

int execute_program(char *path, char *value, int len)
{
	int retval;
	int count;
	int status;
	int fds[2];
	pid_t pid;
	char *pos;
	char arg[PROGRAM_SIZE];
	char *argv[sizeof(arg) / 2];
	int i;

	i = 0;

	if (strchr(path, ' ')) {
		strfieldcpy(arg, path);
		pos = arg;
		while (pos != NULL) {
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

	if (retval != 0)
		return -1;


	pid = fork();

	switch(pid) {
	case 0:
		/* child */
		close(STDOUT_FILENO);

		/* dup write side of pipe to STDOUT */
		dup(fds[1]);

		retval = execv(argv[0], argv);

		exit(-1);
	case -1:
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
				retval = -1;
				break;
			}
		}

		if (count < 0)
			retval = -1;

		if (i > 0 && value[i-1] == '\n')
			i--;
		value[i] = '\0';

		wait(&status);
		close(fds[0]);

		if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0))
			retval = -1;
	}
	return retval;
}
