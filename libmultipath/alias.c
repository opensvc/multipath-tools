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
#include <sys/inotify.h>

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
#include "time-util.h"
#include "lock.h"

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

/* uatomic access only */
static int bindings_file_changed = 1;

static const char bindings_file_path[] = DEFAULT_BINDINGS_FILE;

static pthread_mutex_t timestamp_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct timespec bindings_last_updated;

struct binding {
	char *alias;
	char *wwid;
};

/*
 * Perhaps one day we'll implement this more efficiently, thus use
 * an abstract type.
 */
typedef struct _vector Bindings;

/* Protect global_bindings */
static pthread_mutex_t bindings_mutex = PTHREAD_MUTEX_INITIALIZER;
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

static void free_bindings(Bindings *bindings)
{
	struct binding *bdg;
	int i;

	vector_foreach_slot(bindings, bdg, i)
		_free_binding(bdg);
	vector_reset(bindings);
}

static void set_global_bindings(Bindings *bindings)
{
	Bindings old_bindings;

	pthread_mutex_lock(&bindings_mutex);
	old_bindings = global_bindings;
	global_bindings = *bindings;
	pthread_mutex_unlock(&bindings_mutex);
	free_bindings(&old_bindings);
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

/*
 * Sort order for aliases.
 *
 * The "numeric" ordering of aliases for a given prefix P is
 * Pa, ..., Pz, Paa, ..., Paz, Pba, ... , Pzz, Paaa, ..., Pzzz, Paaaa, ...
 * We use the fact that for equal prefix, longer strings are always
 * higher than shorter ones. Strings of equal length are sorted alphabetically.
 * This is achieved by sorting be length first, then using strcmp().
 * If multiple prefixes are in use, the aliases with a given prefix will
 * not necessarily be in a contiguous range of the vector, but they will
 * be ordered such that for a given prefix, numercally higher aliases will
 * always be sorted after lower ones.
 */
static int alias_compar(const void *p1, const void *p2)
{
	const char *alias1 = *((char * const *)p1);
	const char *alias2 = *((char * const *)p2);

	if (alias1 && alias2) {
		ssize_t ldif = strlen(alias1) - strlen(alias2);

		if (ldif)
			return ldif;
		return strcmp(alias1, alias2);
	} else
		/* Move NULL alias to the end */
		return alias1 ? -1 : alias2 ? 1 : 0;
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
		if ((cmp = alias_compar(&bdg->alias, &alias)) <= 0)
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

static int write_bindings_file(const Bindings *bindings, int fd,
			       struct timespec *ts)
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
	fsync(fd);
	if (ts) {
		struct stat st;

		if (fstat(fd, &st) == 0)
			*ts = st.st_mtim;
		else
			clock_gettime(CLOCK_REALTIME_COARSE, ts);
	}
	return 0;
}

void handle_bindings_file_inotify(const struct inotify_event *event)
{
	const char *base;
	bool changed = false;
	struct stat st;
	struct timespec ts = { 0, 0 };
	int ret;

	if (!(event->mask & IN_MOVED_TO))
		return;

	base = strrchr(bindings_file_path, '/');
	changed = base && !strcmp(base + 1, event->name);
	ret = stat(bindings_file_path, &st);

	if (!changed)
		return;

	pthread_mutex_lock(&timestamp_mutex);
	if (ret == 0) {
		ts = st.st_mtim;
		changed = timespeccmp(&ts, &bindings_last_updated) > 0;
	}
	pthread_mutex_unlock(&timestamp_mutex);

	if (changed) {
		uatomic_xchg_int(&bindings_file_changed, 1);
		condlog(3, "%s: bindings file must be re-read, new timestamp: %ld.%06ld",
			__func__, (long)ts.tv_sec, (long)ts.tv_nsec / 1000);
	} else
		condlog(3, "%s: bindings file is up-to-date, timestamp: %ld.%06ld",
			__func__, (long)ts.tv_sec, (long)ts.tv_nsec / 1000);
}

static int update_bindings_file(const Bindings *bindings)
{
	int rc;
	int fd = -1;
	char tempname[PATH_MAX];
	mode_t old_umask;
	struct timespec ts;

	if (safe_sprintf(tempname, "%s.XXXXXX", bindings_file_path))
		return -1;
	/* coverity: SECURE_TEMP */
	old_umask = umask(0077);
	if ((fd = mkstemp(tempname)) == -1) {
		condlog(1, "%s: mkstemp: %m", __func__);
		return -1;
	}
	umask(old_umask);
	pthread_cleanup_push(cleanup_fd_ptr, &fd);
	rc = write_bindings_file(bindings, fd, &ts);
	pthread_cleanup_pop(1);
	if (rc == -1) {
		condlog(1, "failed to write new bindings file");
		unlink(tempname);
		return rc;
	}
	if ((rc = rename(tempname, bindings_file_path)) == -1)
		condlog(0, "%s: rename: %m", __func__);
	else {
		pthread_mutex_lock(&timestamp_mutex);
		bindings_last_updated = ts;
		pthread_mutex_unlock(&timestamp_mutex);
		condlog(1, "updated bindings file %s", bindings_file_path);
	}
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

	char wwid[WWID_SIZE];

	/* If the map doesn't exist, it's fine */
	if (dm_get_uuid(alias, wwid, sizeof(wwid)) != 0)
		return false;

	/* If both the name and the wwid match, it's fine.*/
	if (strncmp(map_wwid, wwid, sizeof(wwid)) == 0)
		return false;

	condlog(3, "%s: alias '%s' already taken, reselecting alias",
		map_wwid, alias);
	return true;
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

	vector_foreach_slot(bindings, bdg, i) {
		int curr_id = scan_devname(bdg->alias, prefix);

		if (curr_id == -1)
			continue;
		if (id > curr_id) {
			condlog(0, "%s: ERROR: bindings are not sorted", __func__);
			return -1;
		}
		while (id < curr_id && id_already_taken(id, prefix, map_wwid))
			id++;
		if (id < curr_id)
			return id;
		id++;
		if (id <= 0)
			break;
	}

	for (; id > 0; id++) {
		if (!id_already_taken(id, prefix, map_wwid))
			break;
	}

	if (id <= 0) {
		id = -1;
		condlog(0, "no more available user_friendly_names");
	}
	return id;
}

/* Called with binding_mutex held */
static char *
allocate_binding(const char *wwid, int id, const char *prefix)
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

	if (update_bindings_file(&global_bindings) == -1) {
		condlog(1, "%s: deleting binding %s for %s", __func__, alias, wwid);
		delete_binding(&global_bindings, wwid);
		free(alias);
		return NULL;
	}

	condlog(3, "Created new binding [%s] for WWID [%s]", alias, wwid);
	return alias;
}

enum {
	BINDINGS_FILE_UP2DATE,
	BINDINGS_FILE_READ,
	BINDINGS_FILE_ERROR,
	BINDINGS_FILE_BAD,
};

static int _read_bindings_file(const struct config *conf, Bindings *bindings,
			       bool force);

static void read_bindings_file(void)
{
	struct config *conf;
	Bindings bindings = {.allocated = 0, };
	int rc;

	conf = get_multipath_config();
	pthread_cleanup_push(put_multipath_config, conf);
	rc = _read_bindings_file(conf, &bindings, false);
	pthread_cleanup_pop(1);
	if (rc == BINDINGS_FILE_READ)
		set_global_bindings(&bindings);
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
 *    that the mpvec correctly represents kernel state.
 */

char *get_user_friendly_alias(const char *wwid, const char *alias_old,
			      const char *prefix, bool bindings_read_only)
{
	char *alias = NULL;
	int id = 0;
	bool new_binding = false;
	const struct binding *bdg;

	read_bindings_file();

	pthread_mutex_lock(&bindings_mutex);
	pthread_cleanup_push(cleanup_mutex, &bindings_mutex);

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
		alias = allocate_binding(wwid, id, prefix);

	if (alias && !new_binding)
		condlog(2, "Allocated existing binding [%s] for WWID [%s]",
			alias, wwid);

out:
	/* unlock bindings_mutex */
	pthread_cleanup_pop(1);
	return alias;
}

int get_user_friendly_wwid(const char *alias, char *buff)
{
	const struct binding *bdg;
	int rc = -1;

	if (!alias || *alias == '\0') {
		condlog(3, "Cannot find binding for empty alias");
		return -1;
	}

	read_bindings_file();

	pthread_mutex_lock(&bindings_mutex);
	pthread_cleanup_push(cleanup_mutex, &bindings_mutex);
	bdg = get_binding_for_alias(&global_bindings, alias);
	if (bdg) {
		strlcpy(buff, bdg->wwid, WWID_SIZE);
		rc = 0;
	} else
		*buff = '\0';
	pthread_cleanup_pop(1);
	return rc;
}

void cleanup_bindings(void)
{
	pthread_mutex_lock(&bindings_mutex);
	free_bindings(&global_bindings);
	pthread_mutex_unlock(&bindings_mutex);
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
	char header[sizeof(BINDINGS_FILE_HEADER)];

	header[sizeof(BINDINGS_FILE_HEADER) - 1] = '\0';
	if (fread(header, sizeof(BINDINGS_FILE_HEADER) - 1, 1, file) < 1) {
		condlog(2, "%s: failed to read header from %s", __func__,
			bindings_file_path);
		fseek(file, 0, SEEK_SET);
		rc = -1;
	} else if (strcmp(header, BINDINGS_FILE_HEADER)) {
		condlog(2, "%s: invalid header in %s", __func__,
			bindings_file_path);
		fseek(file, 0, SEEK_SET);
		rc = -1;
	}
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

static int mp_alias_compar(const void *p1, const void *p2)
{
	return alias_compar(&((*(struct mpentry * const *)p1)->alias),
			    &((*(struct mpentry * const *)p2)->alias));
}

static int _read_bindings_file(const struct config *conf, Bindings *bindings,
			       bool force)
{
	int can_write;
	int rc = 0, ret, fd;
	FILE *file;
	struct stat st;
	int has_changed = uatomic_xchg_int(&bindings_file_changed, 0);

	if (!force) {
		if (!has_changed) {
			condlog(4, "%s: bindings are unchanged", __func__);
			return BINDINGS_FILE_UP2DATE;
		}
	}

	fd = open_file(bindings_file_path, &can_write, BINDINGS_FILE_HEADER);
	if (fd == -1)
		return BINDINGS_FILE_ERROR;

	file = fdopen(fd, "r");
	if (file != NULL) {
		condlog(3, "%s: reading %s", __func__, bindings_file_path);

		pthread_cleanup_push(cleanup_fclose, file);
		ret = _check_bindings_file(conf, file, bindings);
		if (ret == 0) {
			struct timespec ts;

			rc = BINDINGS_FILE_READ;
			ret = fstat(fd, &st);
			if (ret == 0)
				ts = st.st_mtim;
			else {
				condlog(1, "%s: fstat failed (%m), using current time", __func__);
				clock_gettime(CLOCK_REALTIME_COARSE, &ts);
			}
			pthread_mutex_lock(&timestamp_mutex);
			bindings_last_updated = ts;
			pthread_mutex_unlock(&timestamp_mutex);
		} else if (ret == -1 && can_write && !conf->bindings_read_only) {
			ret = update_bindings_file(bindings);
			if (ret == 0)
				rc = BINDINGS_FILE_READ;
			else
				rc = BINDINGS_FILE_BAD;
		} else {
			condlog(0, "ERROR: bad settings in read-only bindings file %s",
				bindings_file_path);
			rc = BINDINGS_FILE_BAD;
		}
		pthread_cleanup_pop(1);
	} else {
		condlog(1, "failed to fdopen %s: %m",
			bindings_file_path);
		close(fd);
		rc = BINDINGS_FILE_ERROR;
	}

	return rc;
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
	int i, rc;
	Bindings bindings = {.allocated = 0, };
	vector mptable = NULL;
	struct mpentry *mpe;

	mptable = vector_convert(NULL, conf->mptable, struct mpentry *, identity);
	if (!mptable)
		return -1;

	pthread_cleanup_push_cast(free_bindings, &bindings);
	pthread_cleanup_push(cleanup_vector_free, mptable);

	vector_sort(mptable, mp_alias_compar);
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

	rc = _read_bindings_file(conf, &bindings, true);

	if (rc == BINDINGS_FILE_READ) {
		set_global_bindings(&bindings);
		rc = 0;
	}

	return rc;
}
