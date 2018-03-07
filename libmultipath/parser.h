/*
 * Soft:        Keepalived is a failover program for the LVS project
 *              <www.linuxvirtualserver.org>. It monitor & manipulate
 *              a loadbalanced server pool using multi-layer checks.
 *
 * Part:        cfreader.c include file.
 *
 * Version:     $Id: parser.h,v 1.0.3 2003/05/11 02:28:03 acassen Exp $
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

#ifndef _PARSER_H
#define _PARSER_H

/* system includes */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <syslog.h>
#include <ctype.h>

/* local includes */
#include "vector.h"
#include "config.h"

/* Global definitions */
#define EOB  "}"
#define MAXBUF	1024

/* ketword definition */
struct keyword {
	char *string;
	int (*handler) (struct config *, vector);
	int (*print) (struct config *, char *, int, const void *);
	vector sub;
	int unique;
};

/* Reloading helpers */
#define SET_RELOAD      (reload = 1)
#define UNSET_RELOAD    (reload = 0)
#define RELOAD_DELAY    5

/* iterator helper */
#define iterate_sub_keywords(k,p,i) \
	for (i = 0; i < (k)->sub->allocated && ((p) = (k)->sub->slot[i]); i++)

/* Prototypes */
extern int keyword_alloc(vector keywords, char *string,
			 int (*handler) (struct config *, vector),
			 int (*print) (struct config *, char *, int,
				       const void *),
			 int unique);
#define install_keyword_root(str, h) keyword_alloc(keywords, str, h, NULL, 1)
extern void install_sublevel(void);
extern void install_sublevel_end(void);
extern int _install_keyword(vector keywords, char *string,
			    int (*handler) (struct config *, vector),
			    int (*print) (struct config *, char *, int,
					  const void *),
			    int unique);
#define install_keyword(str, vec, pri) _install_keyword(keywords, str, vec, pri, 1)
#define install_keyword_multi(str, vec, pri) _install_keyword(keywords, str, vec, pri, 0)
extern void dump_keywords(vector keydump, int level);
extern void free_keywords(vector keywords);
extern vector alloc_strvec(char *string);
extern void *set_value(vector strvec);
extern int process_file(struct config *conf, char *conf_file);
extern struct keyword * find_keyword(vector keywords, vector v, char * name);
int snprint_keyword(char *buff, int len, char *fmt, struct keyword *kw,
		    const void *data);
bool is_quote(const char* token);

#endif
