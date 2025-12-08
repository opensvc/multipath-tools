// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Part:        Configuration file parser/reader. Place into the dynamic
 *              data structure representation the conf file
 *
 * Version:     $Id: parser.c,v 1.0.3 2003/05/11 02:28:03 acassen Exp $
 *
 * Author:      Alexandre Cassen, <acassen@linux-vs.org>
 */

#include <syslog.h>
#include <errno.h>

#include "vector.h"
#include "config.h"
#include "parser.h"
#include "debug.h"
#include "strbuf.h"

/* local vars */
static int sublevel = 0;
static int line_nr;

int
keyword_alloc(vector keywords, char *string,
	      handler_fn *handler,
	      print_fn *print,
	      int unique)
{
	struct keyword *keyword;

	keyword = (struct keyword *)calloc(1, sizeof (struct keyword));

	if (!keyword)
		return 1;

	if (!vector_alloc_slot(keywords)) {
		free(keyword);
		return 1;
	}
	keyword->string = string;
	keyword->handler = handler;
	keyword->print = print;
	keyword->unique = unique;

	vector_set_slot(keywords, keyword);

	return 0;
}

void
install_sublevel(void)
{
	sublevel++;
}

void
install_sublevel_end(void)
{
	sublevel--;
}

int
install_keyword__(vector keywords, char *string,
		 handler_fn *handler,
		 print_fn *print,
		 int unique)
{
	int i = 0;
	struct keyword *keyword;

	/* fetch last keyword */
	keyword = VECTOR_LAST_SLOT(keywords);
	if (!keyword)
		return 1;

	/* position to last sub level */
	for (i = 0; i < sublevel; i++) {
		keyword = VECTOR_LAST_SLOT(keyword->sub);
		if (!keyword)
			return 1;
	}

	/* First sub level allocation */
	if (!keyword->sub)
		keyword->sub = vector_alloc();

	if (!keyword->sub)
		return 1;

	/* add new sub keyword */
	return keyword_alloc(keyword->sub, string, handler, print, unique);
}

void
free_keywords(vector keywords)
{
	struct keyword *keyword;
	int i;

	if (!keywords)
		return;

	for (i = 0; i < VECTOR_SIZE(keywords); i++) {
		keyword = VECTOR_SLOT(keywords, i);
		if (keyword->sub)
			free_keywords(keyword->sub);
		free(keyword);
	}
	vector_free(keywords);
}

struct keyword *
find_keyword(vector keywords, vector v, char * name)
{
	struct keyword *keyword;
	int i;
	size_t len;

	if (!name || !keywords)
		return NULL;

	if (!v)
		v = keywords;

	len = strlen(name);

	for (i = 0; i < VECTOR_SIZE(v); i++) {
		keyword = VECTOR_SLOT(v, i);
		if ((strlen(keyword->string) == len) &&
		    !strcmp(keyword->string, name))
			return keyword;
		if (keyword->sub) {
			keyword = find_keyword(keywords, keyword->sub, name);
			if (keyword)
				return keyword;
		}
	}
	return NULL;
}

int
snprint_keyword(struct strbuf *buff, const char *fmt, struct keyword *kw,
		const void *data)
{
	int r = 0;
	const char *f;
	struct config *conf;
	STRBUF_ON_STACK(sbuf);

	if (!kw || !kw->print)
		return 0;

	do {
		f = strchr(fmt, '%');
		if (f == NULL) {
			r = append_strbuf_str(&sbuf, fmt);
			goto out;
		}
		if (f != fmt &&
		    (r = append_strbuf_str__(&sbuf, fmt, f - fmt)) < 0)
			goto out;
		fmt = f + 1;
		switch(*fmt) {
		case 'k':
			if ((r = append_strbuf_str(&sbuf, kw->string)) < 0)
				goto out;
			break;
		case 'v':
			conf = get_multipath_config();
			pthread_cleanup_push(put_multipath_config, conf);
			r = kw->print(conf, &sbuf, data);
			pthread_cleanup_pop(1);
			if (r < 0)
				goto out;
			else if (r == 0) {/* no output if no value */
				reset_strbuf(&sbuf);
				goto out;
			}
			break;
		}
	} while (*fmt++);
out:
	if (r >= 0)
		r = append_strbuf_str__(buff, get_strbuf_str(&sbuf),
					get_strbuf_len(&sbuf));
	return r;
}

static const char quote_marker[] = { '\0', '"', '\0' };
bool is_quote(const char* token)
{
	return token[0] == quote_marker[0] &&
		token[1] == quote_marker[1] &&
		token[2] == quote_marker[2];
}

vector
alloc_strvec(char *string)
{
	char *cp, *start, *token;
	int strlen;
	int in_string;
	vector strvec;

	if (!string)
		return NULL;

	cp = string;

	/* Skip white spaces */
	while ((isspace((int) *cp) || !isascii((int) *cp)) && *cp != '\0')
		cp++;

	/* Return if there is only white spaces */
	if (*cp == '\0')
		return NULL;

	/* Return if string begin with a comment */
	if (*cp == '!' || *cp == '#')
		return NULL;

	/* Create a vector and alloc each command piece */
	strvec = vector_alloc();

	if (!strvec)
		return NULL;

	in_string = 0;
	while (1) {
		int two_quotes = 0;

		if (!vector_alloc_slot(strvec))
			goto out;

		vector_set_slot(strvec, NULL);
		start = cp;
		if (*cp == '"' && !(in_string && *(cp + 1) == '"')) {
			cp++;
			token = calloc(1, sizeof(quote_marker));

			if (!token)
				goto out;

			memcpy(token, quote_marker, sizeof(quote_marker));
			if (in_string)
				in_string = 0;
			else
				in_string = 1;
		} else if (!in_string && (*cp == '{' || *cp == '}')) {
			token = malloc(2);

			if (!token)
				goto out;

			*(token) = *cp;
			*(token + 1) = '\0';
			cp++;
		} else {

		move_on:
			while ((in_string ||
				(!isspace((int) *cp) && isascii((int) *cp) &&
				 *cp != '!' && *cp != '#' && *cp != '{' &&
				 *cp != '}')) && *cp != '\0' && *cp != '"')
				cp++;

			/* Two consecutive double quotes - don't end string */
			if (in_string && *cp == '"') {
				if (*(cp + 1) == '"') {
					two_quotes = 1;
					cp += 2;
					goto move_on;
				}
			}

			strlen = cp - start;
			token = calloc(1, strlen + 1);

			if (!token)
				goto out;

			memcpy(token, start, strlen);
			*(token + strlen) = '\0';

			/* Replace "" by " */
			if (two_quotes) {
				char *qq = strstr(token, "\"\"");
				while (qq != NULL) {
					memmove(qq + 1, qq + 2,
						strlen + 1 - (qq + 2 - token));
					qq = strstr(qq + 1, "\"\"");
				}
			}
		}
		vector_set_slot(strvec, token);

		while ((!in_string &&
			(isspace((int) *cp) || !isascii((int) *cp)))
		       && *cp != '\0')
			cp++;
		if (*cp == '\0' ||
		    (!in_string && (*cp == '!' || *cp == '#'))) {
			return strvec;
		}
	}
out:
	vector_free(strvec);
	return NULL;
}

static int
read_line(FILE *stream, char *buf, int size)
{
	char *p;

	if (fgets(buf, size, stream) == NULL)
		return 0;
	strtok_r(buf, "\n\r", &p);
	return 1;
}

void *
set_value(vector strvec)
{
	char *str = VECTOR_SLOT(strvec, 1);
	char *alloc = NULL;

	if (!str) {
		condlog(0, "option '%s' missing value",
			(char *)VECTOR_SLOT(strvec, 0));
		return NULL;
	}
	if (is_quote(str)) {
		if (VECTOR_SIZE(strvec) > 2) {
			str = VECTOR_SLOT(strvec, 2);
			if (!str) {
				condlog(0, "parse error for option '%s'",
					(char *)VECTOR_SLOT(strvec, 0));
				return NULL;
			}
		}
		/* Even empty quotes counts as a value (An empty string) */
		if (is_quote(str)) {
			alloc = (char *)calloc(1, sizeof (char));
			if (!alloc)
				goto oom;
			return alloc;
		}
	}
	alloc = strdup(str);
	if (alloc)
		return alloc;
oom:
	condlog(0, "can't allocate memory for option '%s'",
		(char *)VECTOR_SLOT(strvec, 0));
	return NULL;
}

/* non-recursive configuration stream handler */
static int kw_level = 0;

int warn_on_duplicates(vector uniques, char *str, const char *file)
{
	char *tmp;
	int i;

	vector_foreach_slot(uniques, tmp, i) {
		if (!strcmp(str, tmp)) {
			condlog(1, "%s line %d, duplicate keyword: %s",
				file, line_nr, str);
			return 0;
		}
	}
	tmp = strdup(str);
	if (!tmp)
		return 1;
	if (!vector_alloc_slot(uniques)) {
		free(tmp);
		return 1;
	}
	vector_set_slot(uniques, tmp);
	return 0;
}

void free_uniques(vector uniques)
{
	char *tmp;
	int i;

	vector_foreach_slot(uniques, tmp, i)
		free(tmp);
	vector_free(uniques);
}

int
is_sublevel_keyword(char *str)
{
	return (strcmp(str, "defaults") == 0 || strcmp(str, "blacklist") == 0 ||
		strcmp(str, "blacklist_exceptions") == 0 || strcmp(str, "devices") == 0 ||
		strcmp(str, "device") == 0 || strcmp(str, "multipaths") == 0 ||
		strcmp(str, "multipath") == 0);
}

int
validate_config_strvec(vector strvec, const char *file)
{
	char *str = NULL;

	if (strvec && VECTOR_SIZE(strvec) > 0)
		str = VECTOR_SLOT(strvec, 0);

	if (str == NULL) {
		condlog(0, "can't parse option on line %d of %s",
			line_nr, file);
		return -1;
	}
	if (*str == '}') {
		if (VECTOR_SIZE(strvec) > 1)
			condlog(0, "ignoring extra data starting with '%s' on line %d of %s", (char *)VECTOR_SLOT(strvec, 1), line_nr, file);
		return 0;
	}
	if (*str == '{') {
		condlog(0, "invalid keyword '%s' on line %d of %s",
			str, line_nr, file);
		return -1;
	}
	if (is_sublevel_keyword(str)) {
		str = VECTOR_SIZE(strvec) > 1 ? VECTOR_SLOT(strvec, 1) : NULL;
		if (str == NULL)
			condlog(0, "missing '{' on line %d of %s",
				line_nr, file);
		else if (*str != '{')
			condlog(0, "expecting '{' on line %d of %s. found '%s'",
				line_nr, file, str);
		else if (VECTOR_SIZE(strvec) > 2)
			condlog(0, "ignoring extra data starting with '%s' on line %d of %s", (char *)VECTOR_SLOT(strvec, 2), line_nr, file);
		return 0;
	}
	str = VECTOR_SIZE(strvec) > 1 ? VECTOR_SLOT(strvec, 1) : NULL;
	if (str == NULL) {
		condlog(0, "missing value for option '%s' on line %d of %s",
			(char *)VECTOR_SLOT(strvec, 0), line_nr, file);
		return -1;
	}
	if (!is_quote(str)) {
		if (VECTOR_SIZE(strvec) > 2)
			condlog(0, "ignoring extra data starting with '%s' on line %d of %s", (char *)VECTOR_SLOT(strvec, 2), line_nr, file);
		return 0;
	}
	if (VECTOR_SIZE(strvec) == 2) {
		condlog(0, "missing closing quotes on line %d of %s",
			line_nr, file);
		return 0;
	}
	str = VECTOR_SLOT(strvec, 2);
	if (str == NULL) {
		condlog(0, "can't parse value on line %d of %s",
			line_nr, file);
		return -1;
	}
	if (is_quote(str)) {
		if (VECTOR_SIZE(strvec) > 3)
			condlog(0, "ignoring extra data starting with '%s' on line %d of %s", (char *)VECTOR_SLOT(strvec, 3), line_nr, file);
		return 0;
	}
	if (VECTOR_SIZE(strvec) == 3) {
		condlog(0, "missing closing quotes on line %d of %s",
			line_nr, file);
		return 0;
	}
	str = VECTOR_SLOT(strvec, 3);
	if (str == NULL) {
		condlog(0, "can't parse value on line %d of %s",
			line_nr, file);
		return -1;
	}
	if (!is_quote(str)) {
		/* There should only ever be one token between quotes */
		condlog(0, "parsing error starting with '%s' on line %d of %s",
			str, line_nr, file);
		return -1;
	}
	if (VECTOR_SIZE(strvec) > 4)
		condlog(0, "ignoring extra data starting with '%s' on line %d of %s", (char *)VECTOR_SLOT(strvec, 4), line_nr, file);
	return 0;
}

static int
process_stream(struct config *conf, FILE *stream, vector keywords,
	       const char *section, const char *file)
{
	int i;
	int r = 0, t;
	struct keyword *keyword;
	char *str;
	char *buf;
	vector strvec;
	vector uniques;

	uniques = vector_alloc();
	if (!uniques)
		return 1;

	buf = calloc(1, MAXBUF);

	if (!buf) {
		vector_free(uniques);
		return 1;
	}

	while (read_line(stream, buf, MAXBUF)) {
		line_nr++;
		strvec = alloc_strvec(buf);
		if (!strvec)
			continue;

		if (validate_config_strvec(strvec, file) != 0) {
			free_strvec(strvec);
			continue;
		}

		str = VECTOR_SLOT(strvec, 0);

		if (!strcmp(str, EOB)) {
			if (kw_level > 0) {
				free_strvec(strvec);
				goto out;
			}
			condlog(0, "unmatched '%s' at line %d of %s",
				EOB, line_nr, file);
		}

		for (i = 0; i < VECTOR_SIZE(keywords); i++) {
			keyword = VECTOR_SLOT(keywords, i);

			if (!strcmp(keyword->string, str)) {
				if (keyword->unique &&
				    warn_on_duplicates(uniques, str, file)) {
						r = 1;
						free_strvec(strvec);
						goto out;
				}
				if (keyword->handler) {
				    t = keyword->handler(conf, strvec, file,
							 line_nr);
					r += t;
					if (t)
						condlog(1, "%s line %d, parsing failed: %s",
							file, line_nr, buf);
				}

				if (keyword->sub) {
					kw_level++;
					r += process_stream(conf, stream,
							    keyword->sub,
							    keyword->string,
							    file);
					kw_level--;
				}
				break;
			}
		}
		if (i >= VECTOR_SIZE(keywords)) {
			if (section)
				condlog(1, "%s line %d, invalid keyword in the %s section: %s",
					file, line_nr, section, str);
			else
				condlog(1, "%s line %d, invalid keyword: %s",
					file, line_nr, str);
		}
		free_strvec(strvec);
	}
	if (kw_level == 1)
		condlog(1, "missing '%s' at end of %s", EOB, file);
out:
	free(buf);
	free_uniques(uniques);
	return r;
}

/* Data initialization */
int
process_file(struct config *conf, const char *file)
{
	int r;
	FILE *stream;

	if (!conf->keywords) {
		condlog(0, "No keywords allocated");
		return 1;
	}
	stream = fopen(file, "r");
	if (!stream) {
		condlog(0, "couldn't open configuration file '%s': %s",
			file, strerror(errno));
		return 1;
	}

	/* Stream handling */
	line_nr = 0;
	r = process_stream(conf, stream, conf->keywords, NULL, file);
	fclose(stream);
	//free_keywords(keywords);

	return r;
}
