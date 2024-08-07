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

#include "mpath_cmd.h"
#include "cli.h"
#include "cli_handlers.h"
#include "debug.h"
#include "strbuf.h"

static vector keys;
static vector handlers;

vector get_keys(void)
{
	return keys;
}

vector get_handlers(void)
{
	return handlers;
}
/* See KEY_INVALID in cli.h */
#define INVALID_FINGERPRINT ((uint32_t)(0))

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
add_key (vector vec, char * str, uint8_t code, int has_param)
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

static struct handler *add_handler(uint32_t fp, cli_handler *fn, bool locked)
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
find_handler (uint32_t fp)
{
	int i;
	struct handler *h;

	if (fp == INVALID_FINGERPRINT)
		return NULL;
	vector_foreach_slot (handlers, h, i)
		if (h->fingerprint == fp)
			return h;

	return NULL;
}

int
set_handler_callback__ (uint32_t fp, cli_handler *fn, bool locked)
{
	struct handler *h;

	assert(fp != INVALID_FINGERPRINT);
	assert(find_handler(fp) == NULL);
	h = add_handler(fp, fn, locked);
	if (!h) {
		condlog(0, "%s: failed to set handler for code %"PRIu32,
			__func__, fp);
		return 1;
	}
	return 0;
}

void free_key (struct key * kw)
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

	r += add_key(keys, "list", VRB_LIST, 0);
	r += add_key(keys, "show", VRB_LIST, 0);
	r += add_key(keys, "add", VRB_ADD, 0);
	r += add_key(keys, "remove", VRB_DEL, 0);
	r += add_key(keys, "del", VRB_DEL, 0);
	r += add_key(keys, "switch", VRB_SWITCH, 0);
	r += add_key(keys, "switchgroup", VRB_SWITCH, 0);
	r += add_key(keys, "suspend", VRB_SUSPEND, 0);
	r += add_key(keys, "resume", VRB_RESUME, 0);
	r += add_key(keys, "reinstate", VRB_REINSTATE, 0);
	r += add_key(keys, "fail", VRB_FAIL, 0);
	r += add_key(keys, "resize", VRB_RESIZE, 0);
	r += add_key(keys, "reset", VRB_RESET, 0);
	r += add_key(keys, "reload", VRB_RELOAD, 0);
	r += add_key(keys, "forcequeueing", VRB_FORCEQ, 0);
	r += add_key(keys, "disablequeueing", VRB_DISABLEQ, 0);
	r += add_key(keys, "restorequeueing", VRB_RESTOREQ, 0);
	r += add_key(keys, "paths", KEY_PATHS, 0);
	r += add_key(keys, "maps", KEY_MAPS, 0);
	r += add_key(keys, "multipaths", KEY_MAPS, 0);
	r += add_key(keys, "path", KEY_PATH, 1);
	r += add_key(keys, "map", KEY_MAP, 1);
	r += add_key(keys, "multipath", KEY_MAP, 1);
	r += add_key(keys, "group", KEY_GROUP, 1);
	r += add_key(keys, "reconfigure", VRB_RECONFIGURE, 0);
	r += add_key(keys, "daemon", KEY_DAEMON, 0);
	r += add_key(keys, "status", KEY_STATUS, 0);
	r += add_key(keys, "stats", KEY_STATS, 0);
	r += add_key(keys, "topology", KEY_TOPOLOGY, 0);
	r += add_key(keys, "config", KEY_CONFIG, 0);
	r += add_key(keys, "blacklist", KEY_BLACKLIST, 0);
	r += add_key(keys, "devices", KEY_DEVICES, 0);
	r += add_key(keys, "raw", KEY_RAW, 0);
	r += add_key(keys, "wildcards", KEY_WILDCARDS, 0);
	r += add_key(keys, "quit", VRB_QUIT, 0);
	r += add_key(keys, "exit", VRB_QUIT, 0);
	r += add_key(keys, "shutdown", VRB_SHUTDOWN, 0);
	r += add_key(keys, "getprstatus", VRB_GETPRSTATUS, 0);
	r += add_key(keys, "setprstatus", VRB_SETPRSTATUS, 0);
	r += add_key(keys, "unsetprstatus", VRB_UNSETPRSTATUS, 0);
	r += add_key(keys, "format", KEY_FMT, 1);
	r += add_key(keys, "json", KEY_JSON, 0);
	r += add_key(keys, "getprkey", VRB_GETPRKEY, 0);
	r += add_key(keys, "setprkey", VRB_SETPRKEY, 0);
	r += add_key(keys, "unsetprkey", VRB_UNSETPRKEY, 0);
	r += add_key(keys, "key", KEY_KEY, 1);
	r += add_key(keys, "local", KEY_LOCAL, 0);
	r += add_key(keys, "setmarginal", VRB_SETMARGINAL, 0);
	r += add_key(keys, "unsetmarginal", VRB_UNSETMARGINAL, 0);
	r += add_key(keys, "all", KEY_ALL, 0);


	if (r) {
		free_keys(keys);
		keys = NULL;
		return 1;
	}
	return 0;
}

struct key *find_key (const char * str)
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

static void cleanup_strvec(vector *arg)
{
	free_strvec(*arg);
}

static void cleanup_keys(vector *arg)
{
	free_keys(*arg);
}

/*
 * get_cmdvec() - parse input
 *
 * @cmd: a command string to be parsed
 * @v: a vector of keywords with parameters
 *
 * returns:
 * ENOMEM: not enough memory to allocate command
 * ESRCH: keyword not found at end of input
 * ENOENT: keyword not found somewhere else
 * EINVAL: argument missing for command
 */
int get_cmdvec (char *cmd, vector *v, bool allow_incomplete)
{
	int i;
	int r = 0;
	int get_param = 0;
	char * buff;
	struct key * kw = NULL;
	struct key * cmdkw = NULL;
	vector cmdvec __attribute__((cleanup(cleanup_keys))) = vector_alloc();
	vector strvec __attribute__((cleanup(cleanup_strvec))) = alloc_strvec(cmd);

	if (!strvec || !cmdvec)
		return ENOMEM;

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
			r = i == VECTOR_SIZE(strvec) - 1 ? ESRCH : ENOENT;
			break;
		}
		cmdkw = alloc_key();
		if (!cmdkw) {
			r = ENOMEM;
			break;
		}
		if (!vector_alloc_slot(cmdvec)) {
			free(cmdkw);
			r = ENOMEM;
			break;
		}
		vector_set_slot(cmdvec, cmdkw);
		cmdkw->code = kw->code;
		cmdkw->has_param = kw->has_param;
		if (kw->has_param)
			get_param = 1;
	}
	if (get_param)
		r = EINVAL;

	if (r && !allow_incomplete)
		return r;

	*v = cmdvec;
	cmdvec = NULL;
	return r;
}

uint32_t fingerprint(const struct _vector *vec)
{
	int i;
	uint32_t fp = 0;
	struct key * kw;

	if (!vec || VECTOR_SIZE(vec) > 4)
		return INVALID_FINGERPRINT;

	vector_foreach_slot(vec, kw, i) {
		if (i >= 4)
			break;
		fp |= (uint32_t)kw->code << (8 * i);
	}
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
	int i, j, k;
	uint32_t fp;
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
		for (k = 0; k < 4; k++, fp >>= 8) {
			uint32_t code = fp & 0xff;

			if (!code)
				break;

			vector_foreach_slot (keys, kw, j) {
				if ((uint32_t)kw->code == code) {
					if (print_strbuf(reply, " %s", kw->str) < 0 ||
					    genhelp_sprint_aliases(reply, keys, kw) < 0)
						return -1;

					if (kw->has_param) {
						if (print_strbuf(reply, " $%s",
								 kw->str) < 0)
							return -1;
					}
					break;
				}
			}
		}
		if (append_strbuf_str(reply, "\n") < 0)
			return -1;
	}
	return get_strbuf_len(reply) - initial_len;
}


void genhelp_handler(const char *cmd, int error, struct strbuf *reply)
{
	if (do_genhelp(reply, cmd, error) == -1)
		condlog(0, "genhelp_handler: out of memory");
}

char *
get_keyparam (vector v, uint8_t code)
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

	init_handler_callbacks();
	return 0;
}

void cli_exit(void)
{
	free_handlers();
	free_keys(keys);
	keys = NULL;
}
