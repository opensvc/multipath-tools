#ifndef MPATHPR_H
#define MPATHPR_H

struct prin_param {
	char dev[FILE_NAME_SIZE];
        int rq_servact;
        struct prin_resp *resp;
        int noisy;
        int status;
};

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


struct config * conf;


int prin_do_scsi_ioctl(char * dev, int rq_servact, struct prin_resp * resp, int noisy);
int prout_do_scsi_ioctl( char * dev, int rq_servact, int rq_scope,
                unsigned int rq_type, struct prout_param_descriptor *paramp, int noisy);
void * _mpath_pr_update (void *arg);
int mpath_send_prin_activepath (char * dev, int rq_servact, struct prin_resp * resp, int noisy);
int get_mpvec (vector curmp, vector pathvec, char * refwwid);
void * mpath_prout_pthread_fn(void *p);
void dumpHex(const char* , int len, int no_ascii);

int mpath_prout_reg(struct multipath *mpp,int rq_servact, int rq_scope,
        unsigned int rq_type,  struct prout_param_descriptor * paramp, int noisy);
int mpath_prout_common(struct multipath *mpp,int rq_servact, int rq_scope,
        unsigned int rq_type,  struct prout_param_descriptor * paramp, int noisy);
int mpath_prout_rel(struct multipath *mpp,int rq_servact, int rq_scope,
        unsigned int rq_type,  struct prout_param_descriptor * paramp, int noisy);
int send_prout_activepath(char * dev, int rq_servact, int rq_scope,
        unsigned int rq_type,   struct prout_param_descriptor * paramp, int noisy);

int update_prflag(char * arg1, char * arg2, int noisy);
void * mpath_alloc_prin_response(int prin_sa);
int update_map_pr(struct multipath *mpp);
int devt2devname (char *devname, char *devt);

#endif  
