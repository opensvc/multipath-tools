// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (c) 2022 SUSE LLC
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
#include "debug.h"

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
/*
 * This is the readline completion handler
 */
char *
key_generator (const char * str, int state)
{
	static vector completions;
	static int index;
	char *word;

	if (!state) {
		uint32_t rlfp = 0, mask = 0;
		int len = strlen(str), vlen = 0, i, j;
		struct key * kw;
		struct handler *h;
		vector handlers = get_handlers();
		vector keys = get_keys();
		vector v = NULL;
		int r = get_cmdvec(rl_line_buffer, &v, true);

		index = 0;
		if (completions)
			vector_free(completions);

		completions = vector_alloc();

		if (!completions || r == ENOMEM) {
			if (v)
				vector_free(v);
			return NULL;
		}

		/*
		 * Special case: get_cmdvec() ignores trailing whitespace,
		 * readline doesn't. get_cmdvec() will return "[show]" and
		 * ESRCH for both "show bogus\t" and "show bogus \t".
		 * The former case will fail below. In the latter case,
		 * We shouldn't offer completions.
		 */
		if (r == ESRCH && !len)
			r = ENOENT;

		/*
		 * If a word completion is in progress, we don't want
		 * to take an exact keyword match in the fingerprint.
		 * For ex "show map[tab]" would validate "map" and discard
		 * "maps" as a valid candidate.
		 */
		if (r != ESRCH && VECTOR_SIZE(v) && len) {
			kw = VECTOR_SLOT(v, VECTOR_SIZE(v) - 1);
			/*
			 * If kw->param is set, we were already parsing a
			 * parameter, not the keyword. Don't delete it.
			 */
			if (!kw->param) {
				free_key(kw);
				vector_del_slot(v, VECTOR_SIZE(v) - 1);
				if (r == EINVAL)
					r = 0;
			}
		}

		/*
		 * Clean up the mess if we dropped the last slot of a 1-slot
		 * vector
		 */
		if (v && !VECTOR_SIZE(v)) {
			vector_free(v);
			v = NULL;
		}

		/*
		 * Compute a command fingerprint to find out possible completions.
		 * Once done, the vector is useless. Free it.
		 */
		if (v) {
			rlfp = fingerprint(v);
			vlen = VECTOR_SIZE(v);
			if (vlen >= 4)
				mask = ~0;
			else
				mask = (uint32_t)(1U << (8 * vlen)) - 1;
			free_keys(v);
		}
		condlog(4, "%s: line=\"%s\" str=\"%s\" r=%d fp=%08x mask=%08x",
			__func__, rl_line_buffer, str, r, rlfp, mask);

		/*
		 * If last keyword takes a param, don't even try to guess
		 * Brave souls might try to add parameter completion by walking
		 * paths and multipaths vectors.
		 */
		if (r == EINVAL) {
			if (len == 0 && vector_alloc_slot(completions))
				vector_set_slot(completions,
						strdup("VALUE"));

			goto init_done;
		}

		if (r == ENOENT)
			goto init_done;

		vector_foreach_slot(handlers, h, i) {
			uint8_t code;

			if (rlfp != (h->fingerprint & mask))
				continue;

			if (vlen >= 4)
				/*
				 * => mask == ~0 => rlfp == h->fingerprint
				 * Complete command. This must be the only match.
				 */
				goto init_done;
			else if (rlfp == h->fingerprint && r != ESRCH &&
				 !strcmp(str, "") &&
				 vector_alloc_slot(completions))
				/* just completed */
				vector_set_slot(completions, strdup(""));
			else {
				/* vlen must be 1, 2, or 3 */
				code = (h->fingerprint >> vlen * 8);

				if (code == KEY_INVALID)
					continue;

				vector_foreach_slot(keys, kw, j) {
					if (kw->code != code ||
					    strncmp(kw->str, str, len))
						continue;
					if (vector_alloc_slot(completions))
						vector_set_slot(completions,
								strdup(kw->str));
				}
			}

		}
		vector_foreach_slot(completions, word, i)
			condlog(4, "%s: %d -> \"%s\"", __func__, i, word);

	}

init_done:
	vector_foreach_slot_after(completions, word, index) {
		index++;
		return word;
	}

	return NULL;
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
