// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Part:        Vector structure manipulation.
 *
 * Version:     $Id: vector.c,v 1.0.3 2003/05/11 02:28:03 acassen Exp $
 *
 * Author:      Alexandre Cassen, <acassen@linux-vs.org>
 *
 * Copyright (c) 2002, 2003, 2004 Alexandre Cassen
 * Copyright (c) 2005 Christophe Varoqui
 */

#include <stdlib.h>
#include "vector.h"
#include "msort.h"

/*
 * Initialize vector struct.
 * allocated 'size' slot elements then return vector.
 */
vector
vector_alloc(void)
{
	vector v = (vector) calloc(1, sizeof (struct vector_s));
	return v;
}

/* allocated one slot */
bool
vector_alloc_slot(vector v)
{
	void *new_slot = NULL;
	int new_allocated;
	int i;

	if (!v)
		return false;

	new_allocated = v->allocated + 1;
	new_slot = realloc(v->slot, sizeof (void *) * new_allocated);
	if (!new_slot)
		return false;

	v->slot = new_slot;
	for (i = v->allocated; i < new_allocated; i++)
		v->slot[i] = NULL;

	v->allocated = new_allocated;
	return true;
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

int find_slot(vector v, const void *addr)
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

	if (!v || !v->allocated || slot < 0 || slot >= VECTOR_SIZE(v))
		return;

	for (i = slot + 1; i < VECTOR_SIZE(v); i++)
		v->slot[i - 1] = v->slot[i];

	v->allocated--;

	if (v->allocated <= 0) {
		free(v->slot);
		v->slot = NULL;
		v->allocated = 0;
	} else {
		void *new_slot;

		new_slot = realloc(v->slot, sizeof (void *) * v->allocated);
		/*
		 * If realloc() fails, v->allocated will be smaller than the
		 * actual allocated size vector size.
		 * This is intentional; we want VECTOR_SIZE() to return
		 * the number of used elements. Otherwise, vector_for_each_slot()
		 * et al. wouldn't work as intended; there might be duplicate
		 * or stale elements at the end of the vector.
		 */
		if (new_slot)
			v->slot = new_slot;
	}
}

vector
vector_reset(vector v)
{
	if (!v)
		return NULL;

	if (v->slot)
		free(v->slot);

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
	free(v);
}

void cleanup_vector(vector *pv)
{
	if (*pv)
		vector_free(*pv);
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
			free(str);

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
	if (!vector_alloc_slot(v))
		return -1;
	vector_set_slot(v, value);
	return VECTOR_SIZE(v) - 1;
}

void vector_sort(vector v, int (*compar)(const void *, const void *))
{
	if (!v || !v->slot || !v->allocated)
		return;
	return msort((void *)v->slot, v->allocated, sizeof(void *), compar);

}
