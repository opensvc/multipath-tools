#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <dlfcn.h>

#include "debug.h"
#include "checkers.h"
#include "vector.h"
#include "config.h"

char *checker_state_names[] = {
      "wild",
      "unchecked",
      "down",
      "up",
      "shaky",
      "ghost",
      "pending"
};

static LIST_HEAD(checkers);

char * checker_state_name (int i)
{
	return checker_state_names[i];
}

int init_checkers (void)
{
	if (!add_checker(DEFAULT_CHECKER))
		return 1;
	return 0;
}

struct checker * alloc_checker (void)
{
	return MALLOC(sizeof(struct checker));
}

void free_checker (struct checker * c)
{
	FREE(c);
}

void cleanup_checkers (void)
{
	struct checker * checker_loop;
	struct checker * checker_temp;

	list_for_each_entry_safe(checker_loop, checker_temp, &checkers, node) {
		list_del(&checker_loop->node);
		free_checker(checker_loop);
	}
}

struct checker * checker_lookup (char * name)
{
	struct checker * c;

	list_for_each_entry(c, &checkers, node) {
		if (!strncmp(name, c->name, CHECKER_NAME_LEN))
			return c;
	}
	return add_checker(name);
}

struct checker * add_checker (char * name)
{
	char libname[LIB_CHECKER_NAMELEN];
	void * handle;
	struct checker * c;
	char *errstr;

	c = alloc_checker();
	if (!c)
		return NULL;
	snprintf(libname, LIB_CHECKER_NAMELEN, "%s/libcheck%s.so",
		 conf->multipath_dir, name);
	condlog(3, "loading %s checker", libname);
	handle = dlopen(libname, RTLD_NOW);
	errstr = dlerror();
	if (errstr != NULL)
	condlog(0, "A dynamic linking error occurred: (%s)", errstr);
	if (!handle)
		goto out;

	c->check = (int (*)(struct checker *)) dlsym(handle, "libcheck_check");
	errstr = dlerror();
	if (errstr != NULL)
	condlog(0, "A dynamic linking error occurred: (%s)", errstr);
	if (!c->check)
		goto out;

	c->init = (int (*)(struct checker *)) dlsym(handle, "libcheck_init");
	errstr = dlerror();
	if (errstr != NULL)
	condlog(0, "A dynamic linking error occurred: (%s)", errstr);
	if (!c->init)
		goto out;

	c->free = (void (*)(struct checker *)) dlsym(handle, "libcheck_free");
	errstr = dlerror();
	if (errstr != NULL)
	condlog(0, "A dynamic linking error occurred: (%s)", errstr);
	if (!c->free)
		goto out;

	snprintf(c->name, CHECKER_NAME_LEN, "%s", name);
	c->fd = 0;
	c->sync = 1;
	list_add(&c->node, &checkers);
	return c;
out:
	free_checker(c);
	return NULL;
}

void checker_set_fd (struct checker * c, int fd)
{
	c->fd = fd;
}

void checker_set_sync (struct checker * c)
{
	c->sync = 1;
}

void checker_set_async (struct checker * c)
{
	c->sync = 0;
}

void checker_enable (struct checker * c)
{
	c->disable = 0;
}

void checker_disable (struct checker * c)
{
	c->disable = 1;
}

int checker_init (struct checker * c, void ** mpctxt_addr)
{
	c->mpcontext = mpctxt_addr;
	return c->init(c);
}

void checker_put (struct checker * c)
{
	if (c->free)
		c->free(c);
	memset(c, 0x0, sizeof(struct checker));
}

int checker_check (struct checker * c)
{
	int r;

	if (c->disable)
		return PATH_UNCHECKED;
	if (c->fd <= 0) {
		MSG(c, "no usable fd");
		return PATH_WILD;
	}
	r = c->check(c);

	return r;
}

int checker_selected (struct checker * c)
{
	return (c->check) ? 1 : 0;
}

char * checker_name (struct checker * c)
{
	return c->name;
}

char * checker_message (struct checker * c)
{
	return c->message;
}

void checker_get (struct checker * dst, char * name)
{
	struct checker * src = checker_lookup(name);

	if (!src) {
		dst->check = NULL;
		return;
	}
	dst->fd = src->fd;
	dst->sync = src->sync;
	strncpy(dst->name, src->name, CHECKER_NAME_LEN);
	strncpy(dst->message, src->message, CHECKER_MSG_LEN);
	dst->check = src->check;
	dst->init = src->init;
	dst->free = src->free;
}
