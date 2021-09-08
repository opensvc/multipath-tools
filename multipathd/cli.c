/*
 * Copyright (c) 2005 Christophe Varoqui
 */
#include <sys/time.h>
#include <errno.h>
#include <pthread.h>
#include <assert.h>
#include "vector.h"
#include "structs.h"
#include "structs_vec.h"
#include "parser.h"
#include "util.h"
#include "version.h"
#include <readline/readline.h>

#include "mpath_cmd.h"
#include "cli.h"
#include "debug.h"
#include "strbuf.h"

static vector keys;
static vector handlers;

static struct key *
alloc_key (void)
{
	return (struct key *)calloc(1, sizeof(struct key));
}

static struct handler *
alloc_handler (void)
{
	return (struct handler *)calloc(1, sizeof(struct handler));
}

static int
add_key (vector vec, char * str, uint64_t code, int has_param)
{
	struct key * kw;

	kw = alloc_key();

	if (!kw)
		return 1;

	kw->code = code;
	kw->has_param = has_param;
	kw->str = strdup(str);

	if (!kw->str)
		goto out;

	if (!vector_alloc_slot(vec))
		goto out1;

	vector_set_slot(vec, kw);

	return 0;

out1:
	free(kw->str);
out:
	free(kw);
	return 1;
}

static struct handler *add_handler(uint64_t fp, cli_handler *fn, bool locked)
{
	struct handler * h;

	h = alloc_handler();

	if (h == NULL)
		return NULL;

	if (!vector_alloc_slot(handlers)) {
		free(h);
		return NULL;
	}

	vector_set_slot(handlers, h);
	h->fingerprint = fp;
	h->fn = fn;
	h->locked = locked;

	return h;
}

static struct handler *
find_handler (uint64_t fp)
{
	int i;
	struct handler *h;

	vector_foreach_slot (handlers, h, i)
		if (h->fingerprint == fp)
			return h;

	return NULL;
}

int
__set_handler_callback (uint64_t fp, cli_handler *fn, bool locked)
{
	struct handler *h;

	assert(find_handler(fp) == NULL);
	h = add_handler(fp, fn, locked);
	if (!h) {
		condlog(0, "%s: failed to set handler for code %"PRIu64,
			__func__, fp);
		return 1;
	}
	return 0;
}

static void
free_key (struct key * kw)
{
	if (kw->str)
		free(kw->str);

	if (kw->param)
		free(kw->param);

	free(kw);
}

void
free_keys (vector vec)
{
	int i;
	struct key * kw;

	vector_foreach_slot (vec, kw, i)
		free_key(kw);

	vector_free(vec);
}

void
free_handlers (void)
{
	int i;
	struct handler * h;

	vector_foreach_slot (handlers, h, i)
		free(h);

	vector_free(handlers);
	handlers = NULL;
}

int
load_keys (void)
{
	int r = 0;
	keys = vector_alloc();

	if (!keys)
		return 1;

	r += add_key(keys, "list", LIST, 0);
	r += add_key(keys, "show", LIST, 0);
	r += add_key(keys, "add", ADD, 0);
	r += add_key(keys, "remove", DEL, 0);
	r += add_key(keys, "del", DEL, 0);
	r += add_key(keys, "switch", SWITCH, 0);
	r += add_key(keys, "switchgroup", SWITCH, 0);
	r += add_key(keys, "suspend", SUSPEND, 0);
	r += add_key(keys, "resume", RESUME, 0);
	r += add_key(keys, "reinstate", REINSTATE, 0);
	r += add_key(keys, "fail", FAIL, 0);
	r += add_key(keys, "resize", RESIZE, 0);
	r += add_key(keys, "reset", RESET, 0);
	r += add_key(keys, "reload", RELOAD, 0);
	r += add_key(keys, "forcequeueing", FORCEQ, 0);
	r += add_key(keys, "disablequeueing", DISABLEQ, 0);
	r += add_key(keys, "restorequeueing", RESTOREQ, 0);
	r += add_key(keys, "paths", PATHS, 0);
	r += add_key(keys, "maps", MAPS, 0);
	r += add_key(keys, "multipaths", MAPS, 0);
	r += add_key(keys, "path", PATH, 1);
	r += add_key(keys, "map", MAP, 1);
	r += add_key(keys, "multipath", MAP, 1);
	r += add_key(keys, "group", GROUP, 1);
	r += add_key(keys, "reconfigure", RECONFIGURE, 0);
	r += add_key(keys, "daemon", DAEMON, 0);
	r += add_key(keys, "status", STATUS, 0);
	r += add_key(keys, "stats", STATS, 0);
	r += add_key(keys, "topology", TOPOLOGY, 0);
	r += add_key(keys, "config", CONFIG, 0);
	r += add_key(keys, "blacklist", BLACKLIST, 0);
	r += add_key(keys, "devices", DEVICES, 0);
	r += add_key(keys, "raw", RAW, 0);
	r += add_key(keys, "wildcards", WILDCARDS, 0);
	r += add_key(keys, "quit", QUIT, 0);
	r += add_key(keys, "exit", QUIT, 0);
	r += add_key(keys, "shutdown", SHUTDOWN, 0);
	r += add_key(keys, "getprstatus", GETPRSTATUS, 0);
	r += add_key(keys, "setprstatus", SETPRSTATUS, 0);
	r += add_key(keys, "unsetprstatus", UNSETPRSTATUS, 0);
	r += add_key(keys, "format", FMT, 1);
	r += add_key(keys, "json", JSON, 0);
	r += add_key(keys, "getprkey", GETPRKEY, 0);
	r += add_key(keys, "setprkey", SETPRKEY, 0);
	r += add_key(keys, "unsetprkey", UNSETPRKEY, 0);
	r += add_key(keys, "key", KEY, 1);
	r += add_key(keys, "local", LOCAL, 0);
	r += add_key(keys, "setmarginal", SETMARGINAL, 0);
	r += add_key(keys, "unsetmarginal", UNSETMARGINAL, 0);


	if (r) {
		free_keys(keys);
		keys = NULL;
		return 1;
	}
	return 0;
}

static struct key *
find_key (const char * str)
{
	int i;
	int len, klen;
	struct key * kw = NULL;
	struct key * foundkw = NULL;

	len = strlen(str);

	vector_foreach_slot (keys, kw, i) {
		if (strncmp(kw->str, str, len))
			continue;
		klen = strlen(kw->str);
		if (len == klen)
			return kw; /* exact match */
		if (len < klen) {
			if (!foundkw)
				foundkw = kw; /* shortcut match */
			else
				return NULL; /* ambiguous word */
		}
	}
	return foundkw;
}

/*
 * get_cmdvec
 *
 * returns:
 * ENOMEM: not enough memory to allocate command
 * ESRCH: command not found
 * EINVAL: argument missing for command
 */
int get_cmdvec (char *cmd, vector *v)
{
	int i;
	int r = 0;
	int get_param = 0;
	char * buff;
	struct key * kw = NULL;
	struct key * cmdkw = NULL;
	vector cmdvec, strvec;

	strvec = alloc_strvec(cmd);
	if (!strvec)
		return ENOMEM;

	cmdvec = vector_alloc();

	if (!cmdvec) {
		free_strvec(strvec);
		return ENOMEM;
	}

	vector_foreach_slot(strvec, buff, i) {
		if (is_quote(buff))
			continue;
		if (get_param) {
			get_param = 0;
			cmdkw->param = strdup(buff);
			continue;
		}
		kw = find_key(buff);
		if (!kw) {
			r = ESRCH;
			goto out;
		}
		cmdkw = alloc_key();
		if (!cmdkw) {
			r = ENOMEM;
			goto out;
		}
		if (!vector_alloc_slot(cmdvec)) {
			free(cmdkw);
			r = ENOMEM;
			goto out;
		}
		vector_set_slot(cmdvec, cmdkw);
		cmdkw->code = kw->code;
		cmdkw->has_param = kw->has_param;
		if (kw->has_param)
			get_param = 1;
	}
	if (get_param) {
		r = EINVAL;
		goto out;
	}
	*v = cmdvec;
	free_strvec(strvec);
	return 0;

out:
	free_strvec(strvec);
	free_keys(cmdvec);
	return r;
}

static uint64_t
fingerprint(const struct _vector *vec)
{
	int i;
	uint64_t fp = 0;
	struct key * kw;

	if (!vec)
		return 0;

	vector_foreach_slot(vec, kw, i)
		fp += kw->code;

	return fp;
}

struct handler *find_handler_for_cmdvec(const struct _vector *v)
{
	return find_handler(fingerprint(v));
}

int
alloc_handlers (void)
{
	handlers = vector_alloc();

	if (!handlers)
		return 1;

	return 0;
}

static int
genhelp_sprint_aliases (struct strbuf *reply, vector keys,
			struct key * refkw)
{
	int i;
	struct key * kw;
	size_t initial_len = get_strbuf_len(reply);

	vector_foreach_slot (keys, kw, i) {
		if (kw->code == refkw->code && kw != refkw &&
		    print_strbuf(reply, "|%s", kw->str) < 0)
			return -1;
	}

	return get_strbuf_len(reply) - initial_len;
}

static int
do_genhelp(struct strbuf *reply, const char *cmd, int error) {
	int i, j;
	uint64_t fp;
	struct handler * h;
	struct key * kw;
	int rc = 0;
	size_t initial_len = get_strbuf_len(reply);

	switch(error) {
	case ENOMEM:
		rc = print_strbuf(reply, "%s: Not enough memory\n", cmd);
		break;
	case ESRCH:
		rc = print_strbuf(reply, "%s: not found\n", cmd);
		break;
	case EINVAL:
		rc = print_strbuf(reply, "%s: Missing argument\n", cmd);
		break;
	}
	if (rc < 0)
		return -1;

	if (print_strbuf(reply, VERSION_STRING) < 0 ||
	    append_strbuf_str(reply, "CLI commands reference:\n") < 0)
		return -1;

	vector_foreach_slot (handlers, h, i) {
		fp = h->fingerprint;
		vector_foreach_slot (keys, kw, j) {
			if ((kw->code & fp)) {
				fp -= kw->code;
				if (print_strbuf(reply, " %s", kw->str) < 0 ||
				    genhelp_sprint_aliases(reply, keys, kw) < 0)
					return -1;

				if (kw->has_param) {
					if (print_strbuf(reply, " $%s",
							 kw->str) < 0)
						return -1;
				}
			}
		}
		if (append_strbuf_str(reply, "\n") < 0)
			return -1;
	}
	return get_strbuf_len(reply) - initial_len;
}


char *genhelp_handler(const char *cmd, int error)
{
	STRBUF_ON_STACK(reply);

	if (do_genhelp(&reply, cmd, error) == -1)
		condlog(0, "genhelp_handler: out of memory");
	return steal_strbuf_str(&reply);
}

char *
get_keyparam (vector v, uint64_t code)
{
	struct key * kw;
	int i;

	vector_foreach_slot(v, kw, i)
		if (kw->code == code)
			return kw->param;

	return NULL;
}

int
cli_init (void) {
	if (load_keys())
		return 1;

	if (alloc_handlers())
		return 1;

	return 0;
}

void cli_exit(void)
{
	free_handlers();
	free_keys(keys);
	keys = NULL;
}

static int
key_match_fingerprint (struct key * kw, uint64_t fp)
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
	static uint64_t rlfp;
	struct key * kw;
	int i;
	struct handler *h;
	vector v = NULL;

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
			uint64_t nfp = rlfp | kw->code;
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
