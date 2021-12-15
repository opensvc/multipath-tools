#ifndef _MPATH_PERSIST_INT_H
#define _MPATH_PERSIST_INT_H

struct multipath;

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

#endif /* _MPATH_PERSIST_INT_H */
