/*
 * Copyright (c) 2005 Christophe Varoqui
 * Copyright (c) 2005 Benjamin Marzinski, Redhat
 */
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#include "debug.h"
#include "util.h"
#include "uxsock.h"
#include "alias.h"
#include "file.h"
#include "vector.h"
#include "checkers.h"
#include "structs.h"
#include "config.h"
#include "devmapper.h"
#include "strbuf.h"

/*
 * significant parts of this file were taken from iscsi-bindings.c of the
 * linux-iscsi project.
 * Copyright (C) 2002 Cisco Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * See the file COPYING included with this distribution for more details.
 */

#define BINDINGS_FILE_HEADER		\
"# Multipath bindings, Version : 1.0\n" \
"# NOTE: this file is automatically maintained by the multipath program.\n" \
"# You should not need to edit this file in normal circumstances.\n" \
"#\n" \
"# Format:\n" \
"# alias wwid\n" \
"#\n"

struct binding {
	char *alias;
	char *wwid;
};

/*
 * Perhaps one day we'll implement this more efficiently, thus use
 * an abstract type.
 */
typedef struct _vector Bindings;
static Bindings global_bindings = { .allocated = 0 };

enum {
	BINDING_EXISTS,
	BINDING_CONFLICT,
	BINDING_ADDED,
	BINDING_DELETED,
	BINDING_NOTFOUND,
	BINDING_ERROR,
};

static void _free_binding(struct binding *bdg)
{
	free(bdg->wwid);
	free(bdg->alias);
	free(bdg);
}

static const struct binding *get_binding_for_alias(const Bindings *bindings,
						   const char *alias)
{
	const struct binding *bdg;
	int i;

	if (!alias)
		return NULL;
	vector_foreach_slot(bindings, bdg, i) {
		if (!strncmp(bdg->alias, alias, WWID_SIZE)) {
			condlog(3, "Found matching alias [%s] in bindings file."
				" Setting wwid to %s", alias, bdg->wwid);
			return bdg;
		}
	}

	condlog(3, "No matching alias [%s] in bindings file.", alias);
	return NULL;
}

static const struct binding *get_binding_for_wwid(const Bindings *bindings,
						  const char *wwid)
{
	const struct binding *bdg;
	int i;

	if (!wwid)
		return NULL;
	vector_foreach_slot(bindings, bdg, i) {
		if (!strncmp(bdg->wwid, wwid, WWID_SIZE)) {
			condlog(3, "Found matching wwid [%s] in bindings file."
				" Setting alias to %s", wwid, bdg->alias);
			return bdg;
		}
	}
	condlog(3, "No matching wwid [%s] in bindings file.", wwid);
	return NULL;
}

static int add_binding(Bindings *bindings, const char *alias, const char *wwid)
{
	struct binding *bdg;
	int i, cmp = 0;

	/*
	 * Keep the bindings array sorted by alias.
	 * Optimization: Search backwards, assuming that the bindings file is
	 * sorted already.
	 */
	vector_foreach_slot_backwards(bindings, bdg, i) {
		if ((cmp = strcmp(bdg->alias, alias)) <= 0)
			break;
	}

	/* Check for exact match */
	if (i >= 0 && cmp == 0)
		return strcmp(bdg->wwid, wwid) ?
			BINDING_CONFLICT : BINDING_EXISTS;

	i++;
	bdg = calloc(1, sizeof(*bdg));
	if (bdg) {
		bdg->wwid = strdup(wwid);
		bdg->alias = strdup(alias);
		if (bdg->wwid && bdg->alias &&
		    vector_insert_slot(bindings, i, bdg))
			return BINDING_ADDED;
		else
			_free_binding(bdg);
	}

	return BINDING_ERROR;
}

static int delete_binding(Bindings *bindings, const char *wwid)
{
	struct binding *bdg;
	int i;

	vector_foreach_slot(bindings, bdg, i) {
		if (!strncmp(bdg->wwid, wwid, WWID_SIZE)) {
			_free_binding(bdg);
			break;
		}
	}
	if (i >= VECTOR_SIZE(bindings))
		return BINDING_NOTFOUND;

	vector_del_slot(bindings, i);
	return BINDING_DELETED;
}

static int write_bindings_file(const Bindings *bindings, int fd)
{
	struct binding *bnd;
	STRBUF_ON_STACK(content);
	int i;
	size_t len;

	if (__append_strbuf_str(&content, BINDINGS_FILE_HEADER,
				sizeof(BINDINGS_FILE_HEADER) - 1) == -1)
		return -1;

	vector_foreach_slot(bindings, bnd, i) {
		if (print_strbuf(&content, "%s %s\n",
					bnd->alias, bnd->wwid) < 0)
			return -1;
	}
	len = get_strbuf_len(&content);
	while (len > 0) {
		ssize_t n = write(fd, get_strbuf_str(&content), len);

		if (n < 0)
			return n;
		else if (n == 0) {
			condlog(2, "%s: short write", __func__);
			return -1;
		}
		len -= n;
	}
	return 0;
}

static int update_bindings_file(const Bindings *bindings,
				const char *bindings_file)
{
	int rc;
	int fd = -1;
	char tempname[PATH_MAX];
	mode_t old_umask;

	if (safe_sprintf(tempname, "%s.XXXXXX", bindings_file))
		return -1;
	/* coverity: SECURE_TEMP */
	old_umask = umask(0077);
	if ((fd = mkstemp(tempname)) == -1) {
		condlog(1, "%s: mkstemp: %m", __func__);
		return -1;
	}
	umask(old_umask);
	pthread_cleanup_push(cleanup_fd_ptr, &fd);
	rc = write_bindings_file(bindings, fd);
	pthread_cleanup_pop(1);
	if (rc == -1) {
		condlog(1, "failed to write new bindings file");
		unlink(tempname);
		return rc;
	}
	if ((rc = rename(tempname, bindings_file)) == -1)
		condlog(0, "%s: rename: %m", __func__);
	else
		condlog(1, "updated bindings file %s", bindings_file);
	return rc;
}

int
valid_alias(const char *alias)
{
	if (strchr(alias, '/') != NULL)
		return 0;
	return 1;
}

static int format_devname(struct strbuf *buf, int id)
{
	/*
	 * We need: 7 chars for 32bit integers, 14 chars for 64bit integers
	 */
	char devname[2 * sizeof(int)];
	int pos = sizeof(devname) - 1, rc;

	if (id <= 0)
		return -1;

	devname[pos] = '\0';
	for (; id >= 1; id /= 26)
		devname[--pos] = 'a' + --id % 26;

	rc = append_strbuf_str(buf, devname + pos);
	return rc >= 0 ? rc : -1;
}

static int
scan_devname(const char *alias, const char *prefix)
{
	const char *c;
	int i, n = 0;
	static const int last_26 = INT_MAX / 26;

	if (!prefix || strncmp(alias, prefix, strlen(prefix)))
		return -1;

	if (strlen(alias) == strlen(prefix))
		return -1;

	if (strlen(alias) > strlen(prefix) + 7)
		/* id of 'aaaaaaaa' overflows int */
		return -1;

	c = alias + strlen(prefix);
	while (*c != '\0' && *c != ' ' && *c != '\t') {
		if (*c < 'a' || *c > 'z')
			return -1;
		i = *c - 'a';
		if (n > last_26 || (n == last_26 && i >= INT_MAX % 26))
			return -1;
		n = n * 26 + i;
		c++;
		n++;
	}

	return n;
}

static bool alias_already_taken(const char *alias, const char *map_wwid)
{

	if (dm_map_present(alias)) {
		char wwid[WWID_SIZE];

		/* If both the name and the wwid match, then it's fine.*/
		if (dm_get_uuid(alias, wwid, sizeof(wwid)) == 0 &&
		    strncmp(map_wwid, wwid, sizeof(wwid)) == 0)
			return false;
		condlog(3, "%s: alias '%s' already taken, reselecting alias",
			map_wwid, alias);
		return true;
	}
	return false;
}

static bool id_already_taken(int id, const char *prefix, const char *map_wwid)
{
	STRBUF_ON_STACK(buf);
	const char *alias;

	if (append_strbuf_str(&buf, prefix) < 0 ||
	    format_devname(&buf, id) < 0)
		return false;

	alias = get_strbuf_str(&buf);
	return alias_already_taken(alias, map_wwid);
}

int get_free_id(const Bindings *bindings, const char *prefix, const char *map_wwid)
{
	const struct binding *bdg;
	int i, id = 1;
	int biggest_id = 1;
	int smallest_bigger_id = INT_MAX;

	vector_foreach_slot(bindings, bdg, i) {
		int curr_id = scan_devname(bdg->alias, prefix);

		/*
		 * Find an unused index - explanation of the algorithm
		 *
		 * ID: 1 = mpatha, 2 = mpathb, ...
		 *
		 * We assume the bindings are unsorted. The only constraint
		 * is that no ID occurs more than once. IDs that occur in the
		 * bindings are called "used".
		 *
		 * We call the list 1,2,3,..., exactly in this order, the list
		 * of "expected" IDs. The variable "id" always holds the next
		 * "expected" ID, IOW the last "expected" ID encountered plus 1.
		 * Thus all IDs below "id" are known to be used. However, at the
		 * end of the loop, the value of "id" isn't necessarily unused.
		 *
		 * "smallest_bigger_id" is the smallest used ID that was
		 * encountered while it was larger than the next "expected" ID
		 * at that iteration. Let X be some used ID. If all IDs below X
		 * are used and encountered in the right sequence before X, "id"
		 * will be > X when the loop ends. Otherwise, X was encountered
		 * "out of order", the condition (X > id) holds when X is
		 * encountered, and "smallest_bigger_id" will be set to X; i.e.
		 * it will be less or equal than X when the loop ends.
		 *
		 * At the end of the loop, (id < smallest_bigger_id) means that
		 * the value of "id" had been encountered neither in order nor
		 * out of order, and is thus unused. (id >= smallest_bigger_id)
		 * means that "id"'s value is in use. In this case, we play safe
		 * and use "biggest_id + 1" as the next value to try.
		 *
		 * biggest_id is always > smallest_bigger_id, except in the
		 * "perfectly ordered" case.
		 */
		if (curr_id == id) {
			if (id < INT_MAX)
				id++;
			else {
				id = -1;
				break;
			}
		}
		if (curr_id > biggest_id)
			biggest_id = curr_id;

		if (curr_id > id && curr_id < smallest_bigger_id)
			smallest_bigger_id = curr_id;
	}

	if (id >= smallest_bigger_id)
		id = biggest_id < INT_MAX ? biggest_id + 1 : -1;

	if (id > 0) {
		while(id_already_taken(id, prefix, map_wwid)) {
			if (id == INT_MAX) {
				id = -1;
				break;
			}
			id++;
			if (id == smallest_bigger_id) {
				if (biggest_id == INT_MAX) {
					id = -1;
					break;
				}
				if (biggest_id >= smallest_bigger_id)
					id = biggest_id + 1;
			}
		}
	}

	if (id < 0)
		condlog(0, "no more available user_friendly_names");
	return id;
}

static char *
allocate_binding(const char *filename, const char *wwid, int id, const char *prefix)
{
	STRBUF_ON_STACK(buf);
	char *alias;

	if (id <= 0) {
		condlog(0, "%s: cannot allocate new binding for id %d",
			__func__, id);
		return NULL;
	}

	if (append_strbuf_str(&buf, prefix) < 0 ||
	    format_devname(&buf, id) == -1)
		return NULL;

	alias = steal_strbuf_str(&buf);

	if (add_binding(&global_bindings, alias, wwid) != BINDING_ADDED) {
		condlog(0, "%s: cannot allocate new binding %s for %s",
			__func__, alias, wwid);
		free(alias);
		return NULL;
	}

	if (update_bindings_file(&global_bindings, filename) == -1) {
		condlog(1, "%s: deleting binding %s for %s", __func__, alias, wwid);
		delete_binding(&global_bindings, wwid);
		free(alias);
		return NULL;
	}

	condlog(3, "Created new binding [%s] for WWID [%s]", alias, wwid);
	return alias;
}

/*
 * get_user_friendly_alias() action table
 *
 * The table shows the various cases, the actions taken, and the CI
 * functions from tests/alias.c that represent them.
 *
 *  - O: old alias given
 *  - A: old alias in table (y: yes, correct WWID; X: yes, wrong WWID)
 *  - W: wwid in table
 *
 *  | No | O | A | W | action                                     | function gufa_X              |
 *  |----+---+---+---+--------------------------------------------+------------------------------|
 *  |  1 | n | - | n | get new alias                              | nomatch_Y                    |
 *  |  2 | n | - | y | use alias from bindings                    | match_a_Y                    |
 *  |  3 | y | n | n | add binding for old alias                  | old_nomatch_nowwidmatch      |
 *  |  4 | y | n | y | use alias from bindings (avoid duplicates) | old_nomatch_wwidmatch        |
 *  |  5 | y | y | n | [ impossible ]                             | -                            |
 *  |  6 | y | y | y | use old alias == alias from bindings       | old_match                    |
 *  |  7 | y | X | n | get new alias                              | old_match_other              |
 *  |  8 | y | X | y | use alias from bindings                    | old_match_other_wwidmatch    |
 *
 * Notes:
 *  - "use alias from bindings" means that the alias from the bindings file will
 *    be tried; if it is in use, the alias selection will fail. No other
 *    bindings will be attempted.
 *  - "get new alias" fails if all aliases are used up, or if writing the
 *    bindings file fails.
 *  - if "alias_old" is set, it can't be bound to a different map. alias_old is
 *    initialized in find_existing_alias() by scanning the mpvec. We trust
 *    that the mpvec corrcectly represents kernel state.
 */

char *get_user_friendly_alias(const char *wwid, const char *file, const char *alias_old,
			      const char *prefix, bool bindings_read_only)
{
	char *alias = NULL;
	int id = 0;
	bool new_binding = false;
	const struct binding *bdg;

	if (!*alias_old)
		goto new_alias;

	/* See if there's a binding matching both alias_old and wwid */
	bdg = get_binding_for_alias(&global_bindings, alias_old);
	if (bdg) {
		if (!strcmp(bdg->wwid, wwid)) {
			alias = strdup(alias_old);
			goto out;
		} else {
			condlog(0, "alias %s already bound to wwid %s, cannot reuse",
				alias_old, bdg->wwid);
			goto new_alias;
		}
	}

	/* allocate the existing alias in the bindings file */
	id = scan_devname(alias_old, prefix);

new_alias:
	/* Check for existing binding of WWID */
	bdg = get_binding_for_wwid(&global_bindings, wwid);
	if (bdg) {
		if (!alias_already_taken(bdg->alias, wwid)) {
			condlog(3, "Use existing binding [%s] for WWID [%s]",
				bdg->alias, wwid);
			alias = strdup(bdg->alias);
		}
		goto out;
	}

	if (id <= 0) {
		/*
		 * no existing alias was provided, or allocating it
		 * failed. Try a new one.
		 */
		id = get_free_id(&global_bindings, prefix, wwid);
		if (id <= 0)
			goto out;
		else
			new_binding = true;
	}

	if (!bindings_read_only && id > 0)
		alias = allocate_binding(file, wwid, id, prefix);

	if (alias && !new_binding)
		condlog(2, "Allocated existing binding [%s] for WWID [%s]",
			alias, wwid);

out:
	return alias;
}

int get_user_friendly_wwid(const char *alias, char *buff)
{
	const struct binding *bdg;

	if (!alias || *alias == '\0') {
		condlog(3, "Cannot find binding for empty alias");
		return -1;
	}

	bdg = get_binding_for_alias(&global_bindings, alias);
	if (!bdg) {
		*buff = '\0';
		return -1;
	}
	strlcpy(buff, bdg->wwid, WWID_SIZE);
	return 0;
}

static void free_bindings(Bindings *bindings)
{
	struct binding *bdg;
	int i;

	vector_foreach_slot(bindings, bdg, i)
		_free_binding(bdg);
	vector_reset(bindings);
}

void cleanup_bindings(void)
{
	free_bindings(&global_bindings);
}

enum {
	READ_BINDING_OK,
	READ_BINDING_SKIP,
};

static int read_binding(char *line, unsigned int linenr, char **alias,
			char **wwid) {
	char *c, *saveptr;

	c = strpbrk(line, "#\n\r");
	if (c)
		*c = '\0';

	*alias = strtok_r(line, " \t", &saveptr);
	if (!*alias) /* blank line */
		return READ_BINDING_SKIP;

	*wwid = strtok_r(NULL, " \t", &saveptr);
	if (!*wwid) {
		condlog(1, "invalid line %u in bindings file, missing WWID",
			linenr);
		return READ_BINDING_SKIP;
	}
	if (strlen(*wwid) > WWID_SIZE - 1) {
		condlog(3,
			"Ignoring too large wwid at %u in bindings file",
			linenr);
		return READ_BINDING_SKIP;
	}
	c = strtok_r(NULL, " \t", &saveptr);
	if (c)
		/* This is non-fatal */
		condlog(1, "invalid line %d in bindings file, extra args \"%s\"",
			linenr, c);
	return READ_BINDING_OK;
}

static int _check_bindings_file(const struct config *conf, FILE *file,
				 Bindings *bindings)
{
	int rc = 0;
	unsigned int linenr = 0;
	char *line = NULL;
	size_t line_len = 0;
	ssize_t n;

	pthread_cleanup_push(cleanup_free_ptr, &line);
	while ((n = getline(&line, &line_len, file)) >= 0) {
		char *alias, *wwid;
		const char *mpe_wwid;

		if (read_binding(line, ++linenr, &alias, &wwid)
		    == READ_BINDING_SKIP)
			continue;

		mpe_wwid = get_mpe_wwid(conf->mptable, alias);
		if (mpe_wwid && strcmp(mpe_wwid, wwid)) {
			condlog(0, "ERROR: alias \"%s\" for WWID %s in bindings file "
				"on line %u conflicts with multipath.conf entry for %s",
				alias, wwid, linenr, mpe_wwid);
			rc = -1;
			continue;
		}

		switch (add_binding(bindings, alias, wwid)) {
		case BINDING_CONFLICT:
			condlog(0, "ERROR: multiple bindings for alias \"%s\" in "
				"bindings file on line %u, discarding binding to WWID %s",
				alias, linenr, wwid);
			rc = -1;
			break;
		case BINDING_EXISTS:
			condlog(2, "duplicate line for alias %s in bindings file on line %u",
				alias, linenr);
			break;
		case BINDING_ERROR:
			condlog(2, "error adding binding %s -> %s",
				alias, wwid);
			break;
		default:
			break;
		}
	}
	pthread_cleanup_pop(1);
	return rc;
}

static int alias_compar(const void *p1, const void *p2)
{
	const char *alias1 = (*(struct mpentry * const *)p1)->alias;
	const char *alias2 = (*(struct mpentry * const *)p2)->alias;

	if (alias1 && alias2)
		return strcmp(alias1, alias2);
	else
		/* Move NULL alias to the end */
		return alias1 ? -1 : alias2 ? 1 : 0;
}

/*
 * check_alias_settings(): test for inconsistent alias configuration
 *
 * It's a fatal configuration error if the same alias is assigned to
 * multiple WWIDs. In the worst case, it can cause data corruption
 * by mangling devices with different WWIDs into the same multipath map.
 * This function tests the configuration from multipath.conf and the
 * bindings file for consistency, drops inconsistent multipath.conf
 * alias settings, and rewrites the bindings file if necessary, dropping
 * conflicting lines (if user_friendly_names is on, multipathd will
 * fill in the deleted lines with a newly generated alias later).
 * Note that multipath.conf is not rewritten. Use "multipath -T" for that.
 *
 * Returns: 0 in case of success, -1 if the configuration was bad
 * and couldn't be fixed.
 */
int check_alias_settings(const struct config *conf)
{
	int can_write;
	int rc = 0, i, fd;
	Bindings bindings = {.allocated = 0, };
	vector mptable = NULL;
	struct mpentry *mpe;

	mptable = vector_convert(NULL, conf->mptable, struct mpentry *, identity);
	if (!mptable)
		return -1;

	pthread_cleanup_push_cast(free_bindings, &bindings);
	pthread_cleanup_push(cleanup_vector_free, mptable);

	vector_sort(mptable, alias_compar);
	vector_foreach_slot(mptable, mpe, i) {
		if (!mpe->alias)
			/*
			 * alias_compar() sorts NULL alias at the end,
			 * so we can stop if we encounter this.
			 */
			break;
		if (add_binding(&bindings, mpe->alias, mpe->wwid) ==
		    BINDING_CONFLICT) {
			condlog(0, "ERROR: alias \"%s\" bound to multiple wwids in multipath.conf, "
				"discarding binding to %s",
				mpe->alias, mpe->wwid);
			free(mpe->alias);
			mpe->alias = NULL;
		}
	}
	/* This clears the bindings */
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);

	fd = open_file(conf->bindings_file, &can_write, BINDINGS_FILE_HEADER);
	if (fd != -1) {
		FILE *file = fdopen(fd, "r");

		if (file != NULL) {
			pthread_cleanup_push(cleanup_fclose, file);
			rc = _check_bindings_file(conf, file, &bindings);
			pthread_cleanup_pop(1);
			if (rc == -1 && can_write && !conf->bindings_read_only)
				rc = update_bindings_file(&bindings, conf->bindings_file);
			else if (rc == -1)
				condlog(0, "ERROR: bad settings in read-only bindings file %s",
					conf->bindings_file);
		} else {
			condlog(1, "failed to fdopen %s: %m",
				conf->bindings_file);
			close(fd);
		}
	}

	cleanup_bindings();
	global_bindings = bindings;
	return rc;
}
