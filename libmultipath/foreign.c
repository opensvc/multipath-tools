/*
  Copyright (c) 2018 Martin Wilck, SUSE Linux GmbH

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <sys/sysmacros.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <dirent.h>
#include <fnmatch.h>
#include <dlfcn.h>
#include <libudev.h>
#include <regex.h>
#include "vector.h"
#include "debug.h"
#include "util.h"
#include "foreign.h"
#include "structs.h"
#include "structs_vec.h"
#include "print.h"

static vector foreigns;

/* This protects vector foreigns */
static pthread_rwlock_t foreign_lock = PTHREAD_RWLOCK_INITIALIZER;

static void rdlock_foreigns(void)
{
	pthread_rwlock_rdlock(&foreign_lock);
}

static void wrlock_foreigns(void)
{
	pthread_rwlock_wrlock(&foreign_lock);
}

static void unlock_foreigns(void *unused)
{
	pthread_rwlock_unlock(&foreign_lock);
}

#define get_dlsym(foreign, sym, lbl)					\
	do {								\
		foreign->sym =	dlsym(foreign->handle, #sym);		\
		if (foreign->sym == NULL) {				\
			condlog(0, "%s: symbol \"%s\" not found in \"%s\"", \
				__func__, #sym, foreign->name);		\
			goto lbl;					\
		}							\
	} while(0)

static void free_foreign(struct foreign *fgn)
{
	struct context *ctx;

	if (fgn == NULL)
		return;

	ctx = fgn->context;
	fgn->context = NULL;
	if (ctx != NULL)
		fgn->cleanup(ctx);

	if (fgn->handle != NULL)
		dlclose(fgn->handle);
	free(fgn);
}

void _cleanup_foreign(void)
{
	struct foreign *fgn;
	int i;

	if (foreigns == NULL)
		return;

	vector_foreach_slot_backwards(foreigns, fgn, i) {
		vector_del_slot(foreigns, i);
		free_foreign(fgn);
	}
	vector_free(foreigns);
	foreigns = NULL;
}

void cleanup_foreign(void)
{
	wrlock_foreigns();
	_cleanup_foreign();
	unlock_foreigns(NULL);
}

static const char foreign_pattern[] = "libforeign-*.so";

static int select_foreign_libs(const struct dirent *di)
{

	return fnmatch(foreign_pattern, di->d_name, FNM_FILE_NAME) == 0;
}

static void free_pre(void *arg)
{
	regex_t **pre = arg;

	if (pre != NULL && *pre != NULL) {
		regfree(*pre);
		free(*pre);
		*pre = NULL;
	}
}

static int _init_foreign(const char *multipath_dir, const char *enable)
{
	char pathbuf[PATH_MAX];
	struct dirent **di;
	struct scandir_result sr;
	int r, i;
	regex_t *enable_re = NULL;

	foreigns = vector_alloc();
	if (foreigns == NULL)
		return -ENOMEM;

	pthread_cleanup_push(free_pre, &enable_re);
	enable_re = calloc(1, sizeof(*enable_re));
	if (enable_re) {
		const char *str = enable ? enable : DEFAULT_ENABLE_FOREIGN;

		r = regcomp(enable_re, str, REG_EXTENDED|REG_NOSUB);
		if (r != 0) {
			char errbuf[64];

			(void)regerror(r, enable_re, errbuf, sizeof(errbuf));
			condlog (2, "%s: error compiling enable_foreign = \"%s\": \"%s\"",
				 __func__, str, errbuf);
			free_pre(&enable_re);
		}
	}

	r = scandir(multipath_dir, &di, select_foreign_libs, alphasort);

	if (r == 0) {
		condlog(3, "%s: no foreign multipath libraries found",
			__func__);
		return 0;
	} else if (r < 0) {
		r = errno;
		condlog(1, "%s: error %d scanning foreign multipath libraries",
			__func__, r);
		_cleanup_foreign();
		return -r;
	}

	sr.di = di;
	sr.n = r;
	pthread_cleanup_push_cast(free_scandir_result, &sr);
	for (i = 0; i < r; i++) {
		const char *msg, *fn, *c;
		struct foreign *fgn;
		int len, namesz;

		fn = di[i]->d_name;

		len = strlen(fn);
		c = strchr(fn, '-');
		if (len < sizeof(foreign_pattern) - 1 || c == NULL) {
			condlog(0, "%s: bad file name %s, fnmatch error?",
				__func__, fn);
			continue;
		}
		c++;
		condlog(4, "%s: found %s", __func__, fn);

		namesz = len - sizeof(foreign_pattern) + 3;
		fgn = malloc(sizeof(*fgn) + namesz);
		if (fgn == NULL)
			continue;
		memset(fgn, 0, sizeof(*fgn));
		strlcpy((char*)fgn + offsetof(struct foreign, name), c, namesz);

		if (enable_re != NULL) {
			int ret = regexec(enable_re, fgn->name, 0, NULL, 0);

			if (ret == REG_NOMATCH) {
				condlog(3, "%s: foreign library \"%s\" is not enabled",
					__func__, fgn->name);
				free(fgn);
				continue;
			} else if (ret != 0)
				/* assume it matches */
				condlog(2, "%s: error %d in regexec() for %s",
					__func__, ret, fgn->name);
		}

		snprintf(pathbuf, sizeof(pathbuf), "%s/%s", multipath_dir, fn);
		fgn->handle = dlopen(pathbuf, RTLD_NOW|RTLD_LOCAL);
		msg = dlerror();
		if (fgn->handle == NULL) {
			condlog(1, "%s: failed to dlopen %s: %s", __func__,
				pathbuf, msg);
			goto dl_err;
		}

		get_dlsym(fgn, init, dl_err);
		get_dlsym(fgn, cleanup, dl_err);
		get_dlsym(fgn, add, dl_err);
		get_dlsym(fgn, change, dl_err);
		get_dlsym(fgn, delete, dl_err);
		get_dlsym(fgn, delete_all, dl_err);
		get_dlsym(fgn, check, dl_err);
		get_dlsym(fgn, lock, dl_err);
		get_dlsym(fgn, unlock, dl_err);
		get_dlsym(fgn, get_multipaths, dl_err);
		get_dlsym(fgn, release_multipaths, dl_err);
		get_dlsym(fgn, get_paths, dl_err);
		get_dlsym(fgn, release_paths, dl_err);

		fgn->context = fgn->init(LIBMP_FOREIGN_API, fgn->name);
		if (fgn->context == NULL) {
			condlog(0, "%s: init() failed for %s", __func__, fn);
			goto dl_err;
		}

		if (vector_alloc_slot(foreigns) == NULL) {
			goto dl_err;
		}

		vector_set_slot(foreigns, fgn);
		condlog(3, "foreign library \"%s\" loaded successfully",
			fgn->name);

		continue;

	dl_err:
		free_foreign(fgn);
	}
	pthread_cleanup_pop(1); /* free_scandir_result */
	pthread_cleanup_pop(1); /* free_pre */
	return 0;
}

int init_foreign(const char *multipath_dir, const char *enable)
{
	int ret;

	wrlock_foreigns();

	if (foreigns != NULL) {
		unlock_foreigns(NULL);
		condlog(0, "%s: already initialized", __func__);
		return -EEXIST;
	}

	pthread_cleanup_push(unlock_foreigns, NULL);
	ret = _init_foreign(multipath_dir, enable);
	pthread_cleanup_pop(1);

	return ret;
}

int add_foreign(struct udev_device *udev)
{
	struct foreign *fgn;
	dev_t dt;
	int j;
	int r = FOREIGN_IGNORED;

	if (udev == NULL) {
		condlog(1, "%s called with NULL udev", __func__);
		return FOREIGN_ERR;
	}

	rdlock_foreigns();
	if (foreigns == NULL) {
		unlock_foreigns(NULL);
		return FOREIGN_ERR;
	}
	pthread_cleanup_push(unlock_foreigns, NULL);

	dt = udev_device_get_devnum(udev);
	vector_foreach_slot(foreigns, fgn, j) {
		r = fgn->add(fgn->context, udev);

		if (r == FOREIGN_CLAIMED) {
			condlog(3, "%s: foreign \"%s\" claims device %d:%d",
				__func__, fgn->name, major(dt), minor(dt));
			break;
		} else if (r == FOREIGN_OK) {
			condlog(4, "%s: foreign \"%s\" owns device %d:%d",
				__func__, fgn->name, major(dt), minor(dt));
			break;
		} else if (r != FOREIGN_IGNORED) {
			condlog(1, "%s: unexpected return value %d from \"%s\"",
				__func__, r, fgn->name);
		}
	}

	pthread_cleanup_pop(1);
	return r;
}

int change_foreign(struct udev_device *udev)
{
	struct foreign *fgn;
	int j;
	dev_t dt;
	int r = FOREIGN_IGNORED;

	if (udev == NULL) {
		condlog(1, "%s called with NULL udev", __func__);
		return FOREIGN_ERR;
	}

	rdlock_foreigns();
	if (foreigns == NULL) {
		unlock_foreigns(NULL);
		return FOREIGN_ERR;
	}
	pthread_cleanup_push(unlock_foreigns, NULL);

	dt = udev_device_get_devnum(udev);
	vector_foreach_slot(foreigns, fgn, j) {
		r = fgn->change(fgn->context, udev);

		if (r == FOREIGN_OK) {
			condlog(4, "%s: foreign \"%s\" completed %d:%d",
				__func__, fgn->name, major(dt), minor(dt));
			break;
		} else if (r != FOREIGN_IGNORED) {
			condlog(1, "%s: unexpected return value %d from \"%s\"",
				__func__, r, fgn->name);
		}
	}

	pthread_cleanup_pop(1);
	return r;
}

int delete_foreign(struct udev_device *udev)
{
	struct foreign *fgn;
	int j;
	dev_t dt;
	int r = FOREIGN_IGNORED;

	if (udev == NULL) {
		condlog(1, "%s called with NULL udev", __func__);
		return FOREIGN_ERR;
	}

	rdlock_foreigns();
	if (foreigns == NULL) {
		unlock_foreigns(NULL);
		return FOREIGN_ERR;
	}
	pthread_cleanup_push(unlock_foreigns, NULL);

	dt = udev_device_get_devnum(udev);
	vector_foreach_slot(foreigns, fgn, j) {
		r = fgn->delete(fgn->context, udev);

		if (r == FOREIGN_OK) {
			condlog(3, "%s: foreign \"%s\" deleted device %d:%d",
				__func__, fgn->name, major(dt), minor(dt));
			break;
		} else if (r != FOREIGN_IGNORED) {
			condlog(1, "%s: unexpected return value %d from \"%s\"",
				__func__, r, fgn->name);
		}
	}

	pthread_cleanup_pop(1);
	return r;
}

int delete_all_foreign(void)
{
	struct foreign *fgn;
	int j;

	rdlock_foreigns();
	if (foreigns == NULL) {
		unlock_foreigns(NULL);
		return FOREIGN_ERR;
	}
	pthread_cleanup_push(unlock_foreigns, NULL);

	vector_foreach_slot(foreigns, fgn, j) {
		int r;

		r = fgn->delete_all(fgn->context);
		if (r != FOREIGN_IGNORED && r != FOREIGN_OK) {
			condlog(1, "%s: unexpected return value %d from \"%s\"",
				__func__, r, fgn->name);
		}
	}

	pthread_cleanup_pop(1);
	return FOREIGN_OK;
}

void check_foreign(void)
{
	struct foreign *fgn;
	int j;

	rdlock_foreigns();
	if (foreigns == NULL) {
		unlock_foreigns(NULL);
		return;
	}
	pthread_cleanup_push(unlock_foreigns, NULL);

	vector_foreach_slot(foreigns, fgn, j) {
		fgn->check(fgn->context);
	}

	pthread_cleanup_pop(1);
}

/* Call this after get_path_layout */
void foreign_path_layout(void)
{
	struct foreign *fgn;
	int i;

	rdlock_foreigns();
	if (foreigns == NULL) {
		unlock_foreigns(NULL);
		return;
	}
	pthread_cleanup_push(unlock_foreigns, NULL);

	vector_foreach_slot(foreigns, fgn, i) {
		const struct _vector *vec;

		fgn->lock(fgn->context);
		pthread_cleanup_push(fgn->unlock, fgn->context);

		vec = fgn->get_paths(fgn->context);
		if (vec != NULL) {
			_get_path_layout(vec, LAYOUT_RESET_NOT);
		}
		fgn->release_paths(fgn->context, vec);

		pthread_cleanup_pop(1);
	}

	pthread_cleanup_pop(1);
}

/* Call this after get_multipath_layout */
void foreign_multipath_layout(void)
{
	struct foreign *fgn;
	int i;

	rdlock_foreigns();
	if (foreigns == NULL) {
		unlock_foreigns(NULL);
		return;
	}
	pthread_cleanup_push(unlock_foreigns, NULL);

	vector_foreach_slot(foreigns, fgn, i) {
		const struct _vector *vec;

		fgn->lock(fgn->context);
		pthread_cleanup_push(fgn->unlock, fgn->context);

		vec = fgn->get_multipaths(fgn->context);
		if (vec != NULL) {
			_get_multipath_layout(vec, LAYOUT_RESET_NOT);
		}
		fgn->release_multipaths(fgn->context, vec);

		pthread_cleanup_pop(1);
	}

	pthread_cleanup_pop(1);
}

int snprint_foreign_topology(char *buf, int len, int verbosity)
{
	struct foreign *fgn;
	int i;
	char *c = buf;

	rdlock_foreigns();
	if (foreigns == NULL) {
		unlock_foreigns(NULL);
		return 0;
	}
	pthread_cleanup_push(unlock_foreigns, NULL);

	vector_foreach_slot(foreigns, fgn, i) {
		const struct _vector *vec;
		const struct gen_multipath *gm;
		int j;

		fgn->lock(fgn->context);
		pthread_cleanup_push(fgn->unlock, fgn->context);

		vec = fgn->get_multipaths(fgn->context);
		if (vec != NULL) {
			vector_foreach_slot(vec, gm, j) {

				c += _snprint_multipath_topology(gm, c,
								 buf + len - c,
								 verbosity);
				if (c >= buf + len - 1)
					break;
			}
			if (c >= buf + len - 1)
				break;
		}
		fgn->release_multipaths(fgn->context, vec);
		pthread_cleanup_pop(1);
	}

	pthread_cleanup_pop(1);
	return c - buf;
}

void print_foreign_topology(int verbosity)
{
	int buflen = MAX_LINE_LEN * MAX_LINES;
	char *buf = NULL, *tmp = NULL;

	buf = malloc(buflen);
	buf[0] = '\0';
	while (buf != NULL) {
		char *c = buf;

		c += snprint_foreign_topology(buf, buflen,
						   verbosity);
		if (c < buf + buflen - 1)
			break;

		buflen *= 2;
		tmp = buf;
		buf = realloc(buf, buflen);
	}

	if (buf == NULL && tmp != NULL)
		buf = tmp;

	if (buf != NULL) {
		printf("%s", buf);
		free(buf);
	}
}

int snprint_foreign_paths(char *buf, int len, const char *style, int pretty)
{
	struct foreign *fgn;
	int i;
	char *c = buf;

	rdlock_foreigns();
	if (foreigns == NULL) {
		unlock_foreigns(NULL);
		return 0;
	}
	pthread_cleanup_push(unlock_foreigns, NULL);

	vector_foreach_slot(foreigns, fgn, i) {
		const struct _vector *vec;
		const struct gen_path *gp;
		int j;

		fgn->lock(fgn->context);
		pthread_cleanup_push(fgn->unlock, fgn->context);

		vec = fgn->get_paths(fgn->context);
		if (vec != NULL) {
			vector_foreach_slot(vec, gp, j) {
				c += _snprint_path(gp, c, buf + len - c,
						   style, pretty);
				if (c >= buf + len - 1)
					break;
			}
			if (c >= buf + len - 1)
				break;
		}
		fgn->release_paths(fgn->context, vec);
		pthread_cleanup_pop(1);
	}

	pthread_cleanup_pop(1);
	return c - buf;
}

int snprint_foreign_multipaths(char *buf, int len,
			       const char *style, int pretty)
{
	struct foreign *fgn;
	int i;
	char *c = buf;

	rdlock_foreigns();
	if (foreigns == NULL) {
		unlock_foreigns(NULL);
		return 0;
	}
	pthread_cleanup_push(unlock_foreigns, NULL);

	vector_foreach_slot(foreigns, fgn, i) {
		const struct _vector *vec;
		const struct gen_multipath *gm;
		int j;

		fgn->lock(fgn->context);
		pthread_cleanup_push(fgn->unlock, fgn->context);

		vec = fgn->get_multipaths(fgn->context);
		if (vec != NULL) {
			vector_foreach_slot(vec, gm, j) {
				c += _snprint_multipath(gm, c, buf + len - c,
							style, pretty);
				if (c >= buf + len - 1)
					break;
			}
			if (c >= buf + len - 1)
				break;
		}
		fgn->release_multipaths(fgn->context, vec);
		pthread_cleanup_pop(1);
	}

	pthread_cleanup_pop(1);
	return c - buf;
}
