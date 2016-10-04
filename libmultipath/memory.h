/*
 * Part:        memory.c include file.
 *
 * Version:     $Id: memory.h,v 1.1.11 2005/03/01 01:22:13 acassen Exp $
 *
 * Authors:     Alexandre Cassen, <acassen@linux-vs.org>
 *              Jan Holmberg, <jan@artech.net>
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
 *
 * Copyright (C) 2001-2005 Alexandre Cassen, <acassen@linux-vs.org>
 */

#ifndef _MEMORY_H
#define _MEMORY_H

/* system includes */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Local defines */
#ifdef _DEBUG_

int debug;

#define MAX_ALLOC_LIST 2048

#define MALLOC(n)    ( dbg_malloc((n), \
		      (__FILE__), (char *)(__FUNCTION__), (__LINE__)) )
#define FREE(b)      ( dbg_free((b), \
		      (__FILE__), (char *)(__FUNCTION__), (__LINE__)) )
#define REALLOC(b,n) ( dbg_realloc((b), (n), \
		      (__FILE__), (char *)(__FUNCTION__), (__LINE__)) )
#define STRDUP(n)    ( dbg_strdup((n), \
		      (__FILE__), (char *)(__FUNCTION__), (__LINE__)) )

/* Memory debug prototypes defs */
extern void *dbg_malloc(unsigned long, char *, char *, int);
extern int dbg_free(void *, char *, char *, int);
extern void *dbg_realloc(void *, unsigned long, char *, char *, int);
extern char *dbg_strdup(char *, char *, char *, int);
extern void dbg_free_final(char *);

#else

#define MALLOC(n)    (calloc(1,(n)))
#define FREE(p)      do { free(p); p = NULL; } while(0)
#define REALLOC(p,n) (realloc((p),(n)))
#define STRDUP(n)    (strdup(n))

#endif

/* Common defines */
#define FREE_PTR(P) if((P)) FREE((P));

#endif
