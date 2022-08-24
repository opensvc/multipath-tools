/*
 * Copyright (c) 2022 SUSE LLC
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>

#include "mpath_cmd.h"
#include "uxclnt.h"
#include "vector.h"
#include "uxsock.h"
#include "util.h"
#include "cli.h"

#ifdef USE_LIBEDIT
#include <editline/readline.h>
#endif
#ifdef USE_LIBREADLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif
/*
 * Versions of libedit prior to 2016 were using a wrong
 * prototype for rl_completion_entry_function in readline.h.
 * Internally, libedit casts this to the correct type
 * (char *)(*)(const char *, int).
 * So we simply cast to the wrong prototype here.
 * See http://cvsweb.netbsd.org/bsdweb.cgi/src/lib/libedit/readline/readline.h.diff?r1=1.34&r2=1.35
 * Unfortunately, this change isn't reflected in the libedit version.
 */
#ifdef BROKEN_RL_COMPLETION_FUNC
#define RL_COMP_ENTRY_CAST(x) ((int (*)(const char *, int)) (x))
#else
#define RL_COMP_ENTRY_CAST(x) (x)
#endif

#if defined(USE_LIBREADLINE) || defined(USE_LIBEDIT)
static int
key_match_fingerprint (struct key * kw, uint32_t fp)
{
	if (!fp)
		return 0;

	return ((fp & kw->code) == kw->code);
}

/*
 * This is the readline completion handler
 */
char *
key_generator (const char * str, int state)
{
	static int index, len, has_param;
	static uint32_t rlfp;
	struct key * kw;
	int i;
	struct handler *h;
	vector v = NULL;
	const vector keys = get_keys();
	const vector handlers = get_handlers();

	if (!state) {
		index = 0;
		has_param = 0;
		rlfp = 0;
		len = strlen(str);
		int r = get_cmdvec(rl_line_buffer, &v);
		/*
		 * If a word completion is in progress, we don't want
		 * to take an exact keyword match in the fingerprint.
		 * For ex "show map[tab]" would validate "map" and discard
		 * "maps" as a valid candidate.
		 */
		if (v && len)
			vector_del_slot(v, VECTOR_SIZE(v) - 1);
		/*
		 * Clean up the mess if we dropped the last slot of a 1-slot
		 * vector
		 */
		if (v && !VECTOR_SIZE(v)) {
			vector_free(v);
			v = NULL;
		}
		/*
		 * If last keyword takes a param, don't even try to guess
		 */
		if (r == EINVAL) {
			has_param = 1;
			return (strdup("(value)"));
		}
		/*
		 * Compute a command fingerprint to find out possible completions.
		 * Once done, the vector is useless. Free it.
		 */
		if (v) {
			rlfp = fingerprint(v);
			free_keys(v);
		}
	}
	/*
	 * No more completions for parameter placeholder.
	 * Brave souls might try to add parameter completion by walking paths and
	 * multipaths vectors.
	 */
	if (has_param)
		return ((char *)NULL);
	/*
	 * Loop through keywords for completion candidates
	 */
	vector_foreach_slot_after (keys, kw, index) {
		if (!strncmp(kw->str, str, len)) {
			/*
			 * Discard keywords already in the command line
			 */
			if (key_match_fingerprint(kw, rlfp)) {
				struct key * curkw = find_key(str);
				if (!curkw || (curkw != kw))
					continue;
			}
			/*
			 * Discard keywords making syntax errors.
			 *
			 * nfp is the candidate fingerprint we try to
			 * validate against all known command fingerprints.
			 */
			uint32_t nfp = rlfp | kw->code;
			vector_foreach_slot(handlers, h, i) {
				if (!rlfp || ((h->fingerprint & nfp) == nfp)) {
					/*
					 * At least one full command is
					 * possible with this keyword :
					 * Consider it validated
					 */
					index++;
					return (strdup(kw->str));
				}
			}
		}
	}
	/*
	 * No more candidates
	 */
	return ((char *)NULL);
}
#endif

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

#if defined(USE_LIBREADLINE) || defined(USE_LIBEDIT)
	rl_readline_name = "multipathd";
	rl_completion_entry_function = RL_COMP_ENTRY_CAST(key_generator);
#endif

	cli_init();
	for(;;)
	{
		char *line __attribute__((cleanup(cleanup_charp))) = NULL;
		char *reply __attribute__((cleanup(cleanup_charp))) = NULL;
		ssize_t llen;
		int ret;

#if defined(USE_LIBREADLINE) || defined(USE_LIBEDIT)
		line = readline("multipathd> ");
		if (!line)
			break;
		llen = strlen(line);
		if (!llen)
			continue;
#else
		size_t lsize = 0;

		fputs("multipathd> ", stdout);
		errno = 0;
		llen = getline(&line, &lsize, stdin);
		if (llen == -1) {
			if (errno != 0)
				fprintf(stderr, "Error in getline: %m");
			break;
		}
		if (!llen || !strcmp(line, "\n"))
			continue;
#endif

		if (need_quit(line, llen))
			break;

		if (send_packet(fd, line) != 0)
			break;
		ret = recv_packet(fd, &reply, timeout);
		if (ret != 0)
			break;

		print_reply(reply);

#if defined(USE_LIBREADLINE) || defined(USE_LIBEDIT)
		if (line && *line)
			add_history(line);
#endif
	}
}

int main (int argc, const char * const argv[])
{
	int fd;
	int tmo = DEFAULT_REPLY_TIMEOUT + 100;
	char *ep;

	if (argc > 2) {
		fprintf(stderr, "Usage: %s [timeout]\n", argv[0]);
		return 1;
	}
	if (argc == 2) {
		tmo = strtol(argv[1], &ep, 10);
		if (*argv[1] == '\0' || *ep != '\0' || tmo < 0) {
			fprintf(stderr, "ERROR: invalid timeout value\n");
			return 1;
		}
	}

	fd = mpath_connect();
	if (fd == -1) {
		fprintf(stderr, "ERROR: failed to connect to multipathd\n");
		return 1;
	}

	process(fd, tmo);
	mpath_disconnect(fd);
	return 0;
}

#define HANDLER(x) NULL
#include "callbacks.c"
