/*
 * Part:        Vector structure manipulation.
 *
 * Version:     $Id: vector.c,v 1.0.3 2003/05/11 02:28:03 acassen Exp $
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
 *
 * Copyright (c) 2002, 2003, 2004 Alexandre Cassen
 * Copyright (c) 2005 Christophe Varoqui
 */

#include "memory.h"
#include <stdlib.h>
#include "vector.h"

/*
 * Initialize vector struct.
 * allocated 'size' slot elements then return vector.
 */
vector
vector_alloc(void)
{
	vector v = (vector) MALLOC(sizeof (struct _vector));
	return v;
}

/* allocated one slot */
void *
vector_alloc_slot(vector v)
{
	void *new_slot = NULL;

	if (!v)
		return NULL;

	v->allocated += VECTOR_DEFAULT_SIZE;
	if (v->slot)
		new_slot = REALLOC(v->slot, sizeof (void *) * v->allocated);
	else
		new_slot = (void *) MALLOC(sizeof (void *) * v->allocated);

	if (!new_slot)
		v->allocated -= VECTOR_DEFAULT_SIZE;
	else
		v->slot = new_slot;

	return v->slot;
}

int
vector_move_up(vector v, int src, int dest)
{
	void *value;
	int i;
	if (dest == src)
		return 0;
	if (dest > src || src >= v->allocated)
		return -1;
	value = v->slot[src];
	for (i = src - 1; i >= dest; i--)
		v->slot[i + 1] = v->slot[i];
	v->slot[dest] = value;
	return 0;
}

void *
vector_insert_slot(vector v, int slot, void *value)
{
	int i;

	if (!vector_alloc_slot(v))
		return NULL;

	for (i = VECTOR_SIZE(v) - 2; i >= slot; i--)
		v->slot[i + 1] = v->slot[i];

	v->slot[slot] = value;

	return v->slot[slot];
}

int
find_slot(vector v, void * addr)
{
	int i;

	if (!v)
		return -1;

	for (i = 0; i < VECTOR_SIZE(v); i++)
		if (v->slot[i] == addr)
			return i;

	return -1;
}

void
vector_del_slot(vector v, int slot)
{
	int i;

	if (!v || !v->allocated || slot < 0 || slot > VECTOR_SIZE(v))
		return;

	for (i = slot + 1; i < VECTOR_SIZE(v); i++)
		v->slot[i-1] = v->slot[i];

	v->allocated -= VECTOR_DEFAULT_SIZE;

	if (v->allocated <= 0) {
		FREE(v->slot);
		v->slot = NULL;
		v->allocated = 0;
	} else {
		void *new_slot;

		new_slot = REALLOC(v->slot, sizeof (void *) * v->allocated);
		if (!new_slot)
			v->allocated += VECTOR_DEFAULT_SIZE;
		else
			v->slot = new_slot;
	}
}

void
vector_repack(vector v)
{
	int i;

	if (!v || !v->allocated)
		return;

	for (i = 0; i < VECTOR_SIZE(v); i++)
		if (i > 0 && v->slot[i] == NULL)
			vector_del_slot(v, i--);
}

vector
vector_reset(vector v)
{
	if (!v)
		return NULL;

	if (v->slot)
		FREE(v->slot);

	v->allocated = 0;
	v->slot = NULL;
	return v;
}

/* Free memory vector allocation */
void
vector_free(vector v)
{
	if (!vector_reset(v))
		return;
	FREE(v);
}

void
free_strvec(vector strvec)
{
	int i;
	char *str;

	if (!strvec)
		return;

	vector_foreach_slot (strvec, str, i)
		if (str)
			FREE(str);

	vector_free(strvec);
}

/* Set a vector slot value */
void
vector_set_slot(vector v, void *value)
{
	unsigned int i;

	if (!v)
		return;

	i = VECTOR_SIZE(v) - 1;
	v->slot[i] = value;
}

int vector_find_or_add_slot(vector v, void *value)
{
	int n = find_slot(v, value);

	if (n >= 0)
		return n;
	if (vector_alloc_slot(v) == NULL)
		return -1;
	vector_set_slot(v, value);
	return VECTOR_SIZE(v) - 1;
}
