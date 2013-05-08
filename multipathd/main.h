#ifndef MAIN_H
#define MAIN_H

#define MAPGCINT 5

enum daemon_status {
    DAEMON_INIT,
    DAEMON_START,
    DAEMON_CONFIGURE,
    DAEMON_RUNNING,
    DAEMON_SHUTDOWN,
};

struct prout_param_descriptor;
struct prin_resp;

extern pid_t daemon_pid;

void exit_daemon(void);
const char * daemon_status(void);
int reconfigure (struct vectors *);
int ev_add_path (struct path *, struct vectors *);
int ev_remove_path (struct path *, struct vectors *);
int ev_add_map (char *, char *, struct vectors *);
int ev_remove_map (char *, char *, int, struct vectors *);
void sync_map_state (struct multipath *);
void * mpath_alloc_prin_response(int prin_sa);
int prin_do_scsi_ioctl(char *, int rq_servact, struct prin_resp * resp,
       int noisy);
void dumpHex(const char * , int len, int no_ascii);
int prout_do_scsi_ioctl(char * , int rq_servact, int rq_scope,
       unsigned int rq_type, struct prout_param_descriptor *param,
       int noisy);
int mpath_pr_event_handle(struct path *pp);
void * mpath_pr_event_handler_fn (void * );
int update_map_pr(struct multipath *mpp);
void * mpath_pr_event_handler_fn (void * pathp );
void handle_signals(void);

#endif /* MAIN_H */
