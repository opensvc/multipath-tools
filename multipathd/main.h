#ifndef MAIN_H
#define MAIN_H

#define MAPGCINT 5

enum daemon_status {
	DAEMON_INIT,
	DAEMON_START,
	DAEMON_CONFIGURE,
	DAEMON_IDLE,
	DAEMON_RUNNING,
	DAEMON_SHUTDOWN,
};

struct prout_param_descriptor;
struct prin_resp;

extern pid_t daemon_pid;
extern int uxsock_timeout;

void exit_daemon(void);
const char * daemon_status(void);
enum daemon_status wait_for_state_change_if(enum daemon_status oldstate,
					    unsigned long ms);
int need_to_delay_reconfig (struct vectors *);
int reconfigure (struct vectors *);
int ev_add_path (struct path *, struct vectors *, int);
int ev_remove_path (struct path *, struct vectors *, int);
int ev_add_map (char *, const char *, struct vectors *);
int ev_remove_map (char *, char *, int, struct vectors *);
int set_config_state(enum daemon_status);
void * mpath_alloc_prin_response(int prin_sa);
int prin_do_scsi_ioctl(char *, int rq_servact, struct prin_resp * resp,
		       int noisy);
void dumpHex(const char * , int len, int no_ascii);
int prout_do_scsi_ioctl(char * , int rq_servact, int rq_scope,
			unsigned int rq_type,
			struct prout_param_descriptor *param, int noisy);
int mpath_pr_event_handle(struct path *pp);
void * mpath_pr_event_handler_fn (void * );
int update_map_pr(struct multipath *mpp);
void * mpath_pr_event_handler_fn (void * pathp );
void handle_signals(bool);
int __setup_multipath (struct vectors * vecs, struct multipath * mpp,
		       int reset);
#define setup_multipath(vecs, mpp) __setup_multipath(vecs, mpp, 1)
int update_multipath (struct vectors *vecs, char *mapname, int reset);
int update_path_groups(struct multipath *mpp, struct vectors *vecs,
		       int refresh);

#endif /* MAIN_H */
