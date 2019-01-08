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

#include "nvme-lib.h"
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <libudev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <libudev.h>
#include <pthread.h>
#include <limits.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include "util.h"
#include "vector.h"
#include "generic.h"
#include "foreign.h"
#include "debug.h"
#include "structs.h"
#include "sysfs.h"

static const char nvme_vendor[] = "NVMe";
static const char N_A[] = "n/a";
const char *THIS;

struct nvme_map;
struct nvme_pathgroup {
	struct gen_pathgroup gen;
	struct _vector pathvec;
};

struct nvme_path {
	struct gen_path gen;
	struct udev_device *udev;
	struct udev_device *ctl;
	struct nvme_map *map;
	bool seen;
	/*
	 * The kernel works in failover mode.
	 * Each path has a separate path group.
	 */
	struct nvme_pathgroup pg;
};

struct nvme_map {
	struct gen_multipath gen;
	struct udev_device *udev;
	struct udev_device *subsys;
	dev_t devt;
	struct _vector pgvec;
	int nr_live;
	int ana_supported;
};

#define NAME_LEN 64 /* buffer length for temp attributes */
#define const_gen_mp_to_nvme(g) ((const struct nvme_map*)(g))
#define gen_mp_to_nvme(g) ((struct nvme_map*)(g))
#define nvme_mp_to_gen(n) &((n)->gen)
#define const_gen_pg_to_nvme(g) ((const struct nvme_pathgroup*)(g))
#define gen_pg_to_nvme(g) ((struct nvme_pathgroup*)(g))
#define nvme_pg_to_gen(n) &((n)->gen)
#define const_gen_path_to_nvme(g) ((const struct nvme_path*)(g))
#define gen_path_to_nvme(g) ((struct nvme_path*)(g))
#define nvme_path_to_gen(n) &((n)->gen)
#define nvme_pg_to_path(x) (VECTOR_SLOT(&((x)->pathvec), 0))
#define nvme_path_to_pg(x) &((x)->pg)

static void cleanup_nvme_path(struct nvme_path *path)
{
	condlog(5, "%s: %p %p", __func__, path, path->udev);
	if (path->udev)
		udev_device_unref(path->udev);
	vector_reset(&path->pg.pathvec);

	/* ctl is implicitly referenced by udev, no need to unref */
	free(path);
}

static void cleanup_nvme_map(struct nvme_map *map)
{
	struct nvme_pathgroup *pg;
	struct nvme_path *path;
	int i;

	vector_foreach_slot_backwards(&map->pgvec, pg, i) {
		path = nvme_pg_to_path(pg);
		condlog(5, "%s: %d %p", __func__, i, path);
		cleanup_nvme_path(path);
		vector_del_slot(&map->pgvec, i);
	}
	vector_reset(&map->pgvec);
	if (map->udev)
		udev_device_unref(map->udev);
	/* subsys is implicitly referenced by udev, no need to unref */
	free(map);
}

static const struct _vector*
nvme_mp_get_pgs(const struct gen_multipath *gmp) {
	const struct nvme_map *nvme = const_gen_mp_to_nvme(gmp);

	/* This is all used under the lock, no need to copy */
	return &nvme->pgvec;
}

static void
nvme_mp_rel_pgs(const struct gen_multipath *gmp, const struct _vector *v)
{
	/* empty */
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
	char fld[NAME_LEN];
	const char *val;

	switch (wildcard) {
	case 'd':
		return snprintf(buff, len, "%s",
				udev_device_get_sysname(nvm->udev));
	case 'n':
		return snprintf(buff, len, "%s:nsid.%s",
				udev_device_get_sysattr_value(nvm->subsys,
							      "subsysnqn"),
				udev_device_get_sysattr_value(nvm->udev,
							      "nsid"));
	case 'w':
		return snprintf(buff, len, "%s",
				udev_device_get_sysattr_value(nvm->udev,
							      "wwid"));
	case 'N':
		return snprintf(buff, len, "%u", nvm->nr_live);
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
	case 'h':
		if (nvm->ana_supported == YNU_YES)
			return snprintf(buff, len, "ANA");
	default:
		break;
	}

	return snprintf(buff, len, N_A);
}

static const struct _vector*
nvme_pg_get_paths(const struct gen_pathgroup *gpg) {
	const struct nvme_pathgroup *gp = const_gen_pg_to_nvme(gpg);

	/* This is all used under the lock, no need to copy */
	return &gp->pathvec;
}

static void
nvme_pg_rel_paths(const struct gen_pathgroup *gpg, const struct _vector *v)
{
	/* empty */
}

static int snprint_hcil(const struct nvme_path *np, char *buf, int len)
{
	unsigned int nvmeid, ctlid, nsid;
	int rc;
	const char *sysname = udev_device_get_sysname(np->udev);

	rc = sscanf(sysname, "nvme%uc%un%u", &nvmeid, &ctlid, &nsid);
	if (rc != 3) {
		condlog(1, "%s: failed to scan %s", __func__, sysname);
		rc = snprintf(buf, len, "(ERR:%s)", sysname);
	} else
		rc = snprintf(buf, len, "%u:%u:%u", nvmeid, ctlid, nsid);
	return (rc < len ? rc : len);
}

static int snprint_nvme_path(const struct gen_path *gp,
			     char *buff, int len, char wildcard)
{
	const struct nvme_path *np = const_gen_path_to_nvme(gp);
	dev_t devt;
	char fld[NAME_LEN];
	struct udev_device *pci;

	switch (wildcard) {
	case 'w':
		return snprintf(buff, len, "%s",
				udev_device_get_sysattr_value(np->udev,
							      "wwid"));
	case 'd':
		return snprintf(buff, len, "%s",
				udev_device_get_sysname(np->udev));
	case 'i':
		return snprint_hcil(np, buff, len);
	case 'D':
		devt = udev_device_get_devnum(np->udev);
		return snprintf(buff, len, "%u:%u", major(devt), minor(devt));
	case 'o':
		if (sysfs_attr_get_value(np->ctl, "state",
					 fld, sizeof(fld)) > 0)
			return snprintf(buff, len, "%s", fld);
		break;
	case 'T':
		if (sysfs_attr_get_value(np->udev, "ana_state", fld,
					 sizeof(fld)) > 0)
			return snprintf(buff, len, "%s", fld);
		break;
	case 'p':
		if (sysfs_attr_get_value(np->udev, "ana_state", fld,
					 sizeof(fld)) > 0) {
			rstrip(fld);
			if (!strcmp(fld, "optimized"))
				return snprintf(buff, len, "%d", 50);
			else if (!strcmp(fld, "non-optimized"))
				return snprintf(buff, len, "%d", 10);
			else
				return snprintf(buff, len, "%d", 0);
		}
		break;
	case 's':
		snprintf(fld, sizeof(fld), "%s",
			 udev_device_get_sysattr_value(np->ctl,
						      "model"));
		rstrip(fld);
		return snprintf(buff, len, "%s,%s,%s", nvme_vendor, fld,
				udev_device_get_sysattr_value(np->ctl,
							      "firmware_rev"));
	case 'S':
		return snprintf(buff, len, "%s",
			udev_device_get_sysattr_value(np->udev,
						      "size"));
	case 'z':
		return snprintf(buff, len, "%s",
				udev_device_get_sysattr_value(np->ctl,
							      "serial"));
	case 'm':
		return snprintf(buff, len, "%s",
				udev_device_get_sysname(np->map->udev));
	case 'N':
	case 'R':
		return snprintf(buff, len, "%s:%s",
			udev_device_get_sysattr_value(np->ctl,
						      "transport"),
			udev_device_get_sysattr_value(np->ctl,
						      "address"));
	case 'G':
		return snprintf(buff, len, "[%s]", THIS);
	case 'a':
		pci = udev_device_get_parent_with_subsystem_devtype(np->ctl,
								    "pci",
								    NULL);
		if (pci != NULL)
			return snprintf(buff, len, "PCI:%s",
					udev_device_get_sysname(pci));
		/* fall through */
	default:
		break;
	}
	return snprintf(buff, len, "%s", N_A);
	return 0;
}

static int snprint_nvme_pg(const struct gen_pathgroup *gmp,
			   char *buff, int len, char wildcard)
{
	const struct nvme_pathgroup *pg = const_gen_pg_to_nvme(gmp);
	const struct nvme_path *path = nvme_pg_to_path(pg);

	switch (wildcard) {
	case 't':
		return snprint_nvme_path(nvme_path_to_gen(path),
					 buff, len, 'T');
	case 'p':
		return snprint_nvme_path(nvme_path_to_gen(path),
					 buff, len, 'p');
	default:
		return snprintf(buff, len, N_A);
	}
}

static int nvme_style(const struct gen_multipath* gm,
		      char *buf, int len, int verbosity)
{
	int n = snprintf(buf, len, "%%w [%%G]:%%d %%s");

	return (n < len ? n : len - 1);
}

static const struct gen_multipath_ops nvme_map_ops = {
	.get_pathgroups = nvme_mp_get_pgs,
	.rel_pathgroups = nvme_mp_rel_pgs,
	.style = nvme_style,
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
	struct udev *udev;
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

	ctx->udev = udev_new();
	if (ctx->udev == NULL)
		goto err;

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

static struct nvme_path *
_find_path_by_syspath(struct nvme_map *map, const char *syspath)
{
	struct nvme_pathgroup *pg;
	char real[PATH_MAX];
	const char *ppath;
	int i;

	ppath = realpath(syspath, real);
	if (ppath == NULL) {
		condlog(1, "%s: %s: error in realpath", __func__, THIS);
		ppath = syspath;
	}

	vector_foreach_slot(&map->pgvec, pg, i) {
		struct nvme_path *path = nvme_pg_to_path(pg);

		if (!strcmp(ppath,
			    udev_device_get_syspath(path->udev)))
			return path;
	}
	condlog(4, "%s: %s: %s not found", __func__, THIS, ppath);
	return NULL;
}

static void _udev_device_unref(void *p)
{
	udev_device_unref(p);
}

static void _udev_enumerate_unref(void *p)
{
	udev_enumerate_unref(p);
}

static int _dirent_controller(const struct dirent *di)
{
	static const char nvme_prefix[] = "nvme";
	const char *p;

#ifdef _DIRENT_HAVE_D_TYPE
	if (di->d_type != DT_LNK)
		return 0;
#endif
	if (strncmp(di->d_name, nvme_prefix, sizeof(nvme_prefix) - 1))
		return 0;
	p = di->d_name + sizeof(nvme_prefix) - 1;
	if (*p == '\0' || !isdigit(*p))
		return 0;
	for (++p; *p != '\0'; ++p)
		if (!isdigit(*p))
			return 0;
	return 1;
}

/* Find the block device for a given nvme controller */
struct udev_device *get_ctrl_blkdev(const struct context *ctx,
				    struct udev_device *ctrl)
{
	struct udev_list_entry *item;
	struct udev_device *blkdev = NULL;
	struct udev_enumerate *enm = udev_enumerate_new(ctx->udev);

	if (enm == NULL)
		return NULL;

	pthread_cleanup_push(_udev_enumerate_unref, enm);
	if (udev_enumerate_add_match_parent(enm, ctrl) < 0)
		goto out;
	if (udev_enumerate_add_match_subsystem(enm, "block"))
		goto out;

	if (udev_enumerate_scan_devices(enm) < 0) {
		condlog(1, "%s: %s: error enumerating devices", __func__, THIS);
		goto out;
	}

	for (item = udev_enumerate_get_list_entry(enm);
	     item != NULL;
	     item = udev_list_entry_get_next(item)) {
		struct udev_device *tmp;

		tmp = udev_device_new_from_syspath(ctx->udev,
					   udev_list_entry_get_name(item));
		if (tmp == NULL)
			continue;
		if (!strcmp(udev_device_get_devtype(tmp), "disk")) {
			blkdev = tmp;
			break;
		} else
			udev_device_unref(tmp);
	}

	if (blkdev == NULL)
		condlog(1, "%s: %s: failed to get blockdev for %s",
			__func__, THIS, udev_device_get_sysname(ctrl));
	else
		condlog(5, "%s: %s: got %s", __func__, THIS,
			udev_device_get_sysname(blkdev));
out:
	pthread_cleanup_pop(1);
	return blkdev;
}

static void test_ana_support(struct nvme_map *map, struct udev_device *ctl)
{
	const char *dev_t;
	char sys_path[64];
	long fd;
	int rc;

	if (map->ana_supported != YNU_UNDEF)
		return;

	dev_t = udev_device_get_sysattr_value(ctl, "dev");
	if (snprintf(sys_path, sizeof(sys_path), "/dev/char/%s", dev_t)
	    >= sizeof(sys_path))
		return;

	fd = open(sys_path, O_RDONLY);
	if (fd == -1) {
		condlog(2, "%s: error opening %s", __func__, sys_path);
		return;
	}

	pthread_cleanup_push(close_fd, (void *)fd);
	rc = nvme_id_ctrl_ana(fd, NULL);
	if (rc < 0)
		condlog(2, "%s: error in nvme_id_ctrl: %s", __func__,
			strerror(errno));
	else {
		map->ana_supported = (rc == 1 ? YNU_YES : YNU_NO);
		condlog(3, "%s: NVMe ctrl %s: ANA %s supported", __func__, dev_t,
			rc == 1 ? "is" : "is not");
	}
	pthread_cleanup_pop(1);
}

static void _find_controllers(struct context *ctx, struct nvme_map *map)
{
	char pathbuf[PATH_MAX], realbuf[PATH_MAX];
	struct dirent **di = NULL;
	struct scandir_result sr;
	struct udev_device *subsys;
	struct nvme_pathgroup *pg;
	struct nvme_path *path;
	int r, i, n;

	if (map == NULL || map->udev == NULL)
		return;

	vector_foreach_slot(&map->pgvec, pg, i) {
		path = nvme_pg_to_path(pg);
		path->seen = false;
	}

	subsys = udev_device_get_parent_with_subsystem_devtype(map->udev,
							       "nvme-subsystem",
							       NULL);
	if (subsys == NULL) {
		condlog(1, "%s: %s: BUG: no NVME subsys for %s", __func__, THIS,
			udev_device_get_sysname(map->udev));
		return;
	}

	n = snprintf(pathbuf, sizeof(pathbuf), "%s",
		     udev_device_get_syspath(subsys));
	r = scandir(pathbuf, &di, _dirent_controller, alphasort);

	if (r == 0) {
		condlog(3, "%s: %s: no controllers for %s", __func__, THIS,
			udev_device_get_sysname(map->udev));
		return;
	} else if (r < 0) {
		condlog(1, "%s: %s: error %d scanning controllers of %s",
			__func__, THIS, errno,
			udev_device_get_sysname(map->udev));
		return;
	}

	sr.di = di;
	sr.n = r;
	pthread_cleanup_push_cast(free_scandir_result, &sr);
	for (i = 0; i < r; i++) {
		char *fn = di[i]->d_name;
		struct udev_device *ctrl, *udev;

		if (snprintf(pathbuf + n, sizeof(pathbuf) - n, "/%s", fn)
		    >= sizeof(pathbuf) - n)
			continue;
		if (realpath(pathbuf, realbuf) == NULL) {
			condlog(3, "%s: %s: realpath: %s", __func__, THIS,
				strerror(errno));
			continue;
		}
		condlog(4, "%s: %s: found %s", __func__, THIS, realbuf);

		ctrl = udev_device_new_from_syspath(ctx->udev, realbuf);
		if (ctrl == NULL) {
			condlog(1, "%s: %s: failed to get udev device for %s",
				__func__, THIS, realbuf);
			continue;
		}

		pthread_cleanup_push(_udev_device_unref, ctrl);
		udev = get_ctrl_blkdev(ctx, ctrl);
		/*
		 * We give up the reference to the nvme device here and get
		 * it back from the child below.
		 * This way we don't need to worry about unreffing it.
		 */
		pthread_cleanup_pop(1);

		if (udev == NULL)
			continue;

		path = _find_path_by_syspath(map,
					     udev_device_get_syspath(udev));
		if (path != NULL) {
			path->seen = true;
			condlog(4, "%s: %s already known",
				__func__, fn);
			continue;
		}

		path = calloc(1, sizeof(*path));
		if (path == NULL)
			continue;

		path->gen.ops = &nvme_path_ops;
		path->udev = udev;
		path->seen = true;
		path->map = map;
		path->ctl = udev_device_get_parent_with_subsystem_devtype
			(udev, "nvme", NULL);
		if (path->ctl == NULL) {
			condlog(1, "%s: %s: failed to get controller for %s",
				__func__, THIS, fn);
			cleanup_nvme_path(path);
			continue;
		}
		test_ana_support(map, path->ctl);

		path->pg.gen.ops = &nvme_pg_ops;
		if (vector_alloc_slot(&path->pg.pathvec) == NULL) {
			cleanup_nvme_path(path);
			continue;
		}
		vector_set_slot(&path->pg.pathvec, path);
		if (vector_alloc_slot(&map->pgvec) == NULL) {
			cleanup_nvme_path(path);
			continue;
		}
		vector_set_slot(&map->pgvec, &path->pg);
		condlog(3, "%s: %s: new path %s added to %s",
			__func__, THIS, udev_device_get_sysname(udev),
			udev_device_get_sysname(map->udev));
	}
	pthread_cleanup_pop(1);

	map->nr_live = 0;
	vector_foreach_slot_backwards(&map->pgvec, pg, i) {
		path = nvme_pg_to_path(pg);
		if (!path->seen) {
			condlog(1, "path %d not found in %s any more",
				i, udev_device_get_sysname(map->udev));
			vector_del_slot(&map->pgvec, i);
			cleanup_nvme_path(path);
		} else {
			static const char live_state[] = "live";
			char state[16];

			if ((sysfs_attr_get_value(path->ctl, "state", state,
						  sizeof(state)) > 0) &&
			    !strncmp(state, live_state, sizeof(live_state) - 1))
				map->nr_live++;
		}
	}
	condlog(3, "%s: %s: map %s has %d/%d live paths", __func__, THIS,
		udev_device_get_sysname(map->udev), map->nr_live,
		VECTOR_SIZE(&map->pgvec));
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
	_find_controllers(ctx, map);

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

void _check(struct context *ctx)
{
	struct gen_multipath *gm;
	int i;

	vector_foreach_slot(ctx->mpvec, gm, i) {
		struct nvme_map *map = gen_mp_to_nvme(gm);

		_find_controllers(ctx, map);
	}
}

void check(struct context *ctx)
{
	condlog(4, "%s called for \"%s\"", __func__, THIS);
	lock(ctx);
	pthread_cleanup_push(unlock, ctx);
	_check(ctx);
	pthread_cleanup_pop(1);
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
	vector paths = NULL;
	const struct gen_multipath *gm;
	int i;

	condlog(5, "%s called for \"%s\"", __func__, THIS);
	vector_foreach_slot(ctx->mpvec, gm, i) {
		const struct nvme_map *nm = const_gen_mp_to_nvme(gm);
		paths = vector_convert(paths, &nm->pgvec,
				       struct nvme_pathgroup, nvme_pg_to_path);
	}
	return paths;
}

void release_paths(const struct context *ctx, const struct _vector *mpvec)
{
	condlog(5, "%s called for \"%s\"", __func__, THIS);
	vector_free_const(mpvec);
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
