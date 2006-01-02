/*
 * Copyright (c) 2005 Christophe Varoqui
 */
#include <memory.h>
#include <vector.h>
#include <util.h>

#include "cli.h"

static struct key *
alloc_key (void)
{
	return (struct key *)MALLOC(sizeof(struct key));
}

static struct handler *
alloc_handler (void)
{
	return (struct handler *)MALLOC(sizeof(struct handler));
}

static int
add_key (vector vec, char * str, int code, int has_param)
{
	struct key * kw;

	kw = alloc_key();

	if (!kw)
		return 1;

	kw->code = code;
	kw->has_param = has_param;
	kw->str = STRDUP(str);

	if (!kw->str)
		goto out;

	if (!vector_alloc_slot(vec))
		goto out1;

	vector_set_slot(vec, kw);

	return 0;

out1:
	FREE(kw->str);
out:
	FREE(kw);
	return 1;
}

int
add_handler (int fp, int (*fn)(void *, char **, int *, void *))
{
	struct handler * h;

	h = alloc_handler();

	if (!h)
		return 1;

	if (!vector_alloc_slot(handlers)) {
		FREE(h);
		return 1;
	}

	vector_set_slot(handlers, h);
	h->fingerprint = fp;
	h->fn = fn;

	return 0;
}

static void
free_key (struct key * kw)
{
	if (kw->str)
		FREE(kw->str);

	if (kw->param)
		FREE(kw->param);

	FREE(kw);
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
free_handlers (vector vec)
{
	int i;
	struct handler * h;

	vector_foreach_slot (vec, h, i)
		FREE(h);

	vector_free(vec);
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
	r += add_key(keys, "paths", PATHS, 0);
	r += add_key(keys, "maps", MAPS, 0);
	r += add_key(keys, "multipaths", MAPS, 0);
	r += add_key(keys, "path", PATH, 1);
	r += add_key(keys, "map", MAP, 1);
	r += add_key(keys, "multipath", MAP, 1);
	r += add_key(keys, "group", GROUP, 1);
	r += add_key(keys, "reconfigure", RECONFIGURE, 0);
	r += add_key(keys, "status", STATUS, 0);
	r += add_key(keys, "stats", STATS, 0);
	r += add_key(keys, "topology", TOPOLOGY, 0);

	if (r) {
		free_keys(keys);
		keys = NULL;
		return 1;
	}

	return 0;
}

static struct key *
find_key (char * str)
{
	int i;
	int len, klen;
	struct key * kw = NULL;
	struct key * foundkw = NULL;

	vector_foreach_slot (keys, kw, i) {
		len = strlen(str);
		klen = strlen(kw->str);

		if (strncmp(kw->str, str, len))
			continue;
		else if (len == klen)
			return kw; /* exact match */
		else if (len < klen) {
			if (!foundkw)
				foundkw = kw; /* shortcut match */
			else
				return NULL; /* ambiguous word */
		}
	}
	return foundkw;
}
		
static struct handler *
find_handler (int fp)
{
	int i;
	struct handler *h;

	vector_foreach_slot (handlers, h, i)
		if (h->fingerprint == fp)
			return h;

	return NULL;
}

static vector
get_cmdvec (char * cmd)
{
	int fwd = 1;
	char * p = cmd;
	char * buff;
	struct key * kw = NULL;
	struct key * cmdkw = NULL;
	vector cmdvec;

	cmdvec = vector_alloc();

	if (!cmdvec)
		return NULL;

	while (fwd) {
		fwd = get_word(p, &buff);

		if (!buff)
			break;

		p += fwd;
		kw = find_key(buff);
		FREE(buff);

		if (!kw)
			goto out; /* synthax error */

		cmdkw = alloc_key();

		if (!cmdkw)
			goto out;

		if (!vector_alloc_slot(cmdvec)) {
			FREE(cmdkw);
			goto out;
		}

		vector_set_slot(cmdvec, cmdkw);
		cmdkw->code = kw->code;
		cmdkw->has_param = kw->has_param;
		
		if (kw->has_param) {
			if (*p == '\0')
				goto out;

			fwd = get_word(p, &buff);

			if (!buff)
				goto out;

			p += fwd;
			cmdkw->param = buff;
		}
	}

	return cmdvec;

out:
	free_keys(cmdvec);
	return NULL;
}

static int 
fingerprint(vector vec)
{
	int i;
	int fp = 0;
	struct key * kw;

	vector_foreach_slot(vec, kw, i)
		fp += kw->code;

	return fp;
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
genhelp_sprint_aliases (char * reply, vector keys, struct key * refkw)
{
	int i, fwd = 0;
	struct key * kw;

	vector_foreach_slot (keys, kw, i)
		if (kw->code == refkw->code && kw != refkw)
			fwd += sprintf(reply, "|%s", kw->str);

	return fwd;
}

static char *
genhelp_handler (void)
{
	int i, j;
	int fp;
	struct handler * h;
	struct key * kw;
	char * reply;
	char * p;

	reply = MALLOC(INITIAL_REPLY_LEN);

	if (!reply)
		return NULL;

	p = reply;

	vector_foreach_slot (handlers, h, i) {
		fp = h->fingerprint;
		vector_foreach_slot (keys, kw, j) {
			if ((kw->code & fp)) {
				fp -= kw->code;
				p += sprintf(p, " %s", kw->str);
				p += genhelp_sprint_aliases(p, keys, kw);

				if (kw->has_param)
					p += sprintf(p, " $%s", kw->str);
			}
		}
		p += sprintf(p, "\n");
	}

	return reply;
}

int
parse_cmd (char * cmd, char ** reply, int * len, void * data)
{
	int r;
	struct handler * h;
	vector cmdvec = get_cmdvec(cmd);

	if (!cmdvec) {
		*reply = genhelp_handler();
		*len = strlen(*reply) + 1;
		return 0;
	}

	h = find_handler(fingerprint(cmdvec));

	if (!h) {
		*reply = genhelp_handler();
		*len = strlen(*reply) + 1;
		return 0;
	}

	/*
	 * execute handler
	 */
	r = h->fn(cmdvec, reply, len, data);
	free_keys(cmdvec);

	return r;
}

char *
get_keyparam (vector v, int code)
{
	struct key * kw;
	int i;

	vector_foreach_slot(v, kw, i)
		if (kw->code == code)
			return kw->param;

	return NULL;
}
