#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <libudev.h>

#include "debug.h"
#include "util.h"
#include "prio.h"
#include "structs.h"
#include "discovery.h"

static const char * const prio_dir = MULTIPATH_DIR;
static LIST_HEAD(prioritizers);

unsigned int get_prio_timeout_ms(const struct path *pp)
{
	if (pp->state == PATH_DOWN)
		return 10;
	else if (pp->checker_timeout)
		return pp->checker_timeout * 1000;
	else
		return DEF_TIMEOUT;
}

int init_prio(void)
{
#ifdef LOAD_ALL_SHARED_LIBS
	static const char *const all_prios[] = {
		PRIO_ALUA,
		PRIO_CONST,
		PRIO_DATACORE,
		PRIO_EMC,
		PRIO_HDS,
		PRIO_HP_SW,
		PRIO_ONTAP,
		PRIO_RANDOM,
		PRIO_RDAC,
		PRIO_WEIGHTED_PATH,
		PRIO_SYSFS,
		PRIO_PATH_LATENCY,
		PRIO_ANA,
	};
	unsigned int i;

	for  (i = 0; i < ARRAY_SIZE(all_prios); i++)
		add_prio(all_prios[i]);
#else
	if (!add_prio(DEFAULT_PRIO))
		return 1;
#endif
	return 0;
}

static struct prio * alloc_prio (void)
{
	struct prio *p;

	p = calloc(1, sizeof(struct prio));
	if (p) {
		INIT_LIST_HEAD(&p->node);
		p->refcount = 1;
	}
	return p;
}

void free_prio (struct prio * p)
{
	if (!p)
		return;
	p->refcount--;
	if (p->refcount) {
		condlog(4, "%s prioritizer refcount %d",
			p->name, p->refcount);
		return;
	}
	condlog(3, "unloading %s prioritizer", p->name);
	list_del(&p->node);
	if (p->handle) {
		if (dlclose(p->handle) != 0) {
			condlog(0, "Cannot unload prioritizer %s: %s",
				p->name, dlerror());
		}
	}
	free(p);
}

void cleanup_prio(void)
{
	struct prio * prio_loop;
	struct prio * prio_temp;

	list_for_each_entry_safe(prio_loop, prio_temp, &prioritizers, node) {
		free_prio(prio_loop);
	}
}

static struct prio *prio_lookup(const char *name)
{
	struct prio * p;

	if (!name || !strlen(name))
		return NULL;

	list_for_each_entry(p, &prioritizers, node) {
		if (!strncmp(name, p->name, PRIO_NAME_LEN))
			return p;
	}
	return NULL;
}

int prio_set_args (struct prio * p, const char * args)
{
	return snprintf(p->args, PRIO_ARGS_LEN, "%s", args);
}

struct prio *add_prio (const char *name)
{
	char libname[LIB_PRIO_NAMELEN];
	struct stat stbuf;
	struct prio * p;
	char *errstr;

	p = alloc_prio();
	if (!p)
		return NULL;
	snprintf(p->name, PRIO_NAME_LEN, "%s", name);
	snprintf(libname, LIB_PRIO_NAMELEN, "%s/libprio%s.so",
		 prio_dir, name);
	if (stat(libname,&stbuf) < 0) {
		condlog(0,"Prioritizer '%s' not found in %s",
			name, prio_dir);
		goto out;
	}
	condlog(3, "loading %s prioritizer", libname);
	p->handle = dlopen(libname, RTLD_NOW);
	if (!p->handle) {
		if ((errstr = dlerror()) != NULL)
			condlog(0, "A dynamic linking error occurred: (%s)",
				errstr);
		goto out;
	}
	p->getprio = (int (*)(struct path *, char *)) dlsym(p->handle, "getprio");
	errstr = dlerror();
	if (errstr != NULL)
		condlog(0, "A dynamic linking error occurred: (%s)", errstr);
	if (!p->getprio)
		goto out;
	list_add(&p->node, &prioritizers);
	return p;
out:
	free_prio(p);
	return NULL;
}

int prio_getprio (struct prio * p, struct path * pp)
{
	return p->getprio(pp, p->args);
}

int prio_selected (const struct prio * p)
{
	if (!p)
		return 0;
	return (p->getprio) ? 1 : 0;
}

const char * prio_name (const struct prio * p)
{
	return p->name;
}

const char * prio_args (const struct prio * p)
{
	return p->args;
}

void prio_get(struct prio *dst, const char *name, const char *args)
{
	struct prio * src = NULL;

	if (!dst)
		return;

	if (name && strlen(name)) {
		src = prio_lookup(name);
		if (!src)
			src = add_prio(name);
	}
	if (!src) {
		dst->getprio = NULL;
		return;
	}

	strncpy(dst->name, src->name, PRIO_NAME_LEN);
	if (args)
		strlcpy(dst->args, args, PRIO_ARGS_LEN);
	dst->getprio = src->getprio;
	dst->handle = NULL;

	src->refcount++;
}

void prio_put (struct prio * dst)
{
	struct prio * src;

	if (!dst || !dst->getprio)
		return;

	src = prio_lookup(dst->name);
	memset(dst, 0x0, sizeof(struct prio));
	free_prio(src);
}
