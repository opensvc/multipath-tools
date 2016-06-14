/*
 * Part:        Configuration file parser/reader. Place into the dynamic
 *              data structure representation the conf file
 *
 * Version:     $Id: parser.c,v 1.0.3 2003/05/11 02:28:03 acassen Exp $
 *
 * Author:      Alexandre Cassen, <acassen@linux-vs.org>
 *
 *              This program is distributed in the hope that it will be useful,
 *              but WITHOUT ANY WARRANTY; without even the implied warranty of
 *              MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *              See the GNU General Public License for more details.
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 */

#include <syslog.h>
#include <errno.h>

#include "vector.h"
#include "config.h"
#include "parser.h"
#include "memory.h"
#include "debug.h"

/* local vars */
static int sublevel = 0;
static vector keywords = NULL;
static vector *keywords_addr = NULL;
static int line_nr;

void set_current_keywords (vector *k)
{
	keywords_addr = k;
	keywords = NULL;
}

int
keyword_alloc(vector keywords, char *string,
	      int (*handler) (struct config *, vector),
	      int (*print) (struct config *, char *, int, void *), int unique)
{
	struct keyword *keyword;

	keyword = (struct keyword *) MALLOC(sizeof (struct keyword));

	if (!keyword)
		return 1;

	if (!vector_alloc_slot(keywords)) {
		FREE(keyword);
		return 1;
	}
	keyword->string = string;
	keyword->handler = handler;
	keyword->print = print;
	keyword->unique = unique;

	vector_set_slot(keywords, keyword);

	return 0;
}

int
install_keyword_root(char *string, int (*handler) (struct config *, vector))
{
	int r = keyword_alloc(keywords, string, handler, NULL, 1);
	if (!r)
		*keywords_addr = keywords;
	return r;
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
_install_keyword(char *string, int (*handler) (struct config *, vector),
		 int (*print) (struct config *, char *, int, void *), int unique)
{
	int i = 0;
	struct keyword *keyword;

	/* fetch last keyword */
	keyword = VECTOR_SLOT(keywords, VECTOR_SIZE(keywords) - 1);

	/* position to last sub level */
	for (i = 0; i < sublevel; i++)
		keyword =
		    VECTOR_SLOT(keyword->sub, VECTOR_SIZE(keyword->sub) - 1);

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
		FREE(keyword);
	}
	vector_free(keywords);
}

struct keyword *
find_keyword(vector v, char * name)
{
	struct keyword *keyword;
	int i;
	int len;

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
			keyword = find_keyword(keyword->sub, name);
			if (keyword)
				return keyword;
		}
	}
	return NULL;
}

int
snprint_keyword(char *buff, int len, char *fmt, struct keyword *kw, void *data)
{
	int r;
	int fwd = 0;
	char *f = fmt;

	if (!kw || !kw->print)
		return 0;

	do {
		if (fwd == len || *f == '\0')
			break;
		if (*f != '%') {
			*(buff + fwd) = *f;
			fwd++;
			continue;
		}
		f++;
		switch(*f) {
		case 'k':
			fwd += snprintf(buff + fwd, len - fwd, "%s", kw->string);
			break;
		case 'v':
			r = kw->print(conf, buff + fwd, len - fwd, data);
			if (!r) { /* no output if no value */
				buff = '\0';
				return 0;
			}
			fwd += r;
			break;
		}
		if (fwd > len)
			fwd = len;
	} while (*f++);
	return fwd;
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
		if (!vector_alloc_slot(strvec))
			goto out;

		start = cp;
		if (*cp == '"') {
			cp++;
			token = MALLOC(2);

			if (!token)
				goto out;

			*(token) = '"';
			*(token + 1) = '\0';
			if (in_string)
				in_string = 0;
			else
				in_string = 1;
		} else if (!in_string && (*cp == '{' || *cp == '}')) {
			token = MALLOC(2);

			if (!token)
				goto out;

			*(token) = *cp;
			*(token + 1) = '\0';
			cp++;
		} else {
			while ((in_string ||
				(!isspace((int) *cp) && isascii((int) *cp) &&
				 *cp != '!' && *cp != '#' && *cp != '{' &&
				 *cp != '}')) && *cp != '\0' && *cp != '"')
				cp++;
			strlen = cp - start;
			token = MALLOC(strlen + 1);

			if (!token)
				goto out;

			memcpy(token, start, strlen);
			*(token + strlen) = '\0';
		}
		vector_set_slot(strvec, token);

		while ((isspace((int) *cp) || !isascii((int) *cp))
		       && *cp != '\0')
			cp++;
		if (*cp == '\0' || *cp == '!' || *cp == '#')
			return strvec;
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
	size_t size;
	int i = 0;
	int len = 0;
	char *alloc = NULL;
	char *tmp;

	if (!str) {
		condlog(0, "option '%s' missing value",
			(char *)VECTOR_SLOT(strvec, 0));
		return NULL;
	}
	size = strlen(str);
	if (size == 0) {
		condlog(0, "option '%s' has empty value",
			(char *)VECTOR_SLOT(strvec, 0));
		return NULL;
	}
	if (*str != '"') {
		alloc = MALLOC(sizeof (char) * (size + 1));
		if (alloc)
			memcpy(alloc, str, size);
		else
			condlog(0, "can't allocate memeory for option '%s'",
				(char *)VECTOR_SLOT(strvec, 0));
		return alloc;
	}
	/* Even empty quotes counts as a value (An empty string) */
	alloc = (char *) MALLOC(sizeof (char));
	if (!alloc) {
		condlog(0, "can't allocate memeory for option '%s'",
			(char *)VECTOR_SLOT(strvec, 0));
		return NULL;
	}
	for (i = 2; i < VECTOR_SIZE(strvec); i++) {
		str = VECTOR_SLOT(strvec, i);
		if (!str) {
			free(alloc);
			condlog(0, "parse error for option '%s'",
				(char *)VECTOR_SLOT(strvec, 0));
			return NULL;
		}
		if (*str == '"')
			break;
		tmp = alloc;
		/* The first +1 is for the NULL byte. The rest are for the
		 * spaces between words */
		len += strlen(str) + 1;
		alloc = REALLOC(alloc, sizeof (char) * len);
		if (!alloc) {
			FREE(tmp);
			condlog(0, "can't allocate memeory for option '%s'",
				(char *)VECTOR_SLOT(strvec, 0));
			return NULL;
		}
		if (*alloc != '\0')
			strncat(alloc, " ", 1);
		strncat(alloc, str, strlen(str));
	}
	return alloc;
}

/* non-recursive configuration stream handler */
static int kw_level = 0;

int warn_on_duplicates(vector uniques, char *str, char *file)
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
		strcmp(str, "blacklist_exceptions") == 0 ||
		strcmp(str, "devices") == 0 || strcmp(str, "devices") == 0 ||
		strcmp(str, "device") == 0 || strcmp(str, "multipaths") == 0 ||
		strcmp(str, "multipath") == 0);
}

int
validate_config_strvec(vector strvec, char *file)
{
	char *str;
	int i;

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
		str = VECTOR_SLOT(strvec, 1);
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
	str = VECTOR_SLOT(strvec, 1);
	if (str == NULL) {
		condlog(0, "missing value for option '%s' on line %d of %s",
			(char *)VECTOR_SLOT(strvec, 0), line_nr, file);
		return -1;
	}
	if (*str != '"') {
		if (VECTOR_SIZE(strvec) > 2)
			condlog(0, "ignoring extra data starting with '%s' on line %d of %s", (char *)VECTOR_SLOT(strvec, 2), line_nr, file);
		return 0;
	}
	for (i = 2; i < VECTOR_SIZE(strvec); i++) {
		str = VECTOR_SLOT(strvec, i);
		if (str == NULL) {
			condlog(0, "can't parse value on line %d of %s",
				line_nr, file);
			return -1;
		}
		if (*str == '"') {
			if (VECTOR_SIZE(strvec) > i + 1)
				condlog(0, "ignoring extra data starting with '%s' on line %d of %s", (char *)VECTOR_SLOT(strvec, (i + 1)), line_nr, file);
			return 0;
		}
	}
	condlog(0, "missing closing quotes on line %d of %s",
		line_nr, file);
	return 0;
}

static int
process_stream(struct config *conf, FILE *stream, vector keywords, char *file)
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

	buf = MALLOC(MAXBUF);

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
				break;
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
				    t = (*keyword->handler) (conf, strvec);
					r += t;
					if (t)
						condlog(1, "multipath.conf +%d, parsing failed: %s",
							line_nr, buf);
				}

				if (keyword->sub) {
					kw_level++;
					r += process_stream(conf, stream,
							    keyword->sub, file);
					kw_level--;
				}
				break;
			}
		}
		if (i >= VECTOR_SIZE(keywords))
			condlog(1, "%s line %d, invalid keyword: %s",
				file, line_nr, str);

		free_strvec(strvec);
	}

out:
	FREE(buf);
	free_uniques(uniques);
	return r;
}

int alloc_keywords(void)
{
	if (!keywords)
		keywords = vector_alloc();

	if (!keywords)
		return 1;

	return 0;
}

/* Data initialization */
int
process_file(struct config *conf, char *file)
{
	int r;
	FILE *stream;

	if (!keywords) {
		condlog(0, "No keywords alocated");
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
	r = process_stream(conf, stream, keywords, file);
	fclose(stream);
	//free_keywords(keywords);

	return r;
}
