#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <dlfcn.h>
#include <sys/stat.h>

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
      "pending",
      "timeout",
      "removed",
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
	struct checker *c;

	c = MALLOC(sizeof(struct checker));
	if (c) {
		INIT_LIST_HEAD(&c->node);
		c->refcount = 1;
	}
	return c;
}

void free_checker (struct checker * c)
{
	if (!c)
		return;
	c->refcount--;
	if (c->refcount) {
		condlog(3, "%s checker refcount %d",
			c->name, c->refcount);
		return;
	}
	condlog(3, "unloading %s checker", c->name);
	list_del(&c->node);
	if (c->handle) {
		if (dlclose(c->handle) != 0) {
			condlog(0, "Cannot unload checker %s: %s",
				c->name, dlerror());
		}
	}
	FREE(c);
}

void cleanup_checkers (void)
{
	struct checker * checker_loop;
	struct checker * checker_temp;

	list_for_each_entry_safe(checker_loop, checker_temp, &checkers, node) {
		free_checker(checker_loop);
	}
}

struct checker * checker_lookup (char * name)
{
	struct checker * c;

	if (!name || !strlen(name))
		return NULL;
	list_for_each_entry(c, &checkers, node) {
		if (!strncmp(name, c->name, CHECKER_NAME_LEN))
			return c;
	}
	return add_checker(name);
}

struct checker * add_checker (char * name)
{
	char libname[LIB_CHECKER_NAMELEN];
	struct stat stbuf;
	struct checker * c;
	char *errstr;

	c = alloc_checker();
	if (!c)
		return NULL;
	snprintf(c->name, CHECKER_NAME_LEN, "%s", name);
	snprintf(libname, LIB_CHECKER_NAMELEN, "%s/libcheck%s.so",
		 conf->multipath_dir, name);
	if (stat(libname,&stbuf) < 0) {
		condlog(0,"Checker '%s' not found in %s",
			name, conf->multipath_dir);
		goto out;
	}
	condlog(3, "loading %s checker", libname);
	c->handle = dlopen(libname, RTLD_NOW);
	if (!c->handle) {
		if ((errstr = dlerror()) != NULL)
			condlog(0, "A dynamic linking error occurred: (%s)",
				errstr);
		goto out;
	}
	c->check = (int (*)(struct checker *)) dlsym(c->handle, "libcheck_check");
	errstr = dlerror();
	if (errstr != NULL)
		condlog(0, "A dynamic linking error occurred: (%s)", errstr);
	if (!c->check)
		goto out;

	c->init = (int (*)(struct checker *)) dlsym(c->handle, "libcheck_init");
	errstr = dlerror();
	if (errstr != NULL)
		condlog(0, "A dynamic linking error occurred: (%s)", errstr);
	if (!c->init)
		goto out;

	c->free = (void (*)(struct checker *)) dlsym(c->handle, "libcheck_free");
	errstr = dlerror();
	if (errstr != NULL)
		condlog(0, "A dynamic linking error occurred: (%s)", errstr);
	if (!c->free)
		goto out;

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
	if (!c)
		return;
	c->fd = fd;
}

void checker_set_sync (struct checker * c)
{
	if (!c)
		return;
	c->sync = 1;
}

void checker_set_async (struct checker * c)
{
	if (!c)
		return;
	c->sync = 0;
}

void checker_enable (struct checker * c)
{
	if (!c)
		return;
	c->disable = 0;
}

void checker_disable (struct checker * c)
{
	if (!c)
		return;
	c->disable = 1;
}

int checker_init (struct checker * c, void ** mpctxt_addr)
{
	if (!c)
		return 1;
	c->mpcontext = mpctxt_addr;
	return c->init(c);
}

void checker_put (struct checker * dst)
{
	struct checker * src;

	if (!dst)
		return;
	src = checker_lookup(dst->name);
	if (dst->free)
		dst->free(dst);
	memset(dst, 0x0, sizeof(struct checker));
	free_checker(src);
}

int checker_check (struct checker * c)
{
	int r;

	if (!c)
		return PATH_WILD;

	c->message[0] = '\0';
	if (c->disable) {
		MSG(c, "checker disabled");
		return PATH_UNCHECKED;
	}
	if (c->fd <= 0) {
		MSG(c, "no usable fd");
		return PATH_WILD;
	}
	r = c->check(c);

	return r;
}

int checker_selected (struct checker * c)
{
	if (!c)
		return 0;
	return (c->check) ? 1 : 0;
}

char * checker_name (struct checker * c)
{
	if (!c)
		return NULL;
	return c->name;
}

char * checker_message (struct checker * c)
{
	if (!c)
		return NULL;
	return c->message;
}

void checker_clear_message (struct checker *c)
{
	if (!c)
		return;
	c->message[0] = '\0';
}

void checker_get (struct checker * dst, char * name)
{
	struct checker * src = checker_lookup(name);

	if (!dst)
		return;

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
	dst->handle = NULL;
	src->refcount++;
}
