#include <libdevmapper.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <libudev.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "vector.h"
#include "defaults.h"
#include "checkers.h"
#include "structs.h"
#include "structs_vec.h"
#include "prio.h"
#include "devmapper.h"
#include "debug.h"
#include "config.h"
#include "switchgroup.h"
#include "discovery.h"
#include "configure.h"
#include "dmparser.h"
#include "propsel.h"
#include "util.h"
#include "unaligned.h"

#include "mpath_persist.h"
#include "mpath_persist_int.h"
#include "mpathpr.h"
#include "mpath_pr_ioctl.h"

struct prout_param {
	char dev[FILE_NAME_SIZE];
	int rq_servact;
	int rq_scope;
	unsigned int rq_type;
	struct prout_param_descriptor  *paramp;
	int noisy;
	int status;
};

struct threadinfo {
	int status;
	pthread_t id;
	struct prout_param param;
};

static int mpath_prin_activepath (struct multipath *mpp, int rq_servact,
	struct prin_resp * resp, int noisy)
{
	int i,j, ret = MPATH_PR_DMMP_ERROR;
	struct pathgroup *pgp = NULL;
	struct path *pp = NULL;

	vector_foreach_slot (mpp->pg, pgp, j){
		vector_foreach_slot (pgp->paths, pp, i){
			if (!((pp->state == PATH_UP) ||
			      (pp->state == PATH_GHOST))){
				condlog(3, "%s: %s not available. Skip.",
					mpp->wwid, pp->dev);
				condlog(3, "%s: status = %d.",
					mpp->wwid, pp->state);
				continue;
			}

			condlog(3, "%s: sending pr in command to %s ",
				mpp->wwid, pp->dev);
			ret = prin_do_scsi_ioctl(pp->dev, rq_servact, resp, noisy);
			switch(ret)
			{
				case MPATH_PR_SUCCESS:
				case MPATH_PR_ILLEGAL_REQ:
					return ret;
				default:
					continue;
			}
		}
	}
	return (ret == MPATH_PR_RETRYABLE_ERROR) ? MPATH_PR_OTHER : ret;
}

void *mpath_alloc_prin_response(int prin_sa)
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

static int get_path_info(struct multipath *mpp, vector pathvec)
{
	if (update_multipath_table(mpp, pathvec, DI_CHECKER) != DMP_OK ||
	    update_mpp_paths(mpp, pathvec)) {
		condlog(0, "error parsing map %s", mpp->wwid);
		return MPATH_PR_DMMP_ERROR;
	}
	extract_hwe_from_path(mpp);
	return MPATH_PR_SUCCESS ;
}

static int mpath_get_map(vector curmp, int fd, struct multipath **pmpp)
{
	int rc;
	struct stat info;
	char alias[WWID_SIZE];
	struct multipath *mpp;

	if (fstat(fd, &info) != 0){
		condlog(0, "stat error fd=%d", fd);
		return MPATH_PR_FILE_ERROR;
	}
	if(!S_ISBLK(info.st_mode)){
		condlog(3, "Failed to get major:minor. fd=%d", fd);
		return MPATH_PR_FILE_ERROR;
	}

	/* get alias from major:minor*/
	rc = libmp_mapinfo(DM_MAP_BY_DEVT | MAPINFO_MPATH_ONLY | MAPINFO_CHECK_UUID,
			   (mapid_t) { .devt = info.st_rdev },
			   (mapinfo_t) { .name = alias });

	if (rc == DMP_NO_MATCH) {
		condlog(3, "%s: not a multipath device.", alias);
		return MPATH_PR_DMMP_ERROR;
	} else if (rc != DMP_OK) {
		condlog(1, "%d:%d failed to get device alias.",
			major(info.st_rdev), minor(info.st_rdev));
		return MPATH_PR_DMMP_ERROR;
	}

	condlog(4, "alias = %s", alias);

	mpp = find_mp_by_alias(curmp, alias);

	if (!mpp) {
		condlog(0, "%s: devmap not registered.", alias);
		return MPATH_PR_DMMP_ERROR;
	}

	if (pmpp)
		*pmpp = mpp;

	return MPATH_PR_SUCCESS;
}

int do_mpath_persistent_reserve_in(vector curmp, vector pathvec,
				   int fd, int rq_servact,
				   struct prin_resp *resp, int noisy)
{
	struct multipath *mpp;
	int ret;

	ret = mpath_get_map(curmp, fd, &mpp);
	if (ret != MPATH_PR_SUCCESS)
		return ret;

	ret = get_path_info(mpp, pathvec);
	if (ret != MPATH_PR_SUCCESS)
		return ret;

	ret = mpath_prin_activepath(mpp, rq_servact, resp, noisy);

	return ret;
}

static void *mpath_prout_pthread_fn(void *p)
{
	int ret;
	struct prout_param * param = (struct prout_param *)p;

	ret = prout_do_scsi_ioctl( param->dev,param->rq_servact, param->rq_scope,
			param->rq_type, param->paramp, param->noisy);
	param->status = ret;
	pthread_exit(NULL);
}

static int
mpath_prout_common(struct multipath *mpp, int rq_servact, int rq_scope,
		   unsigned int rq_type, struct prout_param_descriptor *paramp,
		   int noisy, struct path **pptr)
{
	int i, j, ret;
	struct pathgroup *pgp = NULL;
	struct path *pp = NULL;
	bool found = false;
	bool conflict = false;

	vector_foreach_slot (mpp->pg, pgp, j) {
		vector_foreach_slot (pgp->paths, pp, i) {
			if (!((pp->state == PATH_UP) || (pp->state == PATH_GHOST))) {
				condlog(1, "%s: %s path not up. Skip",
					mpp->wwid, pp->dev);
				continue;
			}

			condlog(3, "%s: sending pr out command to %s",
				mpp->wwid, pp->dev);
			found = true;
			ret = prout_do_scsi_ioctl(pp->dev, rq_servact, rq_scope,
						  rq_type, paramp, noisy);
			if (ret == MPATH_PR_SUCCESS && pptr)
				*pptr = pp;
			/*
			 * If this path is considered down by the kernel,
			 * it may have just come back up, and multipathd
			 * may not have had time to update the key. Allow
			 * reservation conflicts.
			 *
			 * If you issue a RESERVE to a regular scsi device
			 * that already holds the reservation, it succeeds
			 * (and does nothing). A multipath device that
			 * holds the reservation should not return a
			 * reservation conflict on a RESERVE command, just
			 * because it issued the RESERVE to a path that
			 * isn't holding the reservation. It should instead
			 * keep trying to see if it succeeds on another
			 * path.
			 */
			if (ret == MPATH_PR_RESERV_CONFLICT &&
			    (pp->dmstate == PSTATE_FAILED ||
			     rq_servact == MPATH_PROUT_RES_SA)) {
				conflict = true;
				continue;
			}
			if (ret != MPATH_PR_RETRYABLE_ERROR)
				return ret;
		}
	}
	if (found)
		return conflict ? MPATH_PR_RESERV_CONFLICT : MPATH_PR_OTHER;
	condlog(0, "%s: no path available", mpp->wwid);
	return MPATH_PR_DMMP_ERROR;
}

/*
 * If you are changing the key registered to a device, and that device is
 * holding the reservation on a path that couldn't get its key updated,
 * either because it is down or no longer part of the multipath device,
 * you need to preempt the reservation to a usable path with the new key
 *
 * Also, it's possible that the reservation was preempted, and the device
 * is being re-registered. If it appears that is the case, clear
 * mpp->prhold in multipathd.
 */
void preempt_missing_path(struct multipath *mpp, uint8_t *key, uint8_t *sa_key,
			  int noisy)
{
	uint8_t zero[8] = {0};
	struct prin_resp resp = {{{.prgeneration = 0}}};
	int rq_scope;
	unsigned int rq_type;
	struct prout_param_descriptor paramp = {.sa_flags = 0};
	int status;

	/*
	 * If you previously didn't have a key registered, you can't
	 * be holding the reservation. Also, you can't preempt if you
	 * no longer have a registered key
	 */
	if (memcmp(key, zero, 8) == 0 || memcmp(sa_key, zero, 8) == 0) {
		update_prhold(mpp->alias, false);
		return;
	}
	/*
	 * If you didn't switch to a different key, there is no need to
	 * preempt.
	 */
	if (memcmp(key, sa_key, 8) == 0)
		return;

	status = mpath_prin_activepath(mpp, MPATH_PRIN_RRES_SA, &resp, noisy);
	if (status != MPATH_PR_SUCCESS) {
		condlog(0, "%s: register: pr in read reservation command failed.",
			mpp->wwid);
		return;
	}
	/* If there is no reservation, there's nothing to preempt */
	if (!resp.prin_descriptor.prin_readresv.additional_length) {
		update_prhold(mpp->alias, false);
		return;
	}
	/*
	 * If there reservation is not held by the old key, you don't
	 * want to preempt it
	 */
	if (memcmp(key, resp.prin_descriptor.prin_readresv.key, 8) != 0) {
		/*
		 * If reservation key doesn't match either the old or
		 * the new key, then clear prhold.
		 */
		if (memcmp(sa_key, resp.prin_descriptor.prin_readresv.key, 8) != 0)
			update_prhold(mpp->alias, false);
		return;
	}

	/*
	 * If multipathd doesn't think it is holding the reservation, don't
	 * preempt it
	 */
	if (get_prhold(mpp->alias) != PR_SET)
		return;
	/* Assume this key is being held by an inaccessable path on this
	 * node. libmpathpersist has never worked if multiple nodes share
	 * the same reservation key for a device
	 */
	rq_type = resp.prin_descriptor.prin_readresv.scope_type & MPATH_PR_TYPE_MASK;
	rq_scope = (resp.prin_descriptor.prin_readresv.scope_type &
		    MPATH_PR_SCOPE_MASK) >>
		   4;
	memcpy(paramp.key, sa_key, 8);
	memcpy(paramp.sa_key, key, 8);
	status = mpath_prout_common(mpp, MPATH_PROUT_PREE_SA, rq_scope,
				    rq_type, &paramp, noisy, NULL);
	if (status != MPATH_PR_SUCCESS)
		condlog(0, "%s: register: pr preempt command failed.", mpp->wwid);
}

/*
 * If libmpathpersist fails at updating the key on a path with a retryable
 * error, it has probably failed. But there is a chance that the path is
 * still usable. To make sure a path isn't active without a key, when it
 * should have one, or with a key, when it shouldn't have one, check if
 * the path is still usable. If it is, we must fail the registration.
 */
static int
check_failed_paths(struct multipath *mpp, struct threadinfo *thread, int count)
{
	int i, j, k;
	int ret;
	struct pathgroup *pgp;
	struct path *pp;
	struct config *conf;

	for (i = 0; i < count; i++) {
		if (thread[i].param.status != MPATH_PR_RETRYABLE_ERROR)
			continue;
		vector_foreach_slot (mpp->pg, pgp, j) {
			vector_foreach_slot (pgp->paths, pp, k) {
				if (strncmp(pp->dev, thread[i].param.dev,
					    FILE_NAME_SIZE) == 0)
					goto match;
			}
		}
		/* no match. This shouldn't ever happen. */
		condlog(0, "%s: Error: can't find path %s", mpp->wwid,
			thread[i].param.dev);
		continue;
	match:
		conf = get_multipath_config();
		ret = pathinfo(pp, conf, DI_CHECKER);
		put_multipath_config(conf);
		/* If pathinfo fails, or if the path is active, return error */
		if (ret != PATHINFO_OK || pp->state == PATH_UP ||
		    pp->state == PATH_GHOST)
			return MPATH_PR_OTHER;
	}
	return MPATH_PR_SUCCESS;
}

static int mpath_prout_reg(struct multipath *mpp,int rq_servact, int rq_scope,
			   unsigned int rq_type,
			   struct prout_param_descriptor * paramp, int noisy)
{

	int i, j, k;
	struct pathgroup *pgp = NULL;
	struct path *pp = NULL;
	bool had_success = false;
	bool need_retry = false;
	bool retryable_error = false;
	int active_pathcount=0;
	int rc;
	int count=0;
	int status = MPATH_PR_SUCCESS;
	int all_tg_pt;

	if (!mpp)
		return MPATH_PR_DMMP_ERROR;

	all_tg_pt = (mpp->all_tg_pt == ALL_TG_PT_ON ||
		     paramp->sa_flags & MPATH_F_ALL_TG_PT_MASK);
	active_pathcount = count_active_paths(mpp);

	if (active_pathcount == 0) {
		condlog (0, "%s: no path available", mpp->wwid);
		return MPATH_PR_DMMP_ERROR;
	}

	struct threadinfo thread[active_pathcount];
	int hosts[active_pathcount];

	memset(thread, 0, sizeof(thread));

	/* init thread parameter */
	for (i =0; i< active_pathcount; i++){
		hosts[i] = -1;
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
			if (all_tg_pt && pp->sg_id.host_no != -1) {
				for (k = 0; k < count; k++) {
					if (pp->sg_id.host_no == hosts[k]) {
						condlog(3, "%s: %s host %d matches skip.", pp->wwid, pp->dev, pp->sg_id.host_no);
						break;
					}
				}
				if (k < count)
					continue;
			}
			strlcpy(thread[count].param.dev, pp->dev,
				FILE_NAME_SIZE);

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
			else
				hosts[count] = pp->sg_id.host_no;
			count = count + 1;
		}
	}
	for( i=0; i < count ; i++){
		if (thread[i].param.status != MPATH_PR_THREAD_ERROR) {
			rc = pthread_join(thread[i].id, NULL);
			if (rc){
				condlog (0, "%s: Thread[%d] failed to join thread %d", mpp->wwid, i, rc);
			}
		}
		/*
		 * We only retry if there is at least one registration that
		 * returned a reservation conflict (which we need to retry)
		 * and at least one registration the return success, so we
		 * know that the command worked on some of the paths. If
		 * the registation fails on all paths, then it wasn't a
		 * valid request, so there's no need to retry.
		 */
		if (thread[i].param.status == MPATH_PR_RESERV_CONFLICT)
			need_retry = true;
		else if (thread[i].param.status == MPATH_PR_RETRYABLE_ERROR)
			retryable_error = true;
		else if (thread[i].param.status == MPATH_PR_SUCCESS)
			had_success = true;
		else if (status == MPATH_PR_SUCCESS)
			status = thread[i].param.status;
	}
	if (need_retry && had_success && rq_servact == MPATH_PROUT_REG_SA &&
	    status == MPATH_PR_SUCCESS) {
		condlog(3, "%s: ERROR: initiating pr out retry", mpp->wwid);
		retryable_error = false;
		for (i = 0; i < count; i++) {
			/* retry retryable errors and conflicts */
			if (thread[i].param.status != MPATH_PR_RESERV_CONFLICT &&
			    thread[i].param.status != MPATH_PR_RETRYABLE_ERROR) {
				thread[i].param.status = MPATH_PR_SKIP;
				continue;
			}
			/*
			 * retry using MPATH_PROUT_REG_IGN_SA to avoid
			 * conflicts. We already know that some paths
			 * succeeded using MPATH_PROUT_REG_SA.
			 */
			thread[i].param.rq_servact = MPATH_PROUT_REG_IGN_SA;
			rc = pthread_create(&thread[i].id, &attr,
					    mpath_prout_pthread_fn,
					    (void *)(&thread[i].param));
			if (rc) {
				condlog(0, "%s: failed to create thread for retry. %d",
					mpp->wwid, rc);
				thread[i].param.status = MPATH_PR_THREAD_ERROR;
			}
		}
		for (i = 0; i < count; i++) {
			if (thread[i].param.status != MPATH_PR_SKIP &&
			    thread[i].param.status != MPATH_PR_THREAD_ERROR) {
				rc = pthread_join(thread[i].id, NULL);
				if (rc) {
					condlog(3, "%s: failed to join thread while retrying %d",
						mpp->wwid, i);
				}
				if (thread[i].param.status ==
				    MPATH_PR_RETRYABLE_ERROR)
					retryable_error = true;
				else if (status == MPATH_PR_SUCCESS)
					status = thread[i].param.status;
			}
		}
		need_retry = false;
	}

	pthread_attr_destroy(&attr);
	if (need_retry)
		return MPATH_PR_RESERV_CONFLICT;
	if (status != MPATH_PR_SUCCESS)
		return status;
	/* If you had retryable errors on all paths, fail the registration */
	if (!had_success)
		return MPATH_PR_OTHER;
	if (retryable_error)
		status = check_failed_paths(mpp, thread, count);
	if (status == MPATH_PR_SUCCESS)
		preempt_missing_path(mpp, paramp->key, paramp->sa_key, noisy);
	return status;
}

/*
 * Called to make a multipath device preempt its own reservation (and
 * optionally release the reservation). Doing this causes the reservation
 * keys to be removed from all the device paths except that path used to issue
 * the preempt, so they need to be restored. To avoid the chance that IO
 * goes to these paths when they don't have a registered key, the device
 * is suspended before issuing the preemption, and the keys are reregistered
 * before resuming it.
 */
static int preempt_self(struct multipath *mpp, int rq_servact, int rq_scope,
			unsigned int rq_type, int noisy, bool do_release)
{
	int status, rel_status = MPATH_PR_SUCCESS;
	struct path *pp = NULL;
	struct prout_param_descriptor paramp = {.sa_flags = 0};
	uint16_t udev_flags = (mpp->skip_kpartx) ? MPATH_UDEV_NO_KPARTX_FLAG : 0;

	if (!dm_simplecmd_noflush(DM_DEVICE_SUSPEND, mpp->alias, 0)) {
		condlog(0, "%s: self preempt failed to suspend device.", mpp->wwid);
		return MPATH_PR_OTHER;
	}

	memcpy(paramp.key, &mpp->reservation_key, 8);
	memcpy(paramp.sa_key, &mpp->reservation_key, 8);
	status = mpath_prout_common(mpp, rq_servact, rq_scope, rq_type,
				    &paramp, noisy, &pp);
	if (status != MPATH_PR_SUCCESS) {
		condlog(0, "%s: self preempt command failed.", mpp->wwid);
		goto fail_resume;
	}

	if (do_release) {
		memset(&paramp, 0, sizeof(paramp));
		memcpy(paramp.key, &mpp->reservation_key, 8);
		rel_status = prout_do_scsi_ioctl(pp->dev, MPATH_PROUT_REL_SA,
						 rq_scope, rq_type, &paramp, noisy);
		if (rel_status != MPATH_PR_SUCCESS)
			condlog(0, "%s: release on alternate path failed.",
				mpp->wwid);
	}

	memset(&paramp, 0, sizeof(paramp));
	memcpy(paramp.sa_key, &mpp->reservation_key, 8);
	status = mpath_prout_reg(mpp, MPATH_PROUT_REG_IGN_SA, rq_scope,
				 rq_type, &paramp, noisy);
	if (status != MPATH_PR_SUCCESS)
		condlog(0, "%s: self preempt failed to reregister paths.",
			mpp->wwid);

fail_resume:
	if (!dm_simplecmd_noflush(DM_DEVICE_RESUME, mpp->alias, udev_flags)) {
		condlog(0, "%s: self preempt failed to resume device.", mpp->wwid);
		if (status == MPATH_PR_SUCCESS)
			status = MPATH_PR_OTHER;
	}
	/* return the first error we encountered */
	return (rel_status != MPATH_PR_SUCCESS) ? rel_status : status;
}

static int mpath_prout_rel(struct multipath *mpp,int rq_servact, int rq_scope,
			   unsigned int rq_type,
			   struct prout_param_descriptor * paramp, int noisy)
{
	int i, j;
	int num = 0;
	struct pathgroup *pgp = NULL;
	struct path *pp = NULL;
	int active_pathcount = 0;
	pthread_attr_t attr;
	int rc;
	int count = 0;
	int status = MPATH_PR_SUCCESS;
	struct prin_resp resp = {{{.prgeneration = 0}}};
	bool all_threads_failed;
	unsigned int scope_type;

	if (!mpp)
		return MPATH_PR_DMMP_ERROR;

	active_pathcount = count_active_paths(mpp);

	if (active_pathcount == 0) {
		condlog (0, "%s: no path available", mpp->wwid);
		return MPATH_PR_DMMP_ERROR;
	}

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

			strlcpy(thread[count].param.dev, pp->dev,
				FILE_NAME_SIZE);
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
	for (i = 0; i < count; i++){
		if (thread[i].param.status != MPATH_PR_THREAD_ERROR) {
			rc = pthread_join (thread[i].id, NULL);
			if (rc){
				condlog (1, "%s: failed to join thread.  %d",  mpp->wwid,  rc);
			}
		}
	}

	all_threads_failed = true;
	for (i = 0; i < count; i++){
		/*  check thread status here and return the status */

		if (thread[i].param.status == MPATH_PR_SUCCESS)
			all_threads_failed = false;
		else if (thread[i].param.status == MPATH_PR_RESERV_CONFLICT)
			status = MPATH_PR_RESERV_CONFLICT;
		else if (status == MPATH_PR_SUCCESS)
			status = thread[i].param.status;
	}
	if (all_threads_failed) {
		condlog(0, "%s: all threads failed to release reservation.",
			mpp->wwid);
		return status;
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
	if (!get_be64(mpp->reservation_key) ||
	    memcmp(&mpp->reservation_key, resp.prin_descriptor.prin_readresv.key, 8)) {
		condlog(2, "%s: Releasing key not holding reservation.", mpp->wwid);
		return MPATH_PR_SUCCESS;
	}

	scope_type = resp.prin_descriptor.prin_readresv.scope_type;
	if ((scope_type & MPATH_PR_TYPE_MASK) != rq_type) {
		condlog(2, "%s: --prout_type %u doesn't match reservation %u",
			mpp->wwid, rq_type, scope_type & MPATH_PR_TYPE_MASK);
		return MPATH_PR_RESERV_CONFLICT;
	}

	condlog (2, "%s: Path holding reservation is not available.", mpp->wwid);
	/*
	 * Cannot free the reservation because the path that is holding it
	 * is not usable. Workaround this by:
	 * 1. Suspending the device
	 * 2. Preempting the reservation to move it to a usable path
	 *    (this removes the registered keys on all paths except the
	 *    preempting one. Since the device is suspended, no IO can
	 *    go to these unregistered paths and fail).
	 * 3. Releasing the reservation on the path that now holds it.
	 * 4. Reregistering keys on all the paths
	 * 5. Resuming the device
	 */
	return preempt_self(mpp, MPATH_PROUT_PREE_SA, rq_scope, rq_type, noisy, true);
}

static int reservation_key_matches(struct multipath *mpp, uint8_t *key, int noisy)
{
	struct prin_resp resp = {{{.prgeneration = 0}}};
	int status;

	status = mpath_prin_activepath(mpp, MPATH_PRIN_RRES_SA, &resp, noisy);
	if (status != MPATH_PR_SUCCESS) {
		condlog(0, "%s: pr in read reservation command failed.", mpp->wwid);
		return YNU_UNDEF;
	}
	if (!resp.prin_descriptor.prin_readresv.additional_length)
		return YNU_NO;
	if (memcmp(key, resp.prin_descriptor.prin_readresv.key, 8) == 0)
		return YNU_YES;
	return YNU_NO;
}

/*
 * for MPATH_PROUT_REG_IGN_SA, we use the ignored paramp->key to store the
 * currently registered key for use in preempt_missing_path(), but only if
 * the key is holding the reservation.
 */
static void set_ignored_key(struct multipath *mpp, uint8_t *key)
{
	memset(key, 0, 8);
	if (!get_be64(mpp->reservation_key))
		return;
	if (get_prhold(mpp->alias) == PR_UNSET)
		return;
	if (reservation_key_matches(mpp, key, 0) == YNU_NO)
		return;
	memcpy(key, &mpp->reservation_key, 8);
}

int do_mpath_persistent_reserve_out(vector curmp, vector pathvec, int fd,
				    int rq_servact, int rq_scope, unsigned int rq_type,
				    struct prout_param_descriptor *paramp, int noisy)
{
	struct multipath *mpp;
	int ret;
	uint64_t zerokey = 0;
	struct be64 oldkey = {0};
	struct config *conf;
	bool unregistering, preempting_reservation = false;
	bool updated_prkey = false;

	ret = mpath_get_map(curmp, fd, &mpp);
	if (ret != MPATH_PR_SUCCESS)
		return ret;

	conf = get_multipath_config();
	mpp->mpe = find_mpe(conf->mptable, mpp->wwid);
	select_reservation_key(conf, mpp);
	put_multipath_config(conf);

	unregistering = (memcmp(&zerokey, paramp->sa_key, 8) == 0);
	if (mpp->prkey_source == PRKEY_SOURCE_FILE &&
	    (rq_servact == MPATH_PROUT_REG_IGN_SA ||
	     (rq_servact == MPATH_PROUT_REG_SA &&
	      (!get_be64(mpp->reservation_key) ||
	       memcmp(paramp->key, &zerokey, 8) == 0 ||
	       memcmp(paramp->key, &mpp->reservation_key, 8) == 0)))) {
		updated_prkey = true;
		memcpy(&oldkey, &mpp->reservation_key, 8);
		memcpy(&mpp->reservation_key, paramp->sa_key, 8);
		if (update_prkey_flags(mpp->alias, get_be64(mpp->reservation_key),
				       paramp->sa_flags)) {
			condlog(0, "%s: failed to set prkey for multipathd.",
				mpp->alias);
			return MPATH_PR_DMMP_ERROR;
		}
	}

	/*
	 * If you are registering a non-zero key, mpp->reservation_key
	 * must be set and must equal paramp->sa_key.
	 * If you're not registering a key, mpp->reservation_key must be
	 * set, and must equal paramp->key
	 * If you updated the reservation key above, then you cannot fail
	 * these checks, since mpp->reservation_key has already been set
	 * to match paramp->sa_key, and if you are registering a non-zero
	 * key, then it must be set to a non-zero value.
	 */
	if ((rq_servact == MPATH_PROUT_REG_IGN_SA ||
	     rq_servact == MPATH_PROUT_REG_SA)) {
		if (!unregistering && !get_be64(mpp->reservation_key)) {
			condlog(0, "%s: no configured reservation key", mpp->alias);
			return MPATH_PR_SYNTAX_ERROR;
		}
		if (!unregistering &&
		    memcmp(paramp->sa_key, &mpp->reservation_key, 8)) {
			condlog(0, "%s: configured reservation key doesn't match: 0x%" PRIx64,
				mpp->alias, get_be64(mpp->reservation_key));
			return MPATH_PR_SYNTAX_ERROR;
		}
	} else {
		if (!get_be64(mpp->reservation_key)) {
			condlog(0, "%s: no configured reservation key", mpp->alias);
			return MPATH_PR_SYNTAX_ERROR;
		}
		if (memcmp(paramp->key, &mpp->reservation_key, 8)) {
			condlog(0, "%s: configured reservation key doesn't match: 0x%" PRIx64,
				mpp->alias, get_be64(mpp->reservation_key));
			return MPATH_PR_SYNTAX_ERROR;
		}
	}

	ret = get_path_info(mpp, pathvec);
	if (ret != MPATH_PR_SUCCESS) {
		if (updated_prkey)
			update_prkey_flags(mpp->alias, get_be64(oldkey),
					   mpp->sa_flags);
		return ret;
	}

	conf = get_multipath_config();
	select_all_tg_pt(conf, mpp);
	/*
	 * If a device preempts itself, it will need to suspend and resume.
	 * Set mpp->skip_kpartx to make sure we set the flags to skip kpartx
	 * if necessary, when doing this.
	 */
	select_skip_kpartx(conf, mpp);
	put_multipath_config(conf);

	if (rq_servact == MPATH_PROUT_REG_IGN_SA)
		set_ignored_key(mpp, paramp->key);

	switch(rq_servact)
	{
	case MPATH_PROUT_REG_SA:
	case MPATH_PROUT_REG_IGN_SA:
		ret= mpath_prout_reg(mpp, rq_servact, rq_scope, rq_type, paramp, noisy);
		break;
	case MPATH_PROUT_PREE_SA:
	case MPATH_PROUT_PREE_AB_SA:
		if (reservation_key_matches(mpp, paramp->sa_key, noisy) == YNU_YES)
			preempting_reservation = true;
		/* if we are preempting ourself */
		if (memcmp(paramp->sa_key, paramp->key, 8) == 0) {
			ret = preempt_self(mpp, rq_servact, rq_scope, rq_type,
					   noisy, false);
			break;
		}
		/* fallthrough */
	case MPATH_PROUT_RES_SA:
	case MPATH_PROUT_CLEAR_SA:
		ret = mpath_prout_common(mpp, rq_servact, rq_scope, rq_type,
					 paramp, noisy, NULL);
		break;
	case MPATH_PROUT_REL_SA:
		ret = mpath_prout_rel(mpp, rq_servact, rq_scope, rq_type, paramp, noisy);
		break;
	default:
		return MPATH_PR_OTHER;
	}

	if (ret != MPATH_PR_SUCCESS) {
		if (updated_prkey)
			update_prkey_flags(mpp->alias, get_be64(oldkey),
					   mpp->sa_flags);
		return ret;
	}

	switch (rq_servact) {
	case MPATH_PROUT_REG_SA:
	case MPATH_PROUT_REG_IGN_SA:
		if (unregistering)
			update_prflag(mpp->alias, 0);
		else
			update_prflag(mpp->alias, 1);
		break;
	case MPATH_PROUT_CLEAR_SA:
		update_prflag(mpp->alias, 0);
		if (mpp->prkey_source == PRKEY_SOURCE_FILE)
			update_prkey(mpp->alias, 0);
		break;
	case MPATH_PROUT_RES_SA:
	case MPATH_PROUT_REL_SA:
		update_prhold(mpp->alias, (rq_servact == MPATH_PROUT_RES_SA));
		break;
	case MPATH_PROUT_PREE_SA:
	case MPATH_PROUT_PREE_AB_SA:
		if (preempting_reservation)
			update_prhold(mpp->alias, true);
	}
	return ret;
}
