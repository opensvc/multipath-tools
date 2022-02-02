#include <libdevmapper.h>

#include "util.h"
#include "vector.h"
#include "config.h"
#include "debug.h"
#include "devmapper.h"

#include "mpath_persist.h"
#include "mpath_persist_int.h"

extern struct udev *udev;

static void adapt_config(struct config *conf)
{
	conf->force_sync = 1;
	set_max_fds(conf->max_fds);
}

int libmpathpersist_init(void)
{
	struct config *conf;
	int rc = 0;

	if (libmultipath_init()) {
		condlog(0, "Failed to initialize libmultipath.");
		return 1;
	}
	if (init_config(DEFAULT_CONFIGFILE)) {
		condlog(0, "Failed to initialize multipath config.");
		return 1;
	}
	conf = libmp_get_multipath_config();
	adapt_config(conf);
	libmp_put_multipath_config(conf);
	return rc;
}

struct config *
mpath_lib_init (void)
{
	struct config *conf;

	conf = load_config(DEFAULT_CONFIGFILE);
	if (!conf) {
		condlog(0, "Failed to initialize multipath config.");
		return NULL;
	}
	adapt_config(conf);
	return conf;
}

static void libmpathpersist_cleanup(void)
{
	libmultipath_exit();
	dm_lib_exit();
}

int
mpath_lib_exit (struct config *conf)
{
	free_config(conf);
	libmpathpersist_cleanup();
	return 0;
}

int libmpathpersist_exit(void)
{
	uninit_config();
	libmpathpersist_cleanup();
	return 0;
}

static vector curmp;
static vector pathvec;

static void __mpath_persistent_reserve_free_vecs(vector curmp, vector pathvec)
{
	free_multipathvec(curmp, KEEP_PATHS);
	free_pathvec(pathvec, FREE_PATHS);
}

void mpath_persistent_reserve_free_vecs(void)
{
	__mpath_persistent_reserve_free_vecs(curmp, pathvec);
	curmp = pathvec = NULL;
}

static int __mpath_persistent_reserve_init_vecs(vector *curmp_p,
						vector *pathvec_p, int verbose)
{
	libmp_verbosity = verbose;

	if (*curmp_p)
		return MPATH_PR_SUCCESS;
	/*
	 * allocate core vectors to store paths and multipaths
	 */
	*curmp_p = vector_alloc ();
	*pathvec_p = vector_alloc ();

	if (!*curmp_p || !*pathvec_p){
		condlog (0, "vector allocation failed.");
		goto err;
	}

	if (dm_get_maps(*curmp_p))
		goto err;

	return MPATH_PR_SUCCESS;

err:
	__mpath_persistent_reserve_free_vecs(*curmp_p, *pathvec_p);
	*curmp_p = *pathvec_p = NULL;
	return MPATH_PR_DMMP_ERROR;
}

int mpath_persistent_reserve_init_vecs(int verbose)
{
	return __mpath_persistent_reserve_init_vecs(&curmp, &pathvec, verbose);
}

int __mpath_persistent_reserve_in (int fd, int rq_servact,
	struct prin_resp *resp, int noisy)
{
	return do_mpath_persistent_reserve_in(curmp, pathvec, fd, rq_servact,
					      resp, noisy);
}


int __mpath_persistent_reserve_out ( int fd, int rq_servact, int rq_scope,
	unsigned int rq_type, struct prout_param_descriptor *paramp, int noisy)
{
	return do_mpath_persistent_reserve_out(curmp, pathvec, fd, rq_servact,
					       rq_scope, rq_type, paramp,
					       noisy);
}

int mpath_persistent_reserve_in (int fd, int rq_servact,
	struct prin_resp *resp, int noisy, int verbose)
{
	vector curmp = NULL, pathvec;
	int ret = __mpath_persistent_reserve_init_vecs(&curmp, &pathvec,
						       verbose);

	if (ret != MPATH_PR_SUCCESS)
		return ret;
	ret = do_mpath_persistent_reserve_in(curmp, pathvec, fd, rq_servact,
					     resp, noisy);
	__mpath_persistent_reserve_free_vecs(curmp, pathvec);
	return ret;
}

int mpath_persistent_reserve_out ( int fd, int rq_servact, int rq_scope,
	unsigned int rq_type, struct prout_param_descriptor *paramp, int noisy, int verbose)
{
	vector curmp = NULL, pathvec;
	int ret = __mpath_persistent_reserve_init_vecs(&curmp, &pathvec,
						       verbose);

	if (ret != MPATH_PR_SUCCESS)
		return ret;
	ret = do_mpath_persistent_reserve_out(curmp, pathvec, fd, rq_servact,
					      rq_scope, rq_type, paramp, noisy);
	__mpath_persistent_reserve_free_vecs(curmp, pathvec);
	return ret;
}
