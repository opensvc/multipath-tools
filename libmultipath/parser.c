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

#include "parser.h"
#include "memory.h"

/* local vars */
static int sublevel = 0;

int
keyword_alloc(vector keywords, char *string, int (*handler) (vector))
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

	vector_set_slot(keywords, keyword);

	return 0;
}

int
install_keyword_root(char *string, int (*handler) (vector))
{
	return keyword_alloc(keywords, string, handler);
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
install_keyword(char *string, int (*handler) (vector))
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
	return keyword_alloc(keyword->sub, string, handler);
}

void
free_keywords(vector keywords)
{
	struct keyword *keyword;
	int i;

	for (i = 0; i < VECTOR_SIZE(keywords); i++) {
		keyword = VECTOR_SLOT(keywords, i);
		if (keyword->sub)
			free_keywords(keyword->sub);
		FREE(keyword);
	}
	vector_free(keywords);
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
	while (isspace((int) *cp) && *cp != '\0')
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

		} else {
			while ((in_string || !isspace((int) *cp)) && *cp
				!= '\0' && *cp != '"')
				cp++;
			strlen = cp - start;
			token = MALLOC(strlen + 1);

			if (!token)
				goto out;

			memcpy(token, start, strlen);
			*(token + strlen) = '\0';
		}
		vector_set_slot(strvec, token);

		while (isspace((int) *cp) && *cp != '\0')
			cp++;
		if (*cp == '\0' || *cp == '!' || *cp == '#')
			return strvec;
	}
out:
	vector_free(strvec);
	return NULL;
}

int
read_line(char *buf, int size)
{
	int ch;
	int count = 0;

	while ((ch = fgetc(stream)) != EOF && (int) ch != '\n'
	       && (int) ch != '\r') {
		if (count < size)
			buf[count] = (int) ch;
		else
			break;
		count++;
	}
	return (ch == EOF) ? 0 : 1;
}

vector
read_value_block(void)
{
	char *buf;
	int i;
	char *str = NULL;
	char *dup;
	vector vec = NULL;
	vector elements = vector_alloc();

	buf = (char *) MALLOC(MAXBUF);

	if (!buf)
		return NULL;

	if (!elements)
		goto out;

	while (read_line(buf, MAXBUF)) {
		vec = alloc_strvec(buf);
		if (vec) {
			str = VECTOR_SLOT(vec, 0);
			if (!strcmp(str, EOB)) {
				free_strvec(vec);
				break;
			}

			if (VECTOR_SIZE(vec))
				for (i = 0; i < VECTOR_SIZE(vec); i++) {
					str = VECTOR_SLOT(vec, i);
					dup = (char *) MALLOC(strlen(str) + 1);
					memcpy(dup, str, strlen(str));

					if (!vector_alloc_slot(elements))
						goto out1;

					vector_set_slot(elements, dup);
				}
			free_strvec(vec);
		}
		memset(buf, 0, MAXBUF);
	}
	FREE(buf);
	return elements;
out1:
	FREE(dup);
out:
	FREE(buf);
	return NULL;
}

int
alloc_value_block(vector strvec, void (*alloc_func) (vector))
{
	char *buf;
	char *str = NULL;
	vector vec = NULL;

	buf = (char *) MALLOC(MAXBUF);

	if (!buf)
		return 1;

	while (read_line(buf, MAXBUF)) {
		vec = alloc_strvec(buf);
		if (vec) {
			str = VECTOR_SLOT(vec, 0);
			if (!strcmp(str, EOB)) {
				free_strvec(vec);
				break;
			}

			if (VECTOR_SIZE(vec))
				(*alloc_func) (vec);

			free_strvec(vec);
		}
		memset(buf, 0, MAXBUF);
	}
	FREE(buf);
	return 0;
}

void *
set_value(vector strvec)
{
	char *str = VECTOR_SLOT(strvec, 1);
	int size = strlen(str);
	int i = 0;
	int len = 0;
	char *alloc = NULL;
	char *tmp;

	if (*str == '"') {
		for (i = 2; i < VECTOR_SIZE(strvec); i++) {
			str = VECTOR_SLOT(strvec, i);
			len += strlen(str);
			if (!alloc)
				alloc =
				    (char *) MALLOC(sizeof (char *) *
						    (len + 1));
			else {
				alloc =
				    REALLOC(alloc, sizeof (char *) * (len + 1));
				tmp = VECTOR_SLOT(strvec, i-1);
				if (*str != '"' && *tmp != '"')
					strncat(alloc, " ", 1);
			}

			if (i != VECTOR_SIZE(strvec)-1)
				strncat(alloc, str, strlen(str));
		}
	} else {
		alloc = MALLOC(sizeof (char *) * (size + 1));
		memcpy(alloc, str, size);
	}
	return alloc;
}

/* non-recursive configuration stream handler */
static int kw_level = 0;
int
process_stream(vector keywords)
{
	int i;
	int r = 0;
	struct keyword *keyword;
	char *str;
	char *buf;
	vector strvec;

	buf = MALLOC(MAXBUF);

	if (!buf)
		return 1;

	while (read_line(buf, MAXBUF)) {
		strvec = alloc_strvec(buf);
		memset(buf,0, MAXBUF);

		if (!strvec)
			continue;

		str = VECTOR_SLOT(strvec, 0);

		if (!strcmp(str, EOB) && kw_level > 0) {
			free_strvec(strvec);
			break;
		}

		for (i = 0; i < VECTOR_SIZE(keywords); i++) {
			keyword = VECTOR_SLOT(keywords, i);

			if (!strcmp(keyword->string, str)) {
				if (keyword->handler)
					r += (*keyword->handler) (strvec);

				if (keyword->sub) {
					kw_level++;
					r += process_stream(keyword->sub);
					kw_level--;
				}
				break;
			}
		}
		
		free_strvec(strvec);
	}

	FREE(buf);
	return r;
}

/* Data initialization */
int
init_data(char *conf_file, vector (*init_keywords) (void))
{
	int r;

	stream = fopen(conf_file, "r");
	if (!stream) {
		syslog(LOG_WARNING, "Configuration file open problem");
		return 1;
	}

	/* Init Keywords structure */
	(*init_keywords) ();

/* Dump configuration *
  vector_dump(keywords);
  dump_keywords(keywords, 0);
*/

	/* Stream handling */
	r = process_stream(keywords);
	fclose(stream);
	free_keywords(keywords);

	return r;
}
