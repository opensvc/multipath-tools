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

static int mpath_send_prin_activepath (char * dev, int rq_servact,
				struct prin_resp * resp, int noisy)
{

	int rc;

	rc = prin_do_scsi_ioctl(dev, rq_servact, resp,  noisy);

	return (rc);
}

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

static int get_mpvec(vector curmp, vector pathvec, char *refwwid)
{
	int i;
	struct multipath *mpp;

	vector_foreach_slot (curmp, mpp, i){
		/*
		 * discard out of scope maps
		 */
		if (!mpp->alias) {
			condlog(0, "%s: map with empty alias!", __func__);
			continue;
		}

		if (mpp->pg != NULL)
			/* Already seen this one */
			continue;

		if (refwwid && strncmp (mpp->alias, refwwid, WWID_SIZE - 1))
			continue;

		if (update_multipath_table(mpp, pathvec, DI_CHECKER) != DMP_OK ||
		    update_mpp_paths(mpp, pathvec)) {
			condlog(1, "error parsing map %s", mpp->wwid);
			remove_map(mpp, pathvec, curmp);
			i--;
		} else
			extract_hwe_from_path(mpp);
	}
	return MPATH_PR_SUCCESS ;
}

static int mpath_get_map(vector curmp, vector pathvec, int fd, struct multipath **pmpp)
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

	/* get info of all paths from the dm device     */
	if (get_mpvec(curmp, pathvec, alias)) {
		condlog(0, "%s: failed to get device info.", alias);
		return MPATH_PR_DMMP_ERROR;
	}

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

	ret = mpath_get_map(curmp, pathvec, fd, &mpp);
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

static int mpath_prout_reg(struct multipath *mpp,int rq_servact, int rq_scope,
			   unsigned int rq_type,
			   struct prout_param_descriptor * paramp, int noisy)
{

	int i, j, k;
	struct pathgroup *pgp = NULL;
	struct path *pp = NULL;
	int rollback = 0;
	int active_pathcount=0;
	int rc;
	int count=0;
	int status = MPATH_PR_SUCCESS;
	int all_tg_pt;
	uint64_t sa_key = 0;

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
		if (!rollback && (thread[i].param.status == MPATH_PR_RESERV_CONFLICT)){
			rollback = 1;
			sa_key = get_unaligned_be64(&paramp->sa_key[0]);
			status = MPATH_PR_RESERV_CONFLICT ;
		}
		if (!rollback && (status == MPATH_PR_SUCCESS)){
			status = thread[i].param.status;
		}
	}
	if (rollback && ((rq_servact == MPATH_PROUT_REG_SA) && sa_key != 0 )){
		condlog (3, "%s: ERROR: initiating pr out rollback", mpp->wwid);
		memcpy(&paramp->key, &paramp->sa_key, 8);
		memset(&paramp->sa_key, 0, 8);
		for( i=0 ; i < count ; i++){
			if(thread[i].param.status == MPATH_PR_SUCCESS) {
				rc = pthread_create(&thread[i].id, &attr, mpath_prout_pthread_fn,
						(void *)(&thread[i].param));
				if (rc){
					condlog (0, "%s: failed to create thread for rollback. %d",  mpp->wwid, rc);
					thread[i].param.status = MPATH_PR_THREAD_ERROR;
				}
			} else
				thread[i].param.status = MPATH_PR_SKIP;
		}
		for(i=0; i < count ; i++){
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

static int send_prout_activepath(char *dev, int rq_servact, int rq_scope,
				 unsigned int rq_type,
				 struct prout_param_descriptor * paramp, int noisy)
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
	strlcpy(param.dev, dev, FILE_NAME_SIZE);
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

static int mpath_prout_common(struct multipath *mpp,int rq_servact, int rq_scope,
			      unsigned int rq_type,
			      struct prout_param_descriptor* paramp, int noisy)
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
	condlog (0, "%s: no path available", mpp->wwid);
	return MPATH_PR_DMMP_ERROR;
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
	int rc, found = 0;
	int count = 0;
	int status = MPATH_PR_SUCCESS;
	struct prin_resp resp;
	struct prout_param_descriptor *pamp = NULL;
	struct prin_resp *pr_buff;
	int length;
	struct transportid *pptr = NULL;

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

	for (i = 0; i < count; i++){
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
	condlog (2, "%s: Path holding reservation is not available.", mpp->wwid);

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
		goto out;
	}

	memset(pamp, 0, length);

	pamp->trnptid_list[0] = (struct transportid *) malloc (sizeof (struct transportid));
	if (!pamp->trnptid_list[0]){
		condlog (0, "%s: failed to alloc pr out transportid.", mpp->wwid);
		goto out1;
	}
	pptr = pamp->trnptid_list[0];

	if (get_be64(mpp->reservation_key)){
		memcpy (pamp->key, &mpp->reservation_key, 8);
		condlog (3, "%s: reservation key set.", mpp->wwid);
	}

	status = mpath_prout_common (mpp, MPATH_PROUT_CLEAR_SA,
				     rq_scope, rq_type, pamp, noisy);

	if (status) {
		condlog(0, "%s: failed to send CLEAR_SA", mpp->wwid);
		goto out2;
	}

	pamp->num_transportid = 1;

	for (i = 0; i < num; i++){
		if (get_be64(mpp->reservation_key) &&
			memcmp(pr_buff->prin_descriptor.prin_readfd.descriptors[i]->key,
			       &mpp->reservation_key, 8)){
			/*register with transport id*/
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

out2:
	free(pptr);
out1:
	free (pamp);
out:
	free (pr_buff);
	return (status);
}

int do_mpath_persistent_reserve_out(vector curmp, vector pathvec, int fd,
				    int rq_servact, int rq_scope, unsigned int rq_type,
				    struct prout_param_descriptor *paramp, int noisy)
{
	struct multipath *mpp;
	int ret;
	uint64_t prkey;
	struct config *conf;

	ret = mpath_get_map(curmp, pathvec, fd, &mpp);
	if (ret != MPATH_PR_SUCCESS)
		return ret;

	conf = get_multipath_config();
	mpp->mpe = find_mpe(conf->mptable, mpp->wwid);
	select_reservation_key(conf, mpp);
	select_all_tg_pt(conf, mpp);
	put_multipath_config(conf);

	memcpy(&prkey, paramp->sa_key, 8);
	if (mpp->prkey_source == PRKEY_SOURCE_FILE && prkey &&
	    (rq_servact == MPATH_PROUT_REG_IGN_SA ||
	     (rq_servact == MPATH_PROUT_REG_SA &&
	      (!get_be64(mpp->reservation_key) ||
	       memcmp(paramp->key, &mpp->reservation_key, 8) == 0)))) {
		memcpy(&mpp->reservation_key, paramp->sa_key, 8);
		if (update_prkey_flags(mpp->alias, get_be64(mpp->reservation_key),
				       paramp->sa_flags)) {
			condlog(0, "%s: failed to set prkey for multipathd.",
				mpp->alias);
			return MPATH_PR_DMMP_ERROR;
		}
	}

	if (!get_be64(mpp->reservation_key) &&
	    (prkey || rq_servact != MPATH_PROUT_REG_IGN_SA)) {
		condlog(0, "%s: no configured reservation key", mpp->alias);
		return MPATH_PR_SYNTAX_ERROR;
	}

	if (memcmp(paramp->key, &mpp->reservation_key, 8) &&
	    memcmp(paramp->sa_key, &mpp->reservation_key, 8) &&
	    (prkey || rq_servact != MPATH_PROUT_REG_IGN_SA)) {
		condlog(0, "%s: configured reservation key doesn't match: 0x%" PRIx64,
			mpp->alias, get_be64(mpp->reservation_key));
		return MPATH_PR_SYNTAX_ERROR;
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
		return MPATH_PR_OTHER;
	}

	if ((ret == MPATH_PR_SUCCESS) && ((rq_servact == MPATH_PROUT_REG_SA) ||
				(rq_servact ==  MPATH_PROUT_REG_IGN_SA)))
	{
		if (prkey == 0) {
			update_prflag(mpp->alias, 0);
			update_prkey(mpp->alias, 0);
		} else
			update_prflag(mpp->alias, 1);
	} else if ((ret == MPATH_PR_SUCCESS) && (rq_servact == MPATH_PROUT_CLEAR_SA)) {
		update_prflag(mpp->alias, 0);
		update_prkey(mpp->alias, 0);
	}
	return ret;
}

int update_map_pr(struct multipath *mpp)
{
	int noisy=0;
	struct prin_resp *resp;
	unsigned int i;
	int ret = MPATH_PR_OTHER, isFound;

	if (!get_be64(mpp->reservation_key))
	{
		/* Nothing to do. Assuming pr mgmt feature is disabled*/
		mpp->prflag = PRFLAG_UNSET;
		condlog(4, "%s: reservation_key not set in multipath.conf",
			mpp->alias);
		return MPATH_PR_SUCCESS;
	}

	resp = mpath_alloc_prin_response(MPATH_PRIN_RKEY_SA);
	if (!resp)
	{
		condlog(0,"%s : failed to alloc resp in update_map_pr", mpp->alias);
		return MPATH_PR_OTHER;
	}
	if (count_active_paths(mpp) == 0)
	{
		condlog(0,"%s: No available paths to check pr status",
			mpp->alias);
		goto out;
	}
	mpp->prflag = PRFLAG_UNSET;
	ret = mpath_prin_activepath(mpp, MPATH_PRIN_RKEY_SA, resp, noisy);

	if (ret != MPATH_PR_SUCCESS )
	{
		condlog(0,"%s : pr in read keys service action failed Error=%d", mpp->alias, ret);
		goto out;
	}

	ret = MPATH_PR_SUCCESS;

	if (resp->prin_descriptor.prin_readkeys.additional_length == 0 )
	{
		condlog(3,"%s: No key found. Device may not be registered. ", mpp->alias);
		goto out;
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
		mpp->prflag = PRFLAG_SET;
		condlog(2, "%s: prflag flag set.", mpp->alias );
	}

out:
	free(resp);
	return ret;
}
