/* 
 * Soft:        Keepalived is a failover program for the LVS project
 *              <www.linuxvirtualserver.org>. It monitor & manipulate
 *              a loadbalanced server pool using multi-layer checks.
 * 
 * Part:        vector.c include file.
 *  
 * Version:     $Id: vector.h,v 1.0.3 2003/05/11 02:28:03 acassen Exp $
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

#ifndef _VECTOR_H
#define _VECTOR_H

/* vector definition */
struct _vector {
	int allocated;
	void **slot;
};
typedef struct _vector *vector;

#define VECTOR_DEFAULT_SIZE 1
#define VECTOR_SIZE(V)   ((V) ? ((V)->allocated) / VECTOR_DEFAULT_SIZE : 0)
#define VECTOR_SLOT(V,E) (((V) && (E) < VECTOR_SIZE(V)) ? (V)->slot[(E)] : NULL)
#define VECTOR_LAST_SLOT(V)   (((V) && VECTOR_SIZE(V) > 0) ? (V)->slot[(VECTOR_SIZE(V) - 1)] : NULL)

#define vector_foreach_slot(v,p,i) \
	for (i = 0; (v) && i < VECTOR_SIZE(v) && ((p) = (v)->slot[i]); i++)
#define vector_foreach_slot_after(v,p,i) \
	for (; (v) && i < VECTOR_SIZE(v) && ((p) = (v)->slot[i]); i++)
#define vector_foreach_slot_backwards(v,p,i) \
	for (i = VECTOR_SIZE(v); i > 0 && ((p) = (v)->slot[i-1]); i--)

/* Prototypes */
extern vector vector_alloc(void);
extern void *vector_alloc_slot(vector v);
extern void vector_free(vector v);
extern void free_strvec(vector strvec);
extern void vector_set_slot(vector v, void *value);
extern void vector_del_slot(vector v, int slot);
extern void *vector_insert_slot(vector v, int slot, void *value);
int find_slot(vector v, void * addr);
extern void vector_repack(vector v);
extern void vector_dump(vector v);
extern void dump_strvec(vector strvec);
extern int vector_move_up(vector v, int src, int dest);
#endif
