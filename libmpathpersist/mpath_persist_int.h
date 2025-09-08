#ifndef MPATH_PERSIST_INT_H_INCLUDED
#define MPATH_PERSIST_INT_H_INCLUDED

/*
 * This header file contains symbols that are used by multipath-tools
 * but aren't part of the public libmpathpersist API.
 */

void * mpath_alloc_prin_response(int prin_sa);
int do_mpath_persistent_reserve_in(vector curmp, vector pathvec,
				   int fd, int rq_servact,
				   struct prin_resp *resp, int noisy);
void *mpath_alloc_prin_response(int prin_sa);
int do_mpath_persistent_reserve_out(vector curmp, vector pathvec, int fd,
				    int rq_servact, int rq_scope,
				    unsigned int rq_type,
				    struct prout_param_descriptor *paramp,
				    int noisy);
int prin_do_scsi_ioctl(char * dev, int rq_servact, struct prin_resp * resp, int noisy);
int prout_do_scsi_ioctl( char * dev, int rq_servact, int rq_scope,
			 unsigned int rq_type, struct prout_param_descriptor *paramp, int noisy);
void dumpHex(const char* , int len, int no_ascii);
int update_map_pr(struct multipath *mpp, struct path *pp);

#endif /* MPATH_PERSIST_INT_H_INCLUDED */
