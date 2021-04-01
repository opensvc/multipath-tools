#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <libdevmapper.h>
#include <libudev.h>
#include <errno.h>

#include "devmapper.h"
#include "structs.h"
#include "util.h"
#include "config.h"
#include "discovery.h"
#include "wwids.h"
#include "sysfs.h"
#include "mpath_cmd.h"
#include "valid.h"
#include "mpath_valid.h"
#include "debug.h"

static unsigned int
get_conf_mode(struct config *conf)
{
	if (conf->find_multipaths == FIND_MULTIPATHS_SMART)
		return MPATH_SMART;
	if (conf->find_multipaths == FIND_MULTIPATHS_GREEDY)
		return MPATH_GREEDY;
	return MPATH_STRICT;
}

static void
set_conf_mode(struct config *conf, unsigned int mode)
{
	if (mode == MPATH_SMART)
		conf->find_multipaths = FIND_MULTIPATHS_SMART;
	else if (mode == MPATH_GREEDY)
		conf->find_multipaths = FIND_MULTIPATHS_GREEDY;
	else
		conf->find_multipaths = FIND_MULTIPATHS_STRICT;
}

unsigned int
mpathvalid_get_mode(void)
{
	int mode;
	struct config *conf;

	conf = get_multipath_config();
	if (!conf)
		mode = MPATH_MODE_ERROR;
	else
		mode = get_conf_mode(conf);
	put_multipath_config(conf);
	return mode;
}

static int
convert_result(int result) {
	switch (result) {
	case PATH_IS_ERROR:
		return MPATH_IS_ERROR;
	case PATH_IS_NOT_VALID:
		return MPATH_IS_NOT_VALID;
	case PATH_IS_VALID:
		return MPATH_IS_VALID;
	case PATH_IS_VALID_NO_CHECK:
		return MPATH_IS_VALID_NO_CHECK;
	case PATH_IS_MAYBE_VALID:
		return MPATH_IS_MAYBE_VALID;
	}
	return MPATH_IS_ERROR;
}

static void
set_log_style(int log_style)
{
	/*
	 * convert MPATH_LOG_* to LOGSINK_*
	 * currently there is no work to do here.
	 */
	logsink = log_style;
}

static int
load_default_config(int verbosity)
{
	/* need to set verbosity here to control logging during init_config() */
	libmp_verbosity = verbosity;
	if (init_config(DEFAULT_CONFIGFILE))
		return -1;
	/* Need to override verbosity from init_config() */
	libmp_verbosity = verbosity;

	return 0;
}

int
mpathvalid_init(int verbosity, int log_style)
{
	unsigned int version[3];

	set_log_style(log_style);
	if (libmultipath_init())
		return -1;

	skip_libmp_dm_init();
	if (load_default_config(verbosity))
		goto fail;

	if (dm_prereq(version))
		goto fail_config;

	return 0;

fail_config:
	uninit_config();
fail:
	libmultipath_exit();
	return -1;
}

int
mpathvalid_reload_config(void)
{
	uninit_config();
	return load_default_config(libmp_verbosity);
}

int
mpathvalid_exit(void)
{
	uninit_config();
	libmultipath_exit();
	return 0;
}

/*
 * name: name of path to check
 * mode: mode to use for determination. MPATH_DEFAULT uses configured mode
 * info: on success, contains the path wwid
 * paths: array of the returned mpath_info from other claimed paths
 * nr_paths: the size of the paths array
 */
int
mpathvalid_is_path(const char *name, unsigned int mode, char **wwid,
	           const char **path_wwids, unsigned int nr_paths)
{
	struct config *conf;
	int find_multipaths_saved, r = MPATH_IS_ERROR;
	unsigned int i;
	struct path *pp;

	if (!name || mode >= MPATH_MODE_ERROR)
		return r;
	if (nr_paths > 0 && !path_wwids)
		return r;
	if (!udev)
		return r;

	pp = alloc_path();
	if (!pp)
		return r;

	if (wwid) {
		*wwid = (char *)malloc(WWID_SIZE);
		if (!*wwid)
			goto out;
	}

	conf = get_multipath_config();
	if (!conf)
		goto out_wwid;
	find_multipaths_saved = conf->find_multipaths;
	if (mode != MPATH_DEFAULT)
		set_conf_mode(conf, mode);
	r = convert_result(is_path_valid(name, conf, pp, true));
	conf->find_multipaths = find_multipaths_saved;
	put_multipath_config(conf);

	if (r == MPATH_IS_MAYBE_VALID) {
		for (i = 0; i < nr_paths; i++) {
			if (path_wwids[i] &&
			    strncmp(path_wwids[i], pp->wwid, WWID_SIZE) == 0) {
				r = MPATH_IS_VALID;
				break;
			}
		}
	}

out_wwid:
	if (wwid) {
		if (r == MPATH_IS_VALID || r == MPATH_IS_VALID_NO_CHECK ||
		    r == MPATH_IS_MAYBE_VALID)
			strlcpy(*wwid, pp->wwid, WWID_SIZE);
		else {
			free(*wwid);
			*wwid = NULL;
		}
	}
out:
	free_path(pp);
	return r;
}
