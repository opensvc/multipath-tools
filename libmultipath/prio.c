#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <dlfcn.h>

#include "debug.h"
#include "prio.h"

static LIST_HEAD(prioritizers);

int init_prio (void)
{
	INIT_LIST_HEAD(&prioritizers);
	if (!add_prio(DEFAULT_PRIO))
		return 1;
	return 0;
}

struct prio * alloc_prio (void)
{
	return zalloc(sizeof(struct prio));
}

void free_prio (struct prio * p)
{
	free(p);
}

void cleanup_prio(void)
{
        struct prio * prio_loop;
        struct prio * prio_temp;

        list_for_each_entry_safe(prio_loop, prio_temp, &prioritizers, node) {
                list_del(&prio_loop->node);
                free(prio_loop);
        }
}

struct prio * prio_lookup (char * name)
{
        struct prio * p;

	list_for_each_entry(p, &prioritizers, node) {
		if (!strncmp(name, p->name, PRIO_NAME_LEN))
			return p;
	}
	p = add_prio(name);
	if (p)
		return p;
	return prio_default();
}

struct prio * add_prio (char * name)
{
	char libname[LIB_PRIO_NAMELEN];
	void * handle;
	struct prio * p;
	char *errstr;

	p = alloc_prio();
	if (!p)
		return NULL;
	snprintf(libname, LIB_PRIO_NAMELEN, "libprio%s.so", name);
	condlog(3, "loading %s prioritizer", libname);
	handle = dlopen(libname, RTLD_NOW);
	errstr = dlerror();
	if (errstr != NULL)
	condlog(0, "A dynamic linking error occurred: (%s)", errstr);
	if (!handle)
		goto out;
	p->getprio = (int (*)(struct path *)) dlsym(handle, "getprio");
	errstr = dlerror();
	if (errstr != NULL)
	condlog(0, "A dynamic linking error occurred: (%s)", errstr);
	if (!p->getprio)
		goto out;
	snprintf(p->name, PRIO_NAME_LEN, "%s", name);
	list_add(&p->node, &prioritizers);
	return p;
out:
	free_prio(p);
	return NULL;
}

int prio_getprio (struct prio * p, struct path * pp)
{
	return p->getprio(pp);
}

char * prio_name (struct prio * p)
{
	return p->name;
}

struct prio * prio_default (void)
{
	return prio_lookup(DEFAULT_PRIO);
}
