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
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
  USA.
*/

#include <sys/sysmacros.h>
#include <libudev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <libudev.h>
#include <pthread.h>
#include "vector.h"
#include "generic.h"
#include "foreign.h"
#include "debug.h"

const char *THIS;

struct nvme_map {
	struct gen_multipath gen;
	struct udev_device *udev;
	struct udev_device *subsys;
	dev_t devt;
};

#define NAME_LEN 64 /* buffer length temp model name */
#define const_gen_mp_to_nvme(g) ((const struct nvme_map*)(g))
#define gen_mp_to_nvme(g) ((struct nvme_map*)(g))
#define nvme_mp_to_gen(n) &((n)->gen)

static void cleanup_nvme_map(struct nvme_map *map)
{
	if (map->udev)
		udev_device_unref(map->udev);
	if (map->subsys)
		udev_device_unref(map->subsys);
	free(map);
}

static const struct _vector*
nvme_mp_get_pgs(const struct gen_multipath *gmp) {
	return NULL;
}

static void
nvme_mp_rel_pgs(const struct gen_multipath *gmp, const struct _vector *v)
{
}

static void rstrip(char *str)
{
	int n;

	for (n = strlen(str) - 1; n >= 0 && str[n] == ' '; n--);
	str[n+1] = '\0';
}

static int snprint_nvme_map(const struct gen_multipath *gmp,
			    char *buff, int len, char wildcard)
{
	const struct nvme_map *nvm = const_gen_mp_to_nvme(gmp);
	static const char nvme_vendor[] = "NVMe";
	char fld[NAME_LEN];
	const char *val;

	switch (wildcard) {
	case 'd':
		return snprintf(buff, len, "%s",
				udev_device_get_sysname(nvm->udev));
	case 'n':
		return snprintf(buff, len, "%s:NQN:%s",
				udev_device_get_sysname(nvm->subsys),
				udev_device_get_sysattr_value(nvm->subsys,
							      "subsysnqn"));
	case 'w':
		return snprintf(buff, len, "%s",
				udev_device_get_sysattr_value(nvm->udev,
							      "wwid"));
	case 'S':
		return snprintf(buff, len, "%s",
				udev_device_get_sysattr_value(nvm->udev,
							      "size"));
	case 'v':
		return snprintf(buff, len, "%s", nvme_vendor);
	case 's':
	case 'p':
		snprintf(fld, sizeof(fld), "%s",
			 udev_device_get_sysattr_value(nvm->subsys,
						      "model"));
		rstrip(fld);
		if (wildcard == 'p')
			return snprintf(buff, len, "%s", fld);
		return snprintf(buff, len, "%s,%s,%s", nvme_vendor, fld,
				udev_device_get_sysattr_value(nvm->subsys,
							      "firmware_rev"));
	case 'e':
		return snprintf(buff, len, "%s",
				udev_device_get_sysattr_value(nvm->subsys,
							      "firmware_rev"));
	case 'r':
		val = udev_device_get_sysattr_value(nvm->udev, "ro");
		if (val[0] == 1)
			return snprintf(buff, len, "%s", "ro");
		else
			return snprintf(buff, len, "%s", "rw");
	case 'G':
		return snprintf(buff, len, "%s", THIS);
	default:
		return snprintf(buff, len, "N/A");
		break;
	}
	return 0;
}

static const struct _vector*
nvme_pg_get_paths(const struct gen_pathgroup *gpg) {
	return NULL;
}

static void
nvme_pg_rel_paths(const struct gen_pathgroup *gpg, const struct _vector *v)
{
}

static int snprint_nvme_pg(const struct gen_pathgroup *gmp,
			   char *buff, int len, char wildcard)
{
	return 0;
}

static int snprint_nvme_path(const struct gen_path *gmp,
			     char *buff, int len, char wildcard)
{
	switch (wildcard) {
	case 'R':
		return snprintf(buff, len, "[foreign: %s]", THIS);
	default:
		break;
	}
	return 0;
}

static const struct gen_multipath_ops nvme_map_ops = {
	.get_pathgroups = nvme_mp_get_pgs,
	.rel_pathgroups = nvme_mp_rel_pgs,
	.style = generic_style,
	.snprint = snprint_nvme_map,
};

static const struct gen_pathgroup_ops nvme_pg_ops __attribute__((unused)) = {
	.get_paths = nvme_pg_get_paths,
	.rel_paths = nvme_pg_rel_paths,
	.snprint = snprint_nvme_pg,
};

static const struct gen_path_ops nvme_path_ops __attribute__((unused)) = {
	.snprint = snprint_nvme_path,
};

struct context {
	pthread_mutex_t mutex;
	vector mpvec;
};

void lock(struct context *ctx)
{
	pthread_mutex_lock(&ctx->mutex);
}

void unlock(void *arg)
{
	struct context *ctx = arg;

	pthread_mutex_unlock(&ctx->mutex);
}

static int _delete_all(struct context *ctx)
{
	struct nvme_map *nm;
	int n = VECTOR_SIZE(ctx->mpvec), i;

	if (n == 0)
		return FOREIGN_IGNORED;

	vector_foreach_slot_backwards(ctx->mpvec, nm, i) {
		vector_del_slot(ctx->mpvec, i);
		cleanup_nvme_map(nm);
	}
	return FOREIGN_OK;
}

int delete_all(struct context *ctx)
{
	int rc;

	condlog(5, "%s called for \"%s\"", __func__, THIS);

	lock(ctx);
	pthread_cleanup_push(unlock, ctx);
	rc = _delete_all(ctx);
	pthread_cleanup_pop(1);

	return rc;
}

void cleanup(struct context *ctx)
{
	(void)delete_all(ctx);

	lock(ctx);
	/*
	 * Locking is not strictly necessary here, locking in foreign.c
	 * makes sure that no other code is called with this ctx any more.
	 * But this should make static checkers feel better.
	 */
	pthread_cleanup_push(unlock, ctx);
	if (ctx->udev)
		udev_unref(ctx->udev);
	if (ctx->mpvec)
		vector_free(ctx->mpvec);
	ctx->mpvec = NULL;
	ctx->udev = NULL;
	pthread_cleanup_pop(1);
	pthread_mutex_destroy(&ctx->mutex);

	free(ctx);
}

struct context *init(unsigned int api, const char *name)
{
	struct context *ctx;

	if (api > LIBMP_FOREIGN_API) {
		condlog(0, "%s: api version mismatch: %08x > %08x\n",
			__func__, api, LIBMP_FOREIGN_API);
		return NULL;
	}

	if ((ctx = calloc(1, sizeof(*ctx)))== NULL)
		return NULL;

	pthread_mutex_init(&ctx->mutex, NULL);

	ctx->mpvec = vector_alloc();
	if (ctx->mpvec == NULL)
		goto err;

	THIS = name;
	return ctx;
err:
	cleanup(ctx);
	return NULL;
}

static struct nvme_map *_find_nvme_map_by_devt(const struct context *ctx,
					      dev_t devt)
{
	struct nvme_map *nm;
	int i;

	if (ctx->mpvec == NULL)
		return NULL;

	vector_foreach_slot(ctx->mpvec, nm, i) {
		if (nm->devt == devt)
			return nm;
	}

	return NULL;
}

static int _add_map(struct context *ctx, struct udev_device *ud,
		    struct udev_device *subsys)
{
	dev_t devt = udev_device_get_devnum(ud);
	struct nvme_map *map;

	if (_find_nvme_map_by_devt(ctx, devt) != NULL)
		return FOREIGN_OK;

	map = calloc(1, sizeof(*map));
	if (map == NULL)
		return FOREIGN_ERR;

	map->devt = devt;
	map->udev = udev_device_ref(ud);
	/*
	 * subsys is implicitly referenced by map->udev,
	 * no need to take a reference here.
	 */
	map->subsys = subsys;
	map->gen.ops = &nvme_map_ops;

	if (vector_alloc_slot(ctx->mpvec) == NULL) {
		cleanup_nvme_map(map);
		return FOREIGN_ERR;
	}

	vector_set_slot(ctx->mpvec, map);

	return FOREIGN_CLAIMED;
}

int add(struct context *ctx, struct udev_device *ud)
{
	struct udev_device *subsys;
	int rc;

	condlog(5, "%s called for \"%s\"", __func__, THIS);

	if (ud == NULL)
		return FOREIGN_ERR;
	if (strcmp("disk", udev_device_get_devtype(ud)))
		return FOREIGN_IGNORED;

	subsys = udev_device_get_parent_with_subsystem_devtype(ud,
							       "nvme-subsystem",
							       NULL);
	if (subsys == NULL)
		return FOREIGN_IGNORED;

	lock(ctx);
	pthread_cleanup_push(unlock, ctx);
	rc = _add_map(ctx, ud, subsys);
	pthread_cleanup_pop(1);

	if (rc == FOREIGN_CLAIMED)
		condlog(3, "%s: %s: added map %s", __func__, THIS,
			udev_device_get_sysname(ud));
	else if (rc != FOREIGN_OK)
		condlog(1, "%s: %s: retcode %d adding %s",
			__func__, THIS, rc, udev_device_get_sysname(ud));

	return rc;
}

int change(struct context *ctx, struct udev_device *ud)
{
	condlog(5, "%s called for \"%s\"", __func__, THIS);
	return FOREIGN_IGNORED;
}

static int _delete_map(struct context *ctx, struct udev_device *ud)
{
	int k;
	struct nvme_map *map;
	dev_t devt = udev_device_get_devnum(ud);

	map = _find_nvme_map_by_devt(ctx, devt);
	if (map ==NULL)
		return FOREIGN_IGNORED;

	k = find_slot(ctx->mpvec, map);
	if (k == -1)
		return FOREIGN_ERR;
	else
		vector_del_slot(ctx->mpvec, k);

	cleanup_nvme_map(map);

	return FOREIGN_OK;
}

int delete(struct context *ctx, struct udev_device *ud)
{
	int rc;

	condlog(5, "%s called for \"%s\"", __func__, THIS);

	if (ud == NULL)
		return FOREIGN_ERR;

	lock(ctx);
	pthread_cleanup_push(unlock, ctx);
	rc = _delete_map(ctx, ud);
	pthread_cleanup_pop(1);

	if (rc == FOREIGN_OK)
		condlog(3, "%s: %s: map %s deleted", __func__, THIS,
			udev_device_get_sysname(ud));
	else if (rc != FOREIGN_IGNORED)
		condlog(1, "%s: %s: retcode %d deleting map %s", __func__,
			THIS, rc, udev_device_get_sysname(ud));

	return rc;
}

void check(struct context *ctx)
{
	condlog(5, "%s called for \"%s\"", __func__, THIS);
	return;
}

/*
 * It's safe to pass our internal pointer, this is only used under the lock.
 */
const struct _vector *get_multipaths(const struct context *ctx)
{
	condlog(5, "%s called for \"%s\"", __func__, THIS);
	return ctx->mpvec;
}

void release_multipaths(const struct context *ctx, const struct _vector *mpvec)
{
	condlog(5, "%s called for \"%s\"", __func__, THIS);
	/* NOP */
}

/*
 * It's safe to pass our internal pointer, this is only used under the lock.
 */
const struct _vector * get_paths(const struct context *ctx)
{
	condlog(5, "%s called for \"%s\"", __func__, THIS);
	return NULL;
}

void release_paths(const struct context *ctx, const struct _vector *mpvec)
{
	condlog(5, "%s called for \"%s\"", __func__, THIS);
	/* NOP */
}

/* compile-time check whether all methods are present and correctly typed */
#define _METHOD_INIT(x) .x = x
static struct foreign __methods __attribute__((unused)) = {
	_METHOD_INIT(init),
	_METHOD_INIT(cleanup),
	_METHOD_INIT(change),
	_METHOD_INIT(delete),
	_METHOD_INIT(delete_all),
	_METHOD_INIT(check),
	_METHOD_INIT(lock),
	_METHOD_INIT(unlock),
	_METHOD_INIT(get_multipaths),
	_METHOD_INIT(release_multipaths),
	_METHOD_INIT(get_paths),
	_METHOD_INIT(release_paths),
};
