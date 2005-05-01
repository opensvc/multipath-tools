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

/* Global definitions */
#define EOB  "}"
#define MAXBUF	1024

/* ketword definition */
struct keyword {
	char *string;
	int (*handler) (vector);
	vector sub;
};

/* Reloading helpers */
#define SET_RELOAD      (reload = 1)
#define UNSET_RELOAD    (reload = 0)
#define RELOAD_DELAY    5

/* global var exported */
vector keywords;
FILE *stream;

/* Prototypes */
extern int keyword_alloc(vector keywords, char *string, int (*handler) (vector));
extern int install_keyword_root(char *string, int (*handler) (vector));
extern void install_sublevel(void);
extern void install_sublevel_end(void);
extern int install_keyword(char *string, int (*handler) (vector));
extern void dump_keywords(vector keydump, int level);
extern void free_keywords(vector keywords);
extern vector alloc_strvec(char *string);
extern int read_line(char *buf, int size);
extern vector read_value_block(void);
extern int alloc_value_block(vector strvec, void (*alloc_func) (vector));
extern void *set_value(vector strvec);
extern int process_stream(vector keywords);
extern int init_data(char *conf_file, vector (*init_keywords) (void));

#endif
