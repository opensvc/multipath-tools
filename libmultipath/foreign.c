// SPDX-License-Identifier: GPL-2.0-or-later
/*
  Copyright (c) 2018 Martin Wilck, SUSE Linux GmbH
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
#include "structs.h"
#include "structs_vec.h"
#include "print.h"
#include "foreign.h"
#include "strbuf.h"

static vector foreigns;
static const char *const foreign_dir = MULTIPATH_DIR;

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

static void unlock_foreigns(__attribute__((unused)) void *unused)
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

void cleanup_foreign__(void)
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
	cleanup_foreign__();
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

static int _init_foreign(const char *enable)
{
	char pathbuf[PATH_MAX];
	struct dirent **di = NULL;
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
			goto out_free_pre;
		}
	}

	r = scandir(foreign_dir, &di, select_foreign_libs, alphasort);

	if (r == 0) {
		condlog(3, "%s: no foreign multipath libraries found",
			__func__);
		goto out_free_pre;
	} else if (r < 0) {
		r = -errno;
		condlog(1, "%s: error scanning foreign multipath libraries: %m",
			__func__);
		cleanup_foreign__();
		goto out_free_pre;
	}

	sr.di = di;
	sr.n = r;
	pthread_cleanup_push_cast(free_scandir_result, &sr);
	for (i = 0; i < r; i++) {
		const char *msg, *fn, *c;
		struct foreign *fgn;
		size_t len, namesz;

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

		snprintf(pathbuf, sizeof(pathbuf), "%s/%s", foreign_dir, fn);
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

		if (!vector_alloc_slot(foreigns)) {
			goto dl_err;
		}

		vector_set_slot(foreigns, fgn);
		condlog(3, "foreign library \"%s\" loaded successfully",
			fgn->name);

		continue;

	dl_err:
		free_foreign(fgn);
	}
	r = 0;
	pthread_cleanup_pop(1); /* free_scandir_result */
out_free_pre:
	pthread_cleanup_pop(1); /* free_pre */
	return r;
}

int init_foreign(const char *enable)
{
	int ret;

	wrlock_foreigns();

	if (foreigns != NULL) {
		unlock_foreigns(NULL);
		condlog(0, "%s: already initialized", __func__);
		return -EEXIST;
	}

	pthread_cleanup_push(unlock_foreigns, NULL);
	ret = _init_foreign(enable);
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
void foreign_path_layout(fieldwidth_t *width)
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
		const struct vector_s *vec;

		fgn->lock(fgn->context);
		pthread_cleanup_push(fgn->unlock, fgn->context);

		vec = fgn->get_paths(fgn->context);
		if (vec != NULL) {
			get_path_layout__(vec, LAYOUT_RESET_NOT, width);
		}
		fgn->release_paths(fgn->context, vec);

		pthread_cleanup_pop(1);
	}

	pthread_cleanup_pop(1);
}

/* Call this after get_multipath_layout */
void foreign_multipath_layout(fieldwidth_t *width)
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
		const struct vector_s *vec;

		fgn->lock(fgn->context);
		pthread_cleanup_push(fgn->unlock, fgn->context);

		vec = fgn->get_multipaths(fgn->context);
		if (vec != NULL) {
			get_multipath_layout__(vec, LAYOUT_RESET_NOT, width);
		}
		fgn->release_multipaths(fgn->context, vec);

		pthread_cleanup_pop(1);
	}

	pthread_cleanup_pop(1);
}

static int snprint_foreign_topology__(struct strbuf *buf, int verbosity, const fieldwidth_t *p_width)
{
	struct foreign *fgn;
	int i;
	size_t initial_len = get_strbuf_len(buf);

	vector_foreach_slot(foreigns, fgn, i) {
		const struct vector_s *vec;
		const struct gen_multipath *gm;
		int j;

		fgn->lock(fgn->context);
		pthread_cleanup_push(fgn->unlock, fgn->context);

		vec = fgn->get_multipaths(fgn->context);
		if (vec != NULL) {
			vector_foreach_slot(vec, gm, j) {
				if (snprint_multipath_topology__(gm, buf, verbosity, p_width) < 0)
					break;
			}
		}
		fgn->release_multipaths(fgn->context, vec);
		pthread_cleanup_pop(1);
	}

	return get_strbuf_len(buf) - initial_len;
}

int snprint_foreign_topology(struct strbuf *buf, int verbosity, const fieldwidth_t *p_width)
{
	int rc;

	rdlock_foreigns();
	if (foreigns == NULL) {
		unlock_foreigns(NULL);
		return 0;
	}
	pthread_cleanup_push(unlock_foreigns, NULL);
	rc = snprint_foreign_topology__(buf, verbosity, p_width);
	pthread_cleanup_pop(1);
	return rc;
}

void print_foreign_topology(int verbosity)
{
	STRBUF_ON_STACK(buf);
	struct foreign *fgn;
	int i;
	fieldwidth_t *p_width __attribute__((cleanup(cleanup_ucharp))) = NULL;

	if ((p_width = alloc_path_layout()) == NULL)
		return;
	rdlock_foreigns();
	if (foreigns == NULL) {
		unlock_foreigns(NULL);
		return;
	}
	pthread_cleanup_push(unlock_foreigns, NULL);
	vector_foreach_slot(foreigns, fgn, i) {
		const struct vector_s *vec;

		fgn->lock(fgn->context);
		pthread_cleanup_push(fgn->unlock, fgn->context);
		vec = fgn->get_paths(fgn->context);
		get_path_layout__(vec, LAYOUT_RESET_NOT, p_width);
		fgn->release_paths(fgn->context, vec);
		pthread_cleanup_pop(1);
	}
	snprint_foreign_topology__(&buf, verbosity, p_width);
	pthread_cleanup_pop(1);
	printf("%s", get_strbuf_str(&buf));
}

int snprint_foreign_paths(struct strbuf *buf, const char *style,
			  const fieldwidth_t *width)
{
	struct foreign *fgn;
	int i;
	size_t initial_len = get_strbuf_len(buf);

	rdlock_foreigns();
	if (foreigns == NULL) {
		unlock_foreigns(NULL);
		return 0;
	}
	pthread_cleanup_push(unlock_foreigns, NULL);

	vector_foreach_slot(foreigns, fgn, i) {
		const struct vector_s *vec;
		const struct gen_path *gp;
		int j, ret = 0;

		fgn->lock(fgn->context);
		pthread_cleanup_push(fgn->unlock, fgn->context);

		vec = fgn->get_paths(fgn->context);
		if (vec != NULL) {
			vector_foreach_slot(vec, gp, j) {
				ret = snprint_path__(gp, buf, style, width);
				if (ret < 0)
					break;
			}
		}
		fgn->release_paths(fgn->context, vec);
		pthread_cleanup_pop(1);
		if (ret < 0)
			break;
	}

	pthread_cleanup_pop(1);
	return get_strbuf_len(buf) - initial_len;
}

int snprint_foreign_multipaths(struct strbuf *buf, const char *style,
			       const fieldwidth_t *width)
{
	struct foreign *fgn;
	int i;
	size_t initial_len = get_strbuf_len(buf);

	rdlock_foreigns();
	if (foreigns == NULL) {
		unlock_foreigns(NULL);
		return 0;
	}
	pthread_cleanup_push(unlock_foreigns, NULL);

	vector_foreach_slot(foreigns, fgn, i) {
		const struct vector_s *vec;
		const struct gen_multipath *gm;
		int j, ret = 0;

		fgn->lock(fgn->context);
		pthread_cleanup_push(fgn->unlock, fgn->context);

		vec = fgn->get_multipaths(fgn->context);
		if (vec != NULL) {
			vector_foreach_slot(vec, gm, j) {
				ret = snprint_multipath__(gm, buf,
							 style, width);
				if (ret < 0)
					break;
			}
		}
		fgn->release_multipaths(fgn->context, vec);
		pthread_cleanup_pop(1);
		if (ret < 0)
			break;
	}

	pthread_cleanup_pop(1);
	return get_strbuf_len(buf) - initial_len;
}
