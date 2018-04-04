#include <libdevmapper.h>
#include "defaults.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include "vector.h"
#include "checkers.h"
#include "structs.h"
#include "structs_vec.h"
#include <libudev.h>

#include "prio.h"
#include <unistd.h>
#include "devmapper.h"
#include "debug.h"
#include "config.h"
#include "switchgroup.h"
#include "discovery.h"
#include "dmparser.h"
#include <ctype.h>
#include "propsel.h"
#include "util.h"

#include "mpath_persist.h"
#include "mpathpr.h"
#include "mpath_pr_ioctl.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/resource.h>

#define __STDC_FORMAT_MACROS 1

extern struct udev *udev;

struct config *
mpath_lib_init (void)
{
	struct config *conf;

	conf = load_config(DEFAULT_CONFIGFILE);
	if (!conf) {
		condlog(0, "Failed to initialize multipath config.");
		return NULL;
	}

	if (conf->max_fds) {
		struct rlimit fd_limit;

		fd_limit.rlim_cur = conf->max_fds;
		fd_limit.rlim_max = conf->max_fds;
		if (setrlimit(RLIMIT_NOFILE, &fd_limit) < 0)
			condlog(0, "can't set open fds limit to %d : %s",
				   conf->max_fds, strerror(errno));
	}

	return conf;
}

int
mpath_lib_exit (struct config *conf)
{
	dm_lib_release();
	dm_lib_exit();
	cleanup_prio();
	cleanup_checkers();
	free_config(conf);
	conf = NULL;
	return 0;
}

static int
updatepaths (struct multipath * mpp)
{
	int i, j;
	struct pathgroup * pgp;
	struct path * pp;
	struct config *conf;

	if (!mpp->pg)
		return 0;

	vector_foreach_slot (mpp->pg, pgp, i){
		if (!pgp->paths)
			continue;

		vector_foreach_slot (pgp->paths, pp, j){
			if (!strlen(pp->dev)){
				if (devt2devname(pp->dev, FILE_NAME_SIZE,
						 pp->dev_t)){
					/*
					 * path is not in sysfs anymore
					 */
					pp->state = PATH_DOWN;
					continue;
				}
				pp->mpp = mpp;
				conf = get_multipath_config();
				pathinfo(pp, conf, DI_ALL);
				put_multipath_config(conf);
				continue;
			}
			pp->mpp = mpp;
			if (pp->state == PATH_UNCHECKED ||
					pp->state == PATH_WILD) {
				conf = get_multipath_config();
				pathinfo(pp, conf, DI_CHECKER);
				put_multipath_config(conf);
			}

			if (pp->priority == PRIO_UNDEF) {
				conf = get_multipath_config();
				pathinfo(pp, conf, DI_PRIO);
				put_multipath_config(conf);
			}
		}
	}
	return 0;
}

int
mpath_prin_activepath (struct multipath *mpp, int rq_servact,
	struct prin_resp * resp, int noisy)
{
	int i,j, ret = MPATH_PR_DMMP_ERROR;
	struct pathgroup *pgp = NULL;
	struct path *pp = NULL;

	vector_foreach_slot (mpp->pg, pgp, j){
		vector_foreach_slot (pgp->paths, pp, i){
			if (!((pp->state == PATH_UP) ||
			      (pp->state == PATH_GHOST))){
				condlog(2, "%s: %s not available. Skip.",
					mpp->wwid, pp->dev);
				condlog(3, "%s: status = %d.",
					mpp->wwid, pp->state);
				continue;
			}

			condlog(3, "%s: sending pr in command to %s ",
				mpp->wwid, pp->dev);
			ret = mpath_send_prin_activepath(pp->dev, rq_servact,
							 resp, noisy);
			switch(ret)
			{
				case MPATH_PR_SUCCESS:
				case MPATH_PR_SENSE_INVALID_OP:
					return ret;
				default:
					continue;
			}
		}
	}
	return ret;
}

int mpath_persistent_reserve_in (int fd, int rq_servact,
	struct prin_resp *resp, int noisy, int verbose)
{
	struct stat info;
	vector curmp = NULL;
	vector pathvec = NULL;
	char * alias;
	struct multipath * mpp;
	int map_present;
	int major, minor;
	int ret;
	struct config *conf;

	conf = get_multipath_config();
	conf->verbosity = verbose;
	put_multipath_config(conf);

	if (fstat( fd, &info) != 0){
		condlog(0, "stat error %d", fd);
		return MPATH_PR_FILE_ERROR;
	}
	if(!S_ISBLK(info.st_mode)){
		condlog(0, "Failed to get major:minor. fd = %d", fd);
		return MPATH_PR_FILE_ERROR;
	}

	major = major(info.st_rdev);
	minor = minor(info.st_rdev);
	condlog(4, "Device %d:%d:  ", major, minor);

	/* get alias from major:minor*/
	alias = dm_mapname(major, minor);
	if (!alias){
		condlog(0, "%d:%d failed to get device alias.", major, minor);
		return MPATH_PR_DMMP_ERROR;
	}

	condlog(3, "alias = %s", alias);
	map_present = dm_map_present(alias);
	if (map_present && !dm_is_mpath(alias)){
		condlog( 0, "%s: not a multipath device.", alias);
		ret = MPATH_PR_DMMP_ERROR;
		goto out;
	}

	/*
	 * allocate core vectors to store paths and multipaths
	 */
	curmp = vector_alloc ();
	pathvec = vector_alloc ();

	if (!curmp || !pathvec){
		condlog (0, "%s: vector allocation failed.", alias);
		ret = MPATH_PR_DMMP_ERROR;
		if (curmp)
			vector_free(curmp);
		if (pathvec)
			vector_free(pathvec);
		goto out;
	}

	if (path_discovery(pathvec, DI_SYSFS | DI_CHECKER) < 0) {
		ret = MPATH_PR_DMMP_ERROR;
		goto out1;
	}

	/* get info of all paths from the dm device	*/
	if (get_mpvec (curmp, pathvec, alias)){
		condlog(0, "%s: failed to get device info.", alias);
		ret = MPATH_PR_DMMP_ERROR;
		goto out1;
	}

	mpp = find_mp_by_alias(curmp, alias);
	if (!mpp){
		condlog(0, "%s: devmap not registered.", alias);
		ret = MPATH_PR_DMMP_ERROR;
		goto out1;
	}

	ret = mpath_prin_activepath(mpp, rq_servact, resp, noisy);

out1:
	free_multipathvec(curmp, KEEP_PATHS);
	free_pathvec(pathvec, FREE_PATHS);
out:
	FREE(alias);
	return ret;
}

int mpath_persistent_reserve_out ( int fd, int rq_servact, int rq_scope,
	unsigned int rq_type, struct prout_param_descriptor *paramp, int noisy, int verbose)
{

	struct stat info;

	vector curmp = NULL;
	vector pathvec = NULL;

	char * alias;
	struct multipath * mpp;
	int map_present;
	int major, minor;
	int ret;
	uint64_t prkey;
	struct config *conf;

	conf = get_multipath_config();
	conf->verbosity = verbose;
	put_multipath_config(conf);

	if (fstat( fd, &info) != 0){
		condlog(0, "stat error fd=%d", fd);
		return MPATH_PR_FILE_ERROR;
	}

	if(!S_ISBLK(info.st_mode)){
		condlog(3, "Failed to get major:minor. fd=%d", fd);
		return MPATH_PR_FILE_ERROR;
	}

	major = major(info.st_rdev);
	minor = minor(info.st_rdev);
	condlog(4, "Device  %d:%d", major, minor);

	/* get WWN of the device from major:minor*/
	alias = dm_mapname(major, minor);
	if (!alias){
		return MPATH_PR_DMMP_ERROR;
	}

	condlog(3, "alias = %s", alias);
	map_present = dm_map_present(alias);

	if (map_present && !dm_is_mpath(alias)){
		condlog(3, "%s: not a multipath device.", alias);
		ret = MPATH_PR_DMMP_ERROR;
		goto out;
	}

	/*
	 * allocate core vectors to store paths and multipaths
	 */
	curmp = vector_alloc ();
	pathvec = vector_alloc ();

	if (!curmp || !pathvec){
		condlog (0, "%s: vector allocation failed.", alias);
		ret = MPATH_PR_DMMP_ERROR;
		if (curmp)
			vector_free(curmp);
		if (pathvec)
			vector_free(pathvec);
		goto out;
	}

	if (path_discovery(pathvec, DI_SYSFS | DI_CHECKER) < 0) {
		ret = MPATH_PR_DMMP_ERROR;
		goto out1;
	}

	/* get info of all paths from the dm device     */
	if (get_mpvec(curmp, pathvec, alias)){
		condlog(0, "%s: failed to get device info.", alias);
		ret = MPATH_PR_DMMP_ERROR;
		goto out1;
	}

	mpp = find_mp_by_alias(curmp, alias);

	if (!mpp) {
		condlog(0, "%s: devmap not registered.", alias);
		ret = MPATH_PR_DMMP_ERROR;
		goto out1;
	}

	conf = get_multipath_config();
	select_reservation_key(conf, mpp);
	put_multipath_config(conf);

	memcpy(&prkey, paramp->sa_key, 8);
	if (mpp->prkey_source == PRKEY_SOURCE_FILE && prkey &&
	    ((!get_be64(mpp->reservation_key) &&
	      rq_servact == MPATH_PROUT_REG_SA) ||
	     rq_servact == MPATH_PROUT_REG_IGN_SA)) {
		memcpy(&mpp->reservation_key, paramp->sa_key, 8);
		if (update_prkey(alias, get_be64(mpp->reservation_key))) {
			condlog(0, "%s: failed to set prkey for multipathd.",
				alias);
			ret = MPATH_PR_DMMP_ERROR;
			goto out1;
		}
	}

	if (memcmp(paramp->key, &mpp->reservation_key, 8) &&
	    memcmp(paramp->sa_key, &mpp->reservation_key, 8)) {
		condlog(0, "%s: configured reservation key doesn't match: 0x%" PRIx64, alias, get_be64(mpp->reservation_key));
		ret = MPATH_PR_SYNTAX_ERROR;
		goto out1;
	}

	switch(rq_servact)
	{
	case MPATH_PROUT_REG_SA:
	case MPATH_PROUT_REG_IGN_SA:
		ret= mpath_prout_reg(mpp, rq_servact, rq_scope, rq_type, paramp, noisy);
		break;
	case MPATH_PROUT_RES_SA :
	case MPATH_PROUT_PREE_SA :
	case MPATH_PROUT_PREE_AB_SA :
	case MPATH_PROUT_CLEAR_SA:
		ret = mpath_prout_common(mpp, rq_servact, rq_scope, rq_type, paramp, noisy);
		break;
	case MPATH_PROUT_REL_SA:
		ret = mpath_prout_rel(mpp, rq_servact, rq_scope, rq_type, paramp, noisy);
		break;
	default:
		ret = MPATH_PR_OTHER;
		goto out1;
	}

	if ((ret == MPATH_PR_SUCCESS) && ((rq_servact == MPATH_PROUT_REG_SA) ||
				(rq_servact ==  MPATH_PROUT_REG_IGN_SA)))
	{
		if (prkey == 0) {
			update_prflag(alias, 0);
			update_prkey(alias, 0);
		} else
			update_prflag(alias, 1);
	} else if ((ret == MPATH_PR_SUCCESS) && (rq_servact == MPATH_PROUT_CLEAR_SA)) {
		update_prflag(alias, 0);
		update_prkey(alias, 0);
	}
out1:
	free_multipathvec(curmp, KEEP_PATHS);
	free_pathvec(pathvec, FREE_PATHS);

out:
	FREE(alias);
	return ret;
}

int
get_mpvec (vector curmp, vector pathvec, char * refwwid)
{
	int i;
	struct multipath *mpp;
	char params[PARAMS_SIZE], status[PARAMS_SIZE];

	if (dm_get_maps (curmp)){
		return 1;
	}

	vector_foreach_slot (curmp, mpp, i){
		/*
		 * discard out of scope maps
		 */
		if (mpp->alias && refwwid &&
		    strncmp (mpp->alias, refwwid, WWID_SIZE - 1)){
			free_multipath (mpp, KEEP_PATHS);
			vector_del_slot (curmp, i);
			i--;
			continue;
		}

		dm_get_map(mpp->alias, &mpp->size, params);
		condlog(3, "params = %s", params);
		dm_get_status(mpp->alias, status);
		condlog(3, "status = %s", status);
		disassemble_map (pathvec, params, mpp, 0);

		/*
		 * disassemble_map() can add new paths to pathvec.
		 * If not in "fast list mode", we need to fetch information
		 * about them
		 */
		updatepaths(mpp);
		mpp->bestpg = select_path_group (mpp);
		disassemble_status (status, mpp);

	}
	return MPATH_PR_SUCCESS ;
}

int mpath_send_prin_activepath (char * dev, int rq_servact,
				struct prin_resp * resp, int noisy)
{

	int rc;

	rc = prin_do_scsi_ioctl(dev, rq_servact, resp,  noisy);

	return (rc);
}

int mpath_prout_reg(struct multipath *mpp,int rq_servact, int rq_scope,
	unsigned int rq_type, struct prout_param_descriptor * paramp, int noisy)
{

	int i, j;
	struct pathgroup *pgp = NULL;
	struct path *pp = NULL;
	int rollback = 0;
	int active_pathcount=0;
	int rc;
	int count=0;
	int status = MPATH_PR_SUCCESS;
	uint64_t sa_key = 0;

	if (!mpp)
		return MPATH_PR_DMMP_ERROR;

	active_pathcount = pathcount(mpp, PATH_UP) + pathcount(mpp, PATH_GHOST);

	if (active_pathcount == 0) {
		condlog (0, "%s: no path available", mpp->wwid);
		return MPATH_PR_DMMP_ERROR;
	}

	if ( paramp->sa_flags & MPATH_F_ALL_TG_PT_MASK ) {
		condlog (1, "Warning: ALL_TG_PT is set. Configuration not supported");
	}

	struct threadinfo thread[active_pathcount];

	memset(thread, 0, sizeof(thread));

	/* init thread parameter */
	for (i =0; i< active_pathcount; i++){
		thread[i].param.rq_servact = rq_servact;
		thread[i].param.rq_scope = rq_scope;
		thread[i].param.rq_type = rq_type;
		thread[i].param.paramp = paramp;
		thread[i].param.noisy = noisy;
		thread[i].param.status = MPATH_PR_SKIP;

		condlog (3, "THREAD ID [%d] INFO]", i);
		condlog (3, "rq_servact=%d ", thread[i].param.rq_servact);
		condlog (3, "rq_scope=%d ", thread[i].param.rq_scope);
		condlog (3, "rq_type=%d ", thread[i].param.rq_type);
		condlog (3, "rkey=");
		condlog (3, "paramp->sa_flags =%02x ",
			 thread[i].param.paramp->sa_flags);
		condlog (3, "noisy=%d ", thread[i].param.noisy);
		condlog (3, "status=%d ", thread[i].param.status);
	}

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	vector_foreach_slot (mpp->pg, pgp, j){
		vector_foreach_slot (pgp->paths, pp, i){
			if (!((pp->state == PATH_UP) || (pp->state == PATH_GHOST))){
				condlog (1, "%s: %s path not up. Skip.", mpp->wwid, pp->dev);
				continue;
			}
			strncpy(thread[count].param.dev, pp->dev,
				FILE_NAME_SIZE - 1);

			if (count && (thread[count].param.paramp->sa_flags & MPATH_F_SPEC_I_PT_MASK)){
				/*
				 * Clearing SPEC_I_PT as transportids are already registered by now.
				 */
				thread[count].param.paramp->sa_flags &= (~MPATH_F_SPEC_I_PT_MASK);
			}

			condlog (3, "%s: sending pr out command to %s", mpp->wwid, pp->dev);

			rc = pthread_create(&thread[count].id, &attr, mpath_prout_pthread_fn, (void *)(&thread[count].param));
			if (rc){
				condlog (0, "%s: failed to create thread %d", mpp->wwid, rc);
				thread[count].param.status = MPATH_PR_THREAD_ERROR;
			}
			count = count + 1;
		}
	}
	for( i=0; i < active_pathcount ; i++){
		if (thread[i].param.status != MPATH_PR_THREAD_ERROR) {
			rc = pthread_join(thread[i].id, NULL);
			if (rc){
				condlog (0, "%s: Thread[%d] failed to join thread %d", mpp->wwid, i, rc);
			}
		}
		if (!rollback && (thread[i].param.status == MPATH_PR_RESERV_CONFLICT)){
			rollback = 1;
			sa_key = 0;
			for (i = 0; i < 8; ++i){
				if (i > 0)
					sa_key <<= 8;
				sa_key |= paramp->sa_key[i];
			}
			status = MPATH_PR_RESERV_CONFLICT ;
		}
		if (!rollback && (status == MPATH_PR_SUCCESS)){
			status = thread[i].param.status;
		}
	}
	if (rollback && ((rq_servact == MPATH_PROUT_REG_SA) && sa_key != 0 )){
		condlog (3, "%s: ERROR: initiating pr out rollback", mpp->wwid);
		for( i=0 ; i < active_pathcount ; i++){
			if(thread[i].param.status == MPATH_PR_SUCCESS) {
				memcpy(&thread[i].param.paramp->key, &thread[i].param.paramp->sa_key, 8);
				memset(&thread[i].param.paramp->sa_key, 0, 8);
				thread[i].param.status = MPATH_PR_SUCCESS;
				rc = pthread_create(&thread[i].id, &attr, mpath_prout_pthread_fn,
						(void *)(&thread[i].param));
				if (rc){
					condlog (0, "%s: failed to create thread for rollback. %d",  mpp->wwid, rc);
					thread[i].param.status = MPATH_PR_THREAD_ERROR;
				}
			} else
				thread[i].param.status = MPATH_PR_SKIP;
		}
		for(i=0; i < active_pathcount ; i++){
			if (thread[i].param.status != MPATH_PR_SKIP &&
			    thread[i].param.status != MPATH_PR_THREAD_ERROR) {
				rc = pthread_join(thread[i].id, NULL);
				if (rc){
					condlog (3, "%s: failed to join thread while rolling back %d",
						 mpp->wwid, i);
				}
			}
		}
	}

	pthread_attr_destroy(&attr);
	return (status);
}

void * mpath_prout_pthread_fn(void *p)
{
	int ret;
	struct prout_param * param = (struct prout_param *)p;

	ret = prout_do_scsi_ioctl( param->dev,param->rq_servact, param->rq_scope,
			param->rq_type, param->paramp, param->noisy);
	param->status = ret;
	pthread_exit(NULL);
}

int mpath_prout_common(struct multipath *mpp,int rq_servact, int rq_scope,
	unsigned int rq_type, struct prout_param_descriptor* paramp, int noisy)
{
	int i,j, ret;
	struct pathgroup *pgp = NULL;
	struct path *pp = NULL;

	vector_foreach_slot (mpp->pg, pgp, j){
		vector_foreach_slot (pgp->paths, pp, i){
			if (!((pp->state == PATH_UP) || (pp->state == PATH_GHOST))){
				condlog (1, "%s: %s path not up. Skip",
					 mpp->wwid, pp->dev);
				continue;
			}

			condlog (3, "%s: sending pr out command to %s", mpp->wwid, pp->dev);
			ret = send_prout_activepath(pp->dev, rq_servact,
						    rq_scope, rq_type,
						    paramp, noisy);
			return ret ;
		}
	}
	return MPATH_PR_SUCCESS;
}

int send_prout_activepath(char * dev, int rq_servact, int rq_scope,
	unsigned int rq_type, struct prout_param_descriptor * paramp, int noisy)
{
	struct prout_param param;
	param.rq_servact = rq_servact;
	param.rq_scope  = rq_scope;
	param.rq_type   = rq_type;
	param.paramp    = paramp;
	param.noisy = noisy;
	param.status = -1;

	pthread_t thread;
	pthread_attr_t attr;
	int rc;

	memset(&thread, 0, sizeof(thread));
	strncpy(param.dev, dev, FILE_NAME_SIZE - 1);
	/* Initialize and set thread joinable attribute */
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	rc = pthread_create(&thread, &attr, mpath_prout_pthread_fn, (void *)(&param));
	if (rc){
		condlog (3, "%s: failed to create thread %d", dev, rc);
		return MPATH_PR_THREAD_ERROR;
	}
	/* Free attribute and wait for the other threads */
	pthread_attr_destroy(&attr);
	rc = pthread_join(thread, NULL);

	return (param.status);
}

int mpath_prout_rel(struct multipath *mpp,int rq_servact, int rq_scope,
	unsigned int rq_type, struct prout_param_descriptor * paramp, int noisy)
{
	int i, j;
	int num = 0;
	struct pathgroup *pgp = NULL;
	struct path *pp = NULL;
	int active_pathcount = 0;
	pthread_attr_t attr;
	int rc, found = 0;
	int count = 0;
	int status = MPATH_PR_SUCCESS;
	struct prin_resp resp;
	struct prout_param_descriptor *pamp;
	struct prin_resp *pr_buff;
	int length;
	struct transportid *pptr;

	if (!mpp)
		return MPATH_PR_DMMP_ERROR;

	active_pathcount = pathcount (mpp, PATH_UP) + pathcount (mpp, PATH_GHOST);

	struct threadinfo thread[active_pathcount];
	memset(thread, 0, sizeof(thread));
	for (i = 0; i < active_pathcount; i++){
		thread[i].param.rq_servact = rq_servact;
		thread[i].param.rq_scope = rq_scope;
		thread[i].param.rq_type = rq_type;
		thread[i].param.paramp = paramp;
		thread[i].param.noisy = noisy;
		thread[i].param.status = MPATH_PR_SKIP;

		condlog (3, " path count = %d", i);
		condlog (3, "rq_servact=%d ", thread[i].param.rq_servact);
		condlog (3, "rq_scope=%d ", thread[i].param.rq_scope);
		condlog (3, "rq_type=%d ", thread[i].param.rq_type);
		condlog (3, "noisy=%d ", thread[i].param.noisy);
		condlog (3, "status=%d ", thread[i].param.status);
	}

	pthread_attr_init (&attr);
	pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_JOINABLE);

	vector_foreach_slot (mpp->pg, pgp, j){
		vector_foreach_slot (pgp->paths, pp, i){
			if (!((pp->state == PATH_UP) || (pp->state == PATH_GHOST))){
				condlog (1, "%s: %s path not up.", mpp->wwid, pp->dev);
				continue;
			}

			strncpy(thread[count].param.dev, pp->dev,
				FILE_NAME_SIZE - 1);
			condlog (3, "%s: sending pr out command to %s", mpp->wwid, pp->dev);
			rc = pthread_create (&thread[count].id, &attr, mpath_prout_pthread_fn,
					(void *) (&thread[count].param));
			if (rc) {
				condlog (0, "%s: failed to create thread. %d",  mpp->wwid, rc);
				thread[count].param.status = MPATH_PR_THREAD_ERROR;
			}
			count = count + 1;
		}
	}
	pthread_attr_destroy (&attr);
	for (i = 0; i < active_pathcount; i++){
		if (thread[i].param.status != MPATH_PR_THREAD_ERROR) {
			rc = pthread_join (thread[i].id, NULL);
			if (rc){
				condlog (1, "%s: failed to join thread.  %d",  mpp->wwid,  rc);
			}
		}
	}

	for (i = 0; i < active_pathcount; i++){
		/*  check thread status here and return the status */

		if (thread[i].param.status == MPATH_PR_RESERV_CONFLICT)
			status = MPATH_PR_RESERV_CONFLICT;
		else if (status == MPATH_PR_SUCCESS
				&& thread[i].param.status != MPATH_PR_RESERV_CONFLICT)
			status = thread[i].param.status;
	}

	status = mpath_prin_activepath (mpp, MPATH_PRIN_RRES_SA, &resp, noisy);
	if (status != MPATH_PR_SUCCESS){
		condlog (0, "%s: pr in read reservation command failed.", mpp->wwid);
		return MPATH_PR_OTHER;
	}

	num = resp.prin_descriptor.prin_readresv.additional_length / 8;
	if (num == 0){
		condlog (2, "%s: Path holding reservation is released.", mpp->wwid);
		return MPATH_PR_SUCCESS;
	}
	condlog (2, "%s: Path holding reservation is not avialable.", mpp->wwid);

	pr_buff =  mpath_alloc_prin_response(MPATH_PRIN_RFSTAT_SA);
	if (!pr_buff){
		condlog (0, "%s: failed to  alloc pr in response buffer.", mpp->wwid);
		return MPATH_PR_OTHER;
	}

	status = mpath_prin_activepath (mpp, MPATH_PRIN_RFSTAT_SA, pr_buff, noisy);

	if (status != MPATH_PR_SUCCESS){
		condlog (0,  "%s: pr in read full status command failed.",  mpp->wwid);
		goto out;
	}

	num = pr_buff->prin_descriptor.prin_readfd.number_of_descriptor;
	if (0 == num){
		goto out;
	}
	length = sizeof (struct prout_param_descriptor) + (sizeof (struct transportid *));

	pamp = (struct prout_param_descriptor *)malloc (length);
	if (!pamp){
		condlog (0, "%s: failed to alloc pr out parameter.", mpp->wwid);
		goto out1;
	}

	memset(pamp, 0, length);

	pamp->trnptid_list[0] = (struct transportid *) malloc (sizeof (struct transportid));
	if (!pamp->trnptid_list[0]){
		condlog (0, "%s: failed to alloc pr out transportid.", mpp->wwid);
		goto out1;
	}

	if (get_be64(mpp->reservation_key)){
		memcpy (pamp->key, &mpp->reservation_key, 8);
		condlog (3, "%s: reservation key set.", mpp->wwid);
	}

	status = mpath_prout_common (mpp, MPATH_PROUT_CLEAR_SA,
				     rq_scope, rq_type, pamp, noisy);

	if (status) {
		condlog(0, "%s: failed to send CLEAR_SA", mpp->wwid);
		goto out1;
	}

	pamp->num_transportid = 1;
	pptr=pamp->trnptid_list[0];

	for (i = 0; i < num; i++){
		if (get_be64(mpp->reservation_key) &&
			memcmp(pr_buff->prin_descriptor.prin_readfd.descriptors[i]->key,
			       &mpp->reservation_key, 8)){
			/*register with tarnsport id*/
			memset(pamp, 0, length);
			pamp->trnptid_list[0] = pptr;
			memset (pamp->trnptid_list[0], 0, sizeof (struct transportid));
			memcpy (pamp->sa_key,
					pr_buff->prin_descriptor.prin_readfd.descriptors[i]->key, 8);
			pamp->sa_flags = MPATH_F_SPEC_I_PT_MASK;
			pamp->num_transportid = 1;

			memcpy (pamp->trnptid_list[0],
					&pr_buff->prin_descriptor.prin_readfd.descriptors[i]->trnptid,
					sizeof (struct transportid));
			status = mpath_prout_common (mpp, MPATH_PROUT_REG_SA, 0, rq_type,
					pamp, noisy);

			pamp->sa_flags = 0;
			memcpy (pamp->key, pr_buff->prin_descriptor.prin_readfd.descriptors[i]->key, 8);
			memset (pamp->sa_key, 0, 8);
			pamp->num_transportid = 0;
			status = mpath_prout_common (mpp, MPATH_PROUT_REG_SA, 0, rq_type,
					pamp, noisy);
		}
		else
		{
			if (get_be64(mpp->reservation_key))
				found = 1;
		}


	}

	if (found){
		memset (pamp, 0, length);
		memcpy (pamp->sa_key, &mpp->reservation_key, 8);
		memset (pamp->key, 0, 8);
		status = mpath_prout_reg(mpp, MPATH_PROUT_REG_SA, rq_scope, rq_type, pamp, noisy);
	}


	free(pptr);
out1:
	free (pamp);
out:
	free (pr_buff);
	return (status);
}

void * mpath_alloc_prin_response(int prin_sa)
{
	void * ptr = NULL;
	int size=0;
	switch (prin_sa)
	{
		case MPATH_PRIN_RKEY_SA:
			size = sizeof(struct prin_readdescr);
			break;
		case MPATH_PRIN_RRES_SA:
			size = sizeof(struct prin_resvdescr);
			break;
		case MPATH_PRIN_RCAP_SA:
			size=sizeof(struct prin_capdescr);
			break;
		case MPATH_PRIN_RFSTAT_SA:
			size = sizeof(struct print_fulldescr_list) +
				sizeof(struct prin_fulldescr *)*MPATH_MX_TIDS;
			break;
	}
	if (size > 0)
	{
		ptr = calloc(size, 1);
	}
	return ptr;
}

int update_map_pr(struct multipath *mpp)
{
	int noisy=0;
	struct prin_resp *resp;
	int i, ret, isFound;

	if (!get_be64(mpp->reservation_key))
	{
		/* Nothing to do. Assuming pr mgmt feature is disabled*/
		condlog(3, "%s: reservation_key not set in multipath.conf", mpp->alias);
		return MPATH_PR_SUCCESS;
	}

	resp = mpath_alloc_prin_response(MPATH_PRIN_RKEY_SA);
	if (!resp)
	{
		condlog(0,"%s : failed to alloc resp in update_map_pr", mpp->alias);
		return MPATH_PR_OTHER;
	}
	ret = mpath_prin_activepath(mpp, MPATH_PRIN_RKEY_SA, resp, noisy);

	if (ret != MPATH_PR_SUCCESS )
	{
		condlog(0,"%s : pr in read keys service action failed Error=%d", mpp->alias, ret);
		free(resp);
		return  ret;
	}

	if (resp->prin_descriptor.prin_readkeys.additional_length == 0 )
	{
		condlog(3,"%s: No key found. Device may not be registered. ", mpp->alias);
		free(resp);
		return MPATH_PR_SUCCESS;
	}

	condlog(2, "%s: Multipath  reservation_key: 0x%" PRIx64 " ", mpp->alias,
		get_be64(mpp->reservation_key));

	isFound =0;
	for (i = 0; i < resp->prin_descriptor.prin_readkeys.additional_length/8; i++ )
	{
		condlog(2, "%s: PR IN READKEYS[%d]  reservation key:", mpp->alias, i);
		dumpHex((char *)&resp->prin_descriptor.prin_readkeys.key_list[i*8], 8 , 1);

		if (!memcmp(&mpp->reservation_key, &resp->prin_descriptor.prin_readkeys.key_list[i*8], 8))
		{
			condlog(2, "%s: reservation key found in pr in readkeys response", mpp->alias);
			isFound =1;
		}
	}

	if (isFound)
	{
		mpp->prflag = 1;
		condlog(2, "%s: prflag flag set.", mpp->alias );
	}

	free(resp);
	return MPATH_PR_SUCCESS;
}
